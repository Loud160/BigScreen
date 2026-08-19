// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

#include "BigScreen/DownloaderActivation.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
    namespace fs = std::filesystem;

    void Write(const fs::path& path, const std::string& value)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << value;
        assert(stream.good());
    }

    std::string Read(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
    }

    struct TemporaryDirectory final {
        TemporaryDirectory()
        {
            const auto stamp = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            path = fs::temp_directory_path() /
                ("bigscreen-downloader-activation-" + std::to_string(stamp));
            fs::create_directories(path);
        }
        ~TemporaryDirectory()
        {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
        fs::path path;
    };

    void Seed(const fs::path& root)
    {
        Write(root / "yt-dlp-active", "known-good-package");
        Write(root / "yt-dlp-active.version", "2026.08.01");
        Write(root / "yt-dlp-active.channel", "stable");
        Write(root / "yt-dlp-next", "candidate-package");
        Write(root / "yt-dlp-next.version", "2026.08.19.123456");
        Write(root / "yt-dlp-next.channel", "nightly");
    }
}

int main()
{
    using namespace BigScreen::DownloaderPackage;

    assert(VersionIsOlder("2026.08.01", "2026.08.19.123456"));
    assert(!VersionIsOlder("not-a-version", "2026.08.19"));
    assert(NormalizeChannel("NIGHTLY", "2026.08.19.123456") == "nightly");
    assert(NormalizeChannel("", "2026.08.19") == "stable");

    // An initialization error after promotion returns both package payloads
    // and their metadata to the exact pre-attempt locations.
    {
        TemporaryDirectory directory;
        Seed(directory.path);
        {
            Activation activation(directory.path);
            std::string error;
            assert(activation.Promote(error));
            assert(error.empty());
            assert(activation.Promoted());
            assert(Read(directory.path / "yt-dlp-active") == "candidate-package");
            assert(Read(directory.path / "yt-dlp-previous") ==
                   "known-good-package");
        }
        assert(Read(directory.path / "yt-dlp-active") == "known-good-package");
        assert(Read(directory.path / "yt-dlp-active.version") == "2026.08.01");
        assert(Read(directory.path / "yt-dlp-next") == "candidate-package");
        assert(Read(directory.path / "yt-dlp-next.channel") == "nightly");
    }

    // A candidate that imports but fails validation is removed, the proven
    // package is restored, and the rejected version is persisted so startup
    // will wait for a genuinely newer updater result.
    {
        TemporaryDirectory directory;
        Seed(directory.path);
        Activation activation(directory.path);
        std::string error;
        assert(activation.Promote(error));
        assert(activation.Reject(error));
        assert(Read(directory.path / "yt-dlp-active") == "known-good-package");
        assert(Read(directory.path / "yt-dlp-rejected.version") ==
               "2026.08.19.123456");
        assert(!fs::exists(directory.path / "yt-dlp-next"));
    }

    // Accepting a validated candidate keeps it active and clears a stale
    // rejected-version marker.
    {
        TemporaryDirectory directory;
        Seed(directory.path);
        Write(directory.path / "yt-dlp-rejected.version", "older-rejection");
        {
            Activation activation(directory.path);
            std::string error;
            assert(activation.Promote(error));
            activation.Accept();
        }
        assert(Read(directory.path / "yt-dlp-active") == "candidate-package");
        assert(!fs::exists(directory.path / "yt-dlp-rejected.version"));
    }
}
