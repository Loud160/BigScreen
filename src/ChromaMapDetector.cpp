// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/ChromaMapDetector.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
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

        const rapidjson::Value* Member(
            const rapidjson::Value& object,
            const char* modern,
            const char* legacy = nullptr)
        {
            if(!object.IsObject())
                return nullptr;
            if(const auto found = object.FindMember(modern);
               found != object.MemberEnd())
                return &found->value;
            if(legacy)
            {
                if(const auto found = object.FindMember(legacy);
                   found != object.MemberEnd())
                    return &found->value;
            }
            return nullptr;
        }

        std::optional<std::string_view> StringMember(
            const rapidjson::Value& object,
            const char* modern,
            const char* legacy = nullptr)
        {
            const auto* value = Member(object, modern, legacy);
            if(!value || !value->IsString())
                return std::nullopt;
            return std::string_view(value->GetString(), value->GetStringLength());
        }

        std::optional<int> IntMember(
            const rapidjson::Value& object,
            const char* modern,
            const char* legacy = nullptr)
        {
            const auto* value = Member(object, modern, legacy);
            if(!value || !value->IsInt())
                return std::nullopt;
            return value->GetInt();
        }

        std::optional<bool> BoolMember(
            const rapidjson::Value& object,
            const char* modern,
            const char* legacy = nullptr)
        {
            const auto* value = Member(object, modern, legacy);
            if(!value || !value->IsBool())
                return std::nullopt;
            return value->GetBool();
        }

        std::optional<Float3> VectorMember(
            const rapidjson::Value& object,
            const char* modern,
            const char* legacy = nullptr)
        {
            const auto* value = Member(object, modern, legacy);
            if(!value)
                return std::nullopt;
            if(value->IsArray() && value->Size() >= 3 &&
               (*value)[0].IsNumber() && (*value)[1].IsNumber() &&
               (*value)[2].IsNumber())
            {
                return Float3{
                    (*value)[0].GetFloat(),
                    (*value)[1].GetFloat(),
                    (*value)[2].GetFloat()};
            }
            if(!value->IsObject())
                return std::nullopt;
            const auto* x = Member(*value, "x");
            const auto* y = Member(*value, "y");
            const auto* z = Member(*value, "z");
            if(!x || !y || !z ||
               !x->IsNumber() || !y->IsNumber() || !z->IsNumber())
                return std::nullopt;
            return Float3{x->GetFloat(), y->GetFloat(), z->GetFloat()};
        }

        std::optional<std::string_view> DifficultyName(int difficulty)
        {
            switch(difficulty)
            {
                case 0: return "Easy";
                case 1: return "Normal";
                case 2: return "Hard";
                case 3: return "Expert";
                case 4: return "ExpertPlus";
                default: return std::nullopt;
            }
        }

        bool DifficultyMatches(const rapidjson::Value& value, int difficulty)
        {
            if(value.IsInt())
                return value.GetInt() == difficulty;
            if(!value.IsString())
                return false;
            const auto expected = DifficultyName(difficulty);
            return expected && EqualsIgnoreCase(
                std::string_view(value.GetString(), value.GetStringLength()),
                *expected);
        }

        std::optional<std::filesystem::path> InfoPath(
            const std::filesystem::path& levelDirectory)
        {
            std::error_code error;
            for(std::filesystem::directory_iterator iterator(levelDirectory, error), end;
                !error && iterator != end;
                iterator.increment(error))
            {
                std::error_code entryError;
                if(!iterator->is_regular_file(entryError) || entryError)
                    continue;
                if(EqualsIgnoreCase(iterator->path().filename().string(), "Info.dat"))
                    return iterator->path();
            }
            return std::nullopt;
        }

        std::optional<std::filesystem::path> SafeDifficultyPath(
            const std::filesystem::path& levelDirectory,
            std::string_view fileName)
        {
            const std::filesystem::path relative(fileName);
            if(relative.empty() || relative.is_absolute() || relative.has_parent_path())
                return std::nullopt;
            const auto path = levelDirectory / relative;
            std::error_code error;
            if(!std::filesystem::is_regular_file(path, error) || error)
                return std::nullopt;
            return path;
        }

        std::optional<std::filesystem::path> SelectedDifficultyPath(
            const std::filesystem::path& levelDirectory,
            std::string_view characteristic,
            int difficulty)
        {
            if(characteristic.empty() || !DifficultyName(difficulty))
                return std::nullopt;
            const auto infoPath = InfoPath(levelDirectory);
            rapidjson::Document info;
            if(!infoPath || !ReadJson(*infoPath, info))
                return std::nullopt;

            // Legacy/v2 Info.dat groups difficulties beneath a characteristic.
            if(const auto* sets = Member(
                   info, "difficultyBeatmapSets", "_difficultyBeatmapSets");
               sets && sets->IsArray())
            {
                for(const auto& set : sets->GetArray())
                {
                    const auto setCharacteristic = StringMember(
                        set, "beatmapCharacteristicName",
                        "_beatmapCharacteristicName");
                    if(!setCharacteristic ||
                       !EqualsIgnoreCase(*setCharacteristic, characteristic))
                        continue;
                    const auto* maps = Member(
                        set, "difficultyBeatmaps", "_difficultyBeatmaps");
                    if(!maps || !maps->IsArray())
                        continue;
                    for(const auto& map : maps->GetArray())
                    {
                        const auto* mapDifficulty = Member(
                            map, "difficulty", "_difficulty");
                        if(!mapDifficulty ||
                           !DifficultyMatches(*mapDifficulty, difficulty))
                            continue;
                        const auto file = StringMember(
                            map, "beatmapDataFilename", "_beatmapFilename");
                        if(file)
                            return SafeDifficultyPath(levelDirectory, *file);
                    }
                }
            }

            // Current Info.dat stores a flat list and repeats characteristic
            // and difficulty on each entry.
            if(const auto* maps = Member(info, "difficultyBeatmaps");
               maps && maps->IsArray())
            {
                for(const auto& map : maps->GetArray())
                {
                    const auto mapCharacteristic = StringMember(
                        map, "characteristic", "beatmapCharacteristicName");
                    const auto* mapDifficulty = Member(map, "difficulty");
                    if(!mapCharacteristic || !mapDifficulty ||
                       !EqualsIgnoreCase(*mapCharacteristic, characteristic) ||
                       !DifficultyMatches(*mapDifficulty, difficulty))
                        continue;
                    const auto file = StringMember(
                        map, "beatmapDataFilename", "beatmapFilename");
                    if(file)
                        return SafeDifficultyPath(levelDirectory, *file);
                }
            }
            return std::nullopt;
        }

        const rapidjson::Value* EnvironmentArray(
            const rapidjson::Value& document)
        {
            if(const auto* custom = Member(document, "customData", "_customData"))
            {
                if(const auto* environment = Member(
                       *custom, "environment", "_environment");
                   environment && environment->IsArray())
                    return environment;
            }
            if(const auto* environment = Member(
                   document, "environment", "_environment");
               environment && environment->IsArray())
                return environment;
            return nullptr;
        }

        bool MatchesCinemaScreen(
            std::string_view id,
            std::string_view lookupMethod)
        {
            const std::string candidate = "CinemaScreen";
            std::string method(lookupMethod);
            std::transform(
                method.begin(), method.end(), method.begin(),
                [](unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
            if(method == "contains")
                return candidate.find(id) != std::string::npos;
            if(method == "startswith")
                return candidate.starts_with(id);
            if(method == "endswith")
                return candidate.ends_with(id);
            if(method == "regex")
            {
                try
                {
                    const std::regex expression(
                        std::string(id),
                        std::regex_constants::ECMAScript |
                        std::regex_constants::optimize);
                    return std::regex_search(candidate, expression) ||
                           std::regex_search(
                               std::string("Environment/CinemaScreen"),
                               expression);
                }
                catch(const std::regex_error&)
                {
                    return false;
                }
            }
            return id == candidate ||
                   (id.size() > candidate.size() && id.ends_with(candidate));
        }

        bool ApplyCinemaScreenInstructions(
            const rapidjson::Document& document,
            MapVideoConfig& config,
            std::size_t& duplicateCount)
        {
            const auto* environment = EnvironmentArray(document);
            if(!environment)
                return false;

            constexpr std::size_t MaximumPreviewScreens = 32;
            bool matched = false;
            for(const auto& entry : environment->GetArray())
            {
                if(!entry.IsObject())
                    continue;
                const auto id = StringMember(entry, "id", "_id");
                if(!id)
                    continue;
                const auto lookup = StringMember(
                    entry, "lookupMethod", "_lookupMethod").value_or("Exact");
                if(!MatchesCinemaScreen(*id, lookup))
                    continue;

                matched = true;
                const auto position = VectorMember(entry, "position", "_position");
                const auto localPosition = VectorMember(
                    entry, "localPosition", "_localPosition");
                const auto rotation = VectorMember(entry, "rotation", "_rotation");
                const auto localRotation = VectorMember(
                    entry, "localRotation", "_localRotation");
                const auto scale = VectorMember(entry, "scale", "_scale");
                const auto active = BoolMember(entry, "active", "_active");
                const auto duplicates = IntMember(entry, "duplicate", "_duplicate");

                if(duplicates)
                {
                    // Chroma treats the presence of duplicate—even zero—as a
                    // clone instruction. It does not mutate the matched base.
                    const int count = std::max(0, *duplicates);
                    if(active && !*active)
                        continue;
                    for(int index = 0;
                        index < count &&
                        config.additionalScreens.size() < MaximumPreviewScreens;
                        ++index)
                    {
                        CinemaAdditionalScreen screen;
                        screen.position = position ? position : localPosition;
                        screen.rotation = rotation ? rotation : localRotation;
                        screen.scale = scale;
                        config.additionalScreens.push_back(std::move(screen));
                        ++duplicateCount;
                    }
                    continue;
                }

                // Without duplicate Chroma modifies the original CinemaScreen.
                // The menu screen has no parent, so local and world transforms
                // are equivalent for this compatibility bridge.
                if(position || localPosition)
                    config.screenPosition = position ? *position : *localPosition;
                if(rotation || localRotation)
                    config.screenRotation = rotation ? *rotation : *localRotation;
                if(scale)
                    config.screenScale = *scale;
                if(active && !*active)
                    config.screenScale = {0.0f, 0.0f, 0.0f};
            }
            if(matched)
            {
                config.hasMapperPresentation = true;
                config.hasMapperScreenGeometry = true;
            }
            return matched;
        }

        std::vector<std::filesystem::path> DifficultyCandidates(
            const std::filesystem::path& levelDirectory,
            std::string_view characteristic,
            int difficulty)
        {
            if(const auto selected = SelectedDifficultyPath(
                   levelDirectory, characteristic, difficulty))
                return {*selected};

            std::vector<std::filesystem::path> candidates;
            std::error_code error;
            for(std::filesystem::directory_iterator iterator(levelDirectory, error), end;
                !error && iterator != end;
                iterator.increment(error))
            {
                std::error_code entryError;
                if(!iterator->is_regular_file(entryError) || entryError)
                    continue;
                auto extension = iterator->path().extension().string();
                std::transform(
                    extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char value) {
                        return static_cast<char>(std::tolower(value));
                    });
                if(extension != ".dat" ||
                   EqualsIgnoreCase(iterator->path().filename().string(), "Info.dat"))
                    continue;
                candidates.push_back(iterator->path());
            }
            std::sort(candidates.begin(), candidates.end());
            return candidates;
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
                BigScreen::BigScreenLogger.warn(
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

    bool ChromaMapDetector::ApplyCinemaScreenPreview(
        const std::filesystem::path& levelDirectory,
        std::string_view characteristic,
        int difficulty,
        MapVideoConfig& config,
        std::string& reason)
    {
        reason.clear();
        const auto candidates = DifficultyCandidates(
            levelDirectory, characteristic, difficulty);
        for(const auto& path : candidates)
        {
            rapidjson::Document document;
            if(!ReadJson(path, document))
                continue;
            std::size_t duplicateCount = 0;
            if(!ApplyCinemaScreenInstructions(document, config, duplicateCount))
                continue;

            std::ostringstream detail;
            detail << "Chroma CinemaScreen preview instructions from "
                   << path.filename().string();
            if(duplicateCount > 0)
                detail << " (" << duplicateCount << " visible duplicate(s))";
            reason = detail.str();
            return true;
        }
        return false;
    }
}
