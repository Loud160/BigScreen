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
#include "GlobalNamespace/LightWithIdManager.hpp"
#include "GlobalNamespace/LightWithIdMonoBehaviour.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "main.hpp"

namespace BigScreen::CinemaEnvironment {
    namespace {
        struct PreparedClone {
            std::size_t modificationIndex = 0;
            UnityW<UnityEngine::GameObject> object;
            UnityEngine::Vector3 sourcePosition{};
        };

        struct OriginalObjectState {
            UnityW<UnityEngine::GameObject> object;
            bool active = true;
            UnityEngine::Vector3 position{};
            UnityEngine::Vector3 rotation{};
            UnityEngine::Vector3 scale{1.0f, 1.0f, 1.0f};
        };

        std::vector<PreparedClone> preparedClones;
        std::vector<OriginalObjectState> originalStates;

        void RememberOriginal(UnityEngine::GameObject* object)
        {
            if(!object)
                return;
            for(const auto& state : originalStates)
            {
                if(state.object.ptr() == object)
                    return;
            }
            auto transform = object->get_transform();
            originalStates.push_back({
                object,
                object->get_activeSelf(),
                transform ? transform->get_position() : UnityEngine::Vector3{},
                transform ? transform->get_eulerAngles() : UnityEngine::Vector3{},
                transform ? transform->get_localScale() :
                    UnityEngine::Vector3{1.0f, 1.0f, 1.0f}});
        }

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

        void RegisterCloneLights(UnityEngine::GameObject* clone)
        {
            if(!clone)
                return;
            auto* manager = UnityEngine::Object::FindObjectOfType<
                GlobalNamespace::LightWithIdManager*>(true);
            if(!manager)
                return;

            for(auto* light : clone->GetComponentsInChildren<
                    GlobalNamespace::LightWithIdMonoBehaviour*>(true))
            {
                if(!light)
                    continue;
                // Instantiate copies the source's registered flag even though
                // the clone is not in LightWithIdManager's lists. Clear that
                // stale state before explicit registration.
                light->__SetIsUnRegistered();
                manager->RegisterLight(
                    light->i___GlobalNamespace__ILightWithId());
            }
        }
    }

    void Cleanup()
    {
        // A normal map unload destroys these objects anyway, but the bundled
        // compatibility cycle deliberately applies multiple Cinema phases in
        // one scene. Restore every non-clone object before the next phase so
        // transforms and active flags never accumulate across tests.
        for(auto& state : originalStates)
        {
            if(!UnityW<UnityEngine::GameObject>::isAlive(state.object))
                continue;
            if(auto transform = state.object->get_transform())
            {
                transform->set_position(state.position);
                transform->set_eulerAngles(state.rotation);
                transform->set_localScale(state.scale);
            }
            state.object->SetActive(state.active);
        }
        originalStates.clear();
        for(auto& clone : preparedClones)
        {
            if(UnityW<UnityEngine::GameObject>::isAlive(clone.object))
                UnityEngine::Object::Destroy(clone.object);
        }
        preparedClones.clear();
    }

    void Prepare(const MapVideoConfig& config)
    {
        Cleanup();
        for(std::size_t index = 0;
            index < config.environmentModifications.size(); ++index)
        {
            const auto& modification = config.environmentModifications[index];
            if(!modification.cloneFrom)
                continue;
            const auto sources = FindExact(
                *modification.cloneFrom, modification.parentName);
            if(sources.empty())
                continue;

            auto* source = sources.back();
            auto sourceTransform = source->get_transform();
            auto* clone = UnityEngine::Object::Instantiate(
                source,
                sourceTransform ? sourceTransform->get_parent() : nullptr,
                true);
            if(!clone)
                continue;

            const auto sourcePosition = sourceTransform
                ? sourceTransform->get_position()
                : UnityEngine::Vector3{};
            clone->set_name(modification.name + " (CinemaClone)");
            if(!config.mergePropGroups && clone->get_transform())
            {
                auto displaced = sourcePosition;
                displaced.z += 200.0f;
                clone->get_transform()->set_position(displaced);
            }
            RegisterCloneLights(clone);
            preparedClones.push_back({index, clone, sourcePosition});
        }
        if(!preparedClones.empty())
        {
            BigScreen::BigScreenLogger.info(
                "Prepared {} Cinema environment clone(s) before Chroma prop grouping ({})",
                preparedClones.size(),
                config.mergePropGroups ? "groups merged" : "groups isolated");
        }
    }

    void Apply(const MapVideoConfig& config)
    {
        int changed = 0;
        int cloned = 0;
        int missing = 0;
        for(std::size_t index = 0;
            index < config.environmentModifications.size(); ++index)
        {
            const auto& modification = config.environmentModifications[index];
            std::vector<UnityEngine::GameObject*> targets;
            if(modification.cloneFrom)
            {
                for(auto& prepared : preparedClones)
                {
                    if(prepared.modificationIndex == index &&
                       UnityW<UnityEngine::GameObject>::isAlive(prepared.object))
                    {
                        // If position was omitted, restore the source position
                        // used before the temporary Chroma grouping offset.
                        if(!modification.position && prepared.object->get_transform())
                            prepared.object->get_transform()->set_position(
                                prepared.sourcePosition);
                        targets.push_back(prepared.object);
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
                BigScreen::BigScreenLogger.warn(
                    "Cinema environment object '{}' was not found in the loaded scene",
                    modification.cloneFrom.value_or(modification.name));
                continue;
            }
            for(auto* target : targets)
            {
                bool isPreparedClone = false;
                for(const auto& prepared : preparedClones)
                {
                    if(prepared.object.ptr() == target)
                    {
                        isPreparedClone = true;
                        break;
                    }
                }
                if(!isPreparedClone)
                    RememberOriginal(target);
                ApplyTransform(target, modification);
                ++changed;
            }
        }
        BigScreen::BigScreenLogger.info(
            "Applied mapper Cinema environment presentation to {} objects ({} cloned, {} entries unmatched)",
            changed,
            cloned,
            missing);
    }
}
