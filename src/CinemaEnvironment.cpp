// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/CinemaEnvironment.hpp"

#include <string>
#include <vector>

#include "BigScreen/MapVideoConfig.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "main.hpp"

namespace BigScreen::CinemaEnvironment {
    namespace {
        bool ParentMatches(
            UnityEngine::Transform* transform,
            const std::optional<std::string>& expectedParent)
        {
            if(!expectedParent)
                return true;
            if(!transform)
                return false;
            auto parent = transform->get_parent();
            return parent && std::string(parent->get_name()) == *expectedParent;
        }

        std::vector<UnityEngine::GameObject*> FindExact(
            const std::string& name,
            const std::optional<std::string>& parent)
        {
            std::vector<UnityEngine::GameObject*> matches;
            // includeInactive is required because Cinema permits a mapper to
            // reactivate environment objects hidden by the stock environment.
            for(auto* transform : UnityEngine::Object::FindObjectsOfType<
                    UnityEngine::Transform*>(true))
            {
                if(!transform || std::string(transform->get_name()) != name ||
                   !ParentMatches(transform, parent))
                {
                    continue;
                }
                auto object = transform->get_gameObject();
                if(object)
                    matches.push_back(object);
            }
            return matches;
        }

        UnityEngine::Vector3 ToUnity(const Float3& value)
        {
            return {value.x, value.y, value.z};
        }

        void ApplyTransform(
            UnityEngine::GameObject* object,
            const EnvironmentModification& modification)
        {
            if(!object)
                return;
            auto transform = object->get_transform();
            if(transform)
            {
                if(modification.position)
                    transform->set_position(ToUnity(*modification.position));
                if(modification.rotation)
                    transform->set_eulerAngles(ToUnity(*modification.rotation));
                if(modification.scale)
                    transform->set_localScale(ToUnity(*modification.scale));
            }
            // Apply active last. This lets an object that begins inactive be
            // positioned before OnEnable observers see its mapper placement.
            if(modification.active)
                object->SetActive(*modification.active);
        }
    }

    void Apply(const MapVideoConfig& config)
    {
        int changed = 0;
        int cloned = 0;
        int missing = 0;
        for(const auto& modification : config.environmentModifications)
        {
            std::vector<UnityEngine::GameObject*> targets;
            if(modification.cloneFrom)
            {
                const auto sources = FindExact(
                    *modification.cloneFrom, modification.parentName);
                if(!sources.empty())
                {
                    auto* source = sources.back();
                    auto clone = UnityEngine::Object::Instantiate(
                        source,
                        source->get_transform()->get_parent(),
                        true);
                    if(clone)
                    {
                        clone->set_name(modification.name + " (Clone)");
                        targets.push_back(clone);
                        ++cloned;
                    }
                }
            }
            else
            {
                targets = FindExact(modification.name, modification.parentName);
            }

            if(targets.empty())
            {
                ++missing;
                PaperLogger.warn(
                    "Cinema environment object '{}' was not found in the loaded scene",
                    modification.cloneFrom.value_or(modification.name));
                continue;
            }
            for(auto* target : targets)
            {
                ApplyTransform(target, modification);
                ++changed;
            }
        }
        PaperLogger.info(
            "Applied mapper Cinema environment presentation to {} objects ({} cloned, {} entries unmatched)",
            changed,
            cloned,
            missing);
    }
}
