#include "BigScreen/ChromaMapDetector.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef QUEST
// Quest's logging header brings in rapidjson-utils.hpp, which must precede
// RapidJSON itself so the shared configuration helpers can install their
// allocator/type customizations without an include-order warning.
#include "main.hpp"
#endif
#include "rapidjson/document.h"

namespace BigScreen {
    namespace {
        bool EqualsIgnoreCase(std::string_view left, std::string_view right)
        {
            return left.size() == right.size() &&
                std::equal(
                    left.begin(), left.end(), right.begin(),
                    [](char a, char b) {
                        return std::tolower(static_cast<unsigned char>(a)) ==
                               std::tolower(static_cast<unsigned char>(b));
                    });
        }

        bool IsChromaDeclarationKey(std::string_view key)
        {
            // Beat Saber v2 uses underscored names while v3/v4 community data
            // normally uses the same words without the prefix. Search these
            // named arrays anywhere because declarations can live in Info.dat
            // or inside one difficulty's customData.
            return EqualsIgnoreCase(key, "_requirements") ||
                   EqualsIgnoreCase(key, "requirements") ||
                   EqualsIgnoreCase(key, "_suggestions") ||
                   EqualsIgnoreCase(key, "suggestions");
        }

        bool ArrayDeclaresChroma(const rapidjson::Value& array)
        {
            if(!array.IsArray())
                return false;
            for(const auto& value : array.GetArray())
            {
                if(value.IsString() &&
                   EqualsIgnoreCase(
                       std::string_view(value.GetString(), value.GetStringLength()),
                       "Chroma"))
                    return true;
            }
            return false;
        }

        bool ContainsChromaDeclaration(const rapidjson::Value& root)
        {
            // Community JSON is untrusted and may be deeply nested. An
            // explicit work stack avoids exhausting the native call stack on
            // a pathological map while preserving the same full-tree search.
            std::vector<const rapidjson::Value*> pending{&root};
            while(!pending.empty())
            {
                const auto* value = pending.back();
                pending.pop_back();
                if(value->IsArray())
                {
                    for(const auto& child : value->GetArray())
                        pending.push_back(&child);
                    continue;
                }
                if(!value->IsObject())
                    continue;
                for(auto member = value->MemberBegin();
                    member != value->MemberEnd(); ++member)
                {
                    const std::string_view key(
                        member->name.GetString(), member->name.GetStringLength());
                    if(IsChromaDeclarationKey(key) &&
                       ArrayDeclaresChroma(member->value))
                        return true;
                    pending.push_back(&member->value);
                }
            }
            return false;
        }

        bool ContainsEnvironmentArray(const rapidjson::Value& root)
        {
            std::vector<const rapidjson::Value*> pending{&root};
            while(!pending.empty())
            {
                const auto* value = pending.back();
                pending.pop_back();
                if(value->IsArray())
                {
                    for(const auto& child : value->GetArray())
                        pending.push_back(&child);
                    continue;
                }
                if(!value->IsObject())
                    continue;
                for(auto member = value->MemberBegin();
                    member != value->MemberEnd(); ++member)
                {
                    const std::string_view key(
                        member->name.GetString(), member->name.GetStringLength());
                    // Chroma's v2 and v3 schemas both use a non-empty array of
                    // scene-object instructions. An environment-name string
                    // in ordinary Info.dat must not count as an override.
                    if((EqualsIgnoreCase(key, "_environment") ||
                        EqualsIgnoreCase(key, "environment")) &&
                       member->value.IsArray() && !member->value.Empty())
                        return true;
                    pending.push_back(&member->value);
                }
            }
            return false;
        }

        bool ReadJson(
            const std::filesystem::path& path,
            rapidjson::Document& document)
        {
            std::ifstream input(path, std::ios::binary);
            if(!input)
                return false;
            const std::string json{
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
            document.Parse(json.data(), json.size());
            return !document.HasParseError();
        }

        struct CacheEntry {
            std::size_t fingerprint = 0;
            bool usesChroma = false;
            std::string reason;
        };
        std::mutex cacheMutex;
        std::unordered_map<std::string, CacheEntry> cache;

        void Mix(std::size_t& value, std::size_t component)
        {
            value ^= component + 0x9e3779b9u + (value << 6u) + (value >> 2u);
        }
    }

    bool ChromaMapDetector::UsesChroma(
        const std::filesystem::path& levelDirectory,
        std::string& reason)
    {
        reason.clear();
        std::error_code directoryError;
        if(!std::filesystem::is_directory(levelDirectory, directoryError))
            return false;

        // Inspect every .dat rather than trusting a particular Info.dat schema.
        // This covers v2, v3, v4, legacy WIP maps, and maps whose author placed
        // the Chroma requirement only inside a difficulty file.
        std::error_code iteratorError;
        std::vector<std::filesystem::path> dataFiles;
        std::size_t fingerprint = 0;
        for(std::filesystem::directory_iterator iterator(levelDirectory, iteratorError), end;
            !iteratorError && iterator != end;
            iterator.increment(iteratorError))
        {
            std::error_code entryError;
            if(!iterator->is_regular_file(entryError) || entryError)
                continue;
            auto extension = iterator->path().extension().string();
            std::transform(
                extension.begin(), extension.end(), extension.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if(extension != ".dat")
                continue;

            const auto path = iterator->path();
            const auto bytes = iterator->file_size(entryError);
            if(entryError)
                continue;
            const auto modified = iterator->last_write_time(entryError);
            if(entryError)
                continue;
            dataFiles.push_back(path);
            Mix(fingerprint, std::hash<std::string>{}(path.filename().string()));
            Mix(fingerprint, std::hash<std::uintmax_t>{}(bytes));
            Mix(fingerprint, std::hash<decltype(modified.time_since_epoch().count())>{}(
                modified.time_since_epoch().count()));
        }

        const auto cacheKey = levelDirectory.lexically_normal().string();
        {
            std::scoped_lock lock(cacheMutex);
            const auto found = cache.find(cacheKey);
            if(found != cache.end() && found->second.fingerprint == fingerprint)
            {
                reason = found->second.reason;
                return found->second.usesChroma;
            }
        }

        bool detected = false;
        for(const auto& path : dataFiles)
        {
            rapidjson::Document document;
            if(!ReadJson(path, document))
            {
#ifdef QUEST
                PaperLogger.warn(
                    "Skipped unreadable or invalid map data while checking Chroma: '{}'",
                    path.string());
#endif
                continue;
            }
            if(ContainsChromaDeclaration(document))
            {
                reason = "Chroma requirement or suggestion in " +
                    path.filename().string();
                detected = true;
                break;
            }
            if(ContainsEnvironmentArray(document))
            {
                reason = "Chroma environment data in " +
                    path.filename().string();
                detected = true;
                break;
            }
        }
        {
            std::scoped_lock lock(cacheMutex);
            cache[cacheKey] = {fingerprint, detected, reason};
        }
        return detected;
    }
}
