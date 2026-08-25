// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

#include "miniz_tdef.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50;
constexpr std::uint32_t kCentralHeaderSignature = 0x02014b50;
constexpr std::uint32_t kEndSignature = 0x06054b50;
constexpr std::uint16_t kUtf8Flag = 0x0800;
constexpr std::uint16_t kStoredMethod = 0;
constexpr std::uint16_t kDeflateMethod = 8;
constexpr std::uint16_t kFixedDosTime = 0;
constexpr std::uint16_t kFixedDosDate = 0x2821; // 2000-01-01
constexpr std::uint32_t kZip32Maximum =
    std::numeric_limits<std::uint32_t>::max();

struct Entry {
    std::string name;
    std::vector<std::uint8_t> payload;
    std::uint16_t method = kStoredMethod;
    std::uint32_t crc32 = 0;
    std::uint32_t compressedLength = 0;
    std::uint32_t length = 0;
    std::uint32_t outputOffset = 0;
};

void Write16(std::ostream& output, std::uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU)};
    output.write(bytes, sizeof(bytes));
}

void Write32(std::ostream& output, std::uint32_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU)};
    output.write(bytes, sizeof(bytes));
}

std::uint32_t Crc32(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xffffffffU;
    for (std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                static_cast<std::uint32_t>(-
                    static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return crc ^ 0xffffffffU;
}

bool OrdinalLess(const std::string& left, const std::string& right) {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [](char a, char b) {
            return static_cast<unsigned char>(a) <
                static_cast<unsigned char>(b);
        });
}

std::string NormalizeEntryName(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.empty() || name.front() == '/' || name.back() == '/' ||
        name == ".." || name.find("../") != std::string::npos ||
        name.find(':') != std::string::npos ||
        name.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("Unsafe ZIP entry name: " + name);
    }
    return name;
}

std::vector<std::string> ReadEntryList(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open ZIP entry list: " +
            path.u8string());
    }

    std::vector<std::string> names;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            names.push_back(NormalizeEntryName(line));
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("Could not read ZIP entry list: " +
            path.u8string());
    }
    if (names.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error(
            "Deterministic ZIP32 cannot contain more than 65535 entries.");
    }

    std::sort(names.begin(), names.end(), OrdinalLess);
    if (std::adjacent_find(names.begin(), names.end()) != names.end()) {
        throw std::runtime_error("Duplicate ZIP entry name in entry list.");
    }
    return names;
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Could not open ZIP input: " + path.u8string());
    }
    const std::streamoff end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > kZip32Maximum) {
        throw std::runtime_error("ZIP32 input is too large: " + path.u8string());
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    }
    if (!input) {
        throw std::runtime_error("Could not read ZIP input: " + path.u8string());
    }
    return data;
}

Entry PrepareEntry(const std::filesystem::path& stageRoot,
                   const std::string& name) {
    Entry entry;
    entry.name = name;
    const std::filesystem::path source =
        stageRoot / std::filesystem::u8path(name);
    std::vector<std::uint8_t> input = ReadFile(source);
    entry.length = static_cast<std::uint32_t>(input.size());
    entry.crc32 = Crc32(input);

    // Level 9 is intentionally fixed. The nondeterministic miniz flag is not
    // enabled, so identical bytes produce identical raw RFC 1951 streams on
    // Windows and Linux. ZIP stores incompressible files instead of growing
    // them solely to claim that every entry was compressed.
    size_t compressedLength = 0;
    const int flags = static_cast<int>(
        tdefl_create_comp_flags_from_zip_params(9, -15, 0));
    void* compressed = tdefl_compress_mem_to_heap(
        input.empty() ? nullptr : input.data(), input.size(),
        &compressedLength, flags);
    if (compressed == nullptr && !input.empty()) {
        throw std::runtime_error("DEFLATE compression failed for: " + name);
    }

    if (compressed != nullptr && compressedLength < input.size()) {
        const auto* begin = static_cast<const std::uint8_t*>(compressed);
        entry.payload.assign(begin, begin + compressedLength);
        entry.method = kDeflateMethod;
    } else {
        entry.payload = std::move(input);
        entry.method = kStoredMethod;
    }
    MZ_FREE(compressed);
    entry.compressedLength =
        static_cast<std::uint32_t>(entry.payload.size());
    return entry;
}

