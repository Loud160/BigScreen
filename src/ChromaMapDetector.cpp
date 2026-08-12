#include "BigScreen/ChromaMapDetector.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string_view>

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

        bool ContainsChromaDeclaration(const rapidjson::Value& value)
        {
            if(value.IsArray())
            {
                for(const auto& child : value.GetArray())
                {
                    if(ContainsChromaDeclaration(child))
                        return true;
                }
                return false;
            }
            if(!value.IsObject())
                return false;

            for(auto member = value.MemberBegin(); member != value.MemberEnd(); ++member)
            {
                const std::string_view key(
                    member->name.GetString(), member->name.GetStringLength());
                if(IsChromaDeclarationKey(key) &&
                   ArrayDeclaresChroma(member->value))
                    return true;
                if(ContainsChromaDeclaration(member->value))
                    return true;
            }
            return false;
        }

        bool ContainsEnvironmentArray(const rapidjson::Value& value)
        {
            if(value.IsArray())
            {
                for(const auto& child : value.GetArray())
                {
                    if(ContainsEnvironmentArray(child))
                        return true;
                }
                return false;
            }
            if(!value.IsObject())
                return false;

            for(auto member = value.MemberBegin(); member != value.MemberEnd(); ++member)
            {
                const std::string_view key(
                    member->name.GetString(), member->name.GetStringLength());
                // Chroma's v2 and v3 environment schemas both use an array of
                // scene-object instructions. Require a non-empty array so an
                // ordinary Info.dat environment-name string cannot match.
                if((EqualsIgnoreCase(key, "_environment") ||
                    EqualsIgnoreCase(key, "environment")) &&
                   member->value.IsArray() && !member->value.Empty())
                    return true;
                if(ContainsEnvironmentArray(member->value))
                    return true;
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
        for(std::filesystem::directory_iterator iterator(levelDirectory, iteratorError), end;
            !iteratorError && iterator != end;
            iterator.increment(iteratorError))
        {
            if(!iterator->is_regular_file())
                continue;
            auto extension = iterator->path().extension().string();
            std::transform(
                extension.begin(), extension.end(), extension.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if(extension != ".dat")
                continue;

            rapidjson::Document document;
            if(!ReadJson(iterator->path(), document))
                continue;
            if(ContainsChromaDeclaration(document))
            {
                reason = "Chroma requirement or suggestion in " +
                    iterator->path().filename().string();
                return true;
            }
            if(ContainsEnvironmentArray(document))
            {
                reason = "Chroma environment data in " +
                    iterator->path().filename().string();
                return true;
            }
        }
        return false;
    }
}