void WriteLocalHeader(std::ostream& output, const Entry& entry) {
    Write32(output, kLocalHeaderSignature);
    Write16(output, entry.method == kDeflateMethod ? 20 : 10);
    Write16(output, kUtf8Flag);
    Write16(output, entry.method);
    Write16(output, kFixedDosTime);
    Write16(output, kFixedDosDate);
    Write32(output, entry.crc32);
    Write32(output, entry.compressedLength);
    Write32(output, entry.length);
    Write16(output, static_cast<std::uint16_t>(entry.name.size()));
    Write16(output, 0);
    output.write(entry.name.data(),
        static_cast<std::streamsize>(entry.name.size()));
}

void WriteCentralHeader(std::ostream& output, const Entry& entry) {
    Write32(output, kCentralHeaderSignature);
    Write16(output, 20); // ZIP 2.0, fixed DOS creator platform.
    Write16(output, entry.method == kDeflateMethod ? 20 : 10);
    Write16(output, kUtf8Flag);
    Write16(output, entry.method);
    Write16(output, kFixedDosTime);
    Write16(output, kFixedDosDate);
    Write32(output, entry.crc32);
    Write32(output, entry.compressedLength);
    Write32(output, entry.length);
    Write16(output, static_cast<std::uint16_t>(entry.name.size()));
    Write16(output, 0); // extra length
    Write16(output, 0); // comment length
    Write16(output, 0); // disk number
    Write16(output, 0); // internal attributes
    Write32(output, 0); // external attributes
    Write32(output, entry.outputOffset);
    output.write(entry.name.data(),
        static_cast<std::streamsize>(entry.name.size()));
}

std::uint32_t CheckedPosition(std::ostream& output, const char* description) {
    const std::streampos position = output.tellp();
    if (position < 0 || static_cast<std::uint64_t>(position) > kZip32Maximum) {
        throw std::runtime_error(std::string("ZIP32 ") + description +
            " exceeded 4 GiB.");
    }
    return static_cast<std::uint32_t>(position);
}

void CreateArchive(const std::filesystem::path& destination,
                   const std::filesystem::path& stageRoot,
                   const std::filesystem::path& entryList) {
    const std::vector<std::string> names = ReadEntryList(entryList);
    const std::filesystem::path temporary = destination.string() + ".building";
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path());
    }

    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Could not create ZIP output: " +
                temporary.u8string());
        }

        std::vector<Entry> entries;
        entries.reserve(names.size());
        for (const std::string& name : names) {
            Entry entry = PrepareEntry(stageRoot, name);
            entry.outputOffset = CheckedPosition(output, "entry offset");
            WriteLocalHeader(output, entry);
            if (!entry.payload.empty()) {
                output.write(
                    reinterpret_cast<const char*>(entry.payload.data()),
                    static_cast<std::streamsize>(entry.payload.size()));
            }
            if (!output) {
                throw std::runtime_error("Could not write ZIP entry: " + name);
            }
            entries.push_back(std::move(entry));
        }

        const std::uint32_t centralOffset =
            CheckedPosition(output, "central directory offset");
        for (const Entry& entry : entries) {
            WriteCentralHeader(output, entry);
        }
        const std::uint32_t endOffset =
            CheckedPosition(output, "central directory size");
        const std::uint32_t centralLength = endOffset - centralOffset;

        Write32(output, kEndSignature);
        Write16(output, 0);
        Write16(output, 0);
        Write16(output, static_cast<std::uint16_t>(entries.size()));
        Write16(output, static_cast<std::uint16_t>(entries.size()));
        Write32(output, centralLength);
        Write32(output, centralOffset);
        Write16(output, 0);
        output.close();
        if (!output) {
            throw std::runtime_error("Could not finalize ZIP output: " +
                temporary.u8string());
        }

        std::filesystem::remove(destination, ignored);
        std::filesystem::rename(temporary, destination);
    } catch (...) {
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: bigscreen-deterministic-zip "
                  << "<destination.zip> <staging-directory> <entry-list>\n";
        return 2;
    }

    try {
        CreateArchive(
            std::filesystem::u8path(argv[1]),
            std::filesystem::u8path(argv[2]),
            std::filesystem::u8path(argv[3]));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Deterministic ZIP creation failed: " << error.what()
                  << '\n';
        return 1;
    }
}
