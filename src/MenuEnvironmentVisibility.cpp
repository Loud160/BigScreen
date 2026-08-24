// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/MenuEnvironmentVisibility.hpp"

#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/MenuFlowCoordinator.hpp"
#include "BigScreen/Settings.hpp"
#include "GlobalNamespace/BloomPrePassLight.hpp"
#include "GlobalNamespace/DirectionalLight.hpp"
#include "GlobalNamespace/LightWithIdMonoBehaviour.hpp"
#include "GlobalNamespace/LineLight.hpp"
#include "GlobalNamespace/PointLight.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Light.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Transform.hpp"
#include "main.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace BigScreen {
    namespace {
        bool IsOwnedByEnvironment(
            UnityEngine::Transform* transform,
            UnityEngine::Transform* environmentRoot)
        {
            for(int depth = 0; transform && depth < 64; ++depth)
            {
                if(transform == environmentRoot)
                    return true;
                transform = transform->get_parent();
            }
            return false;
        }

        bool HasBigScreenAncestor(UnityEngine::Transform* transform)
        {
            for(int depth = 0; transform && depth < 64; ++depth)
            {
                const std::string name(
                    transform->get_gameObject()->get_name());
                if(name.find("Big Screen") != std::string::npos)
                    return true;
                transform = transform->get_parent();
            }
            return false;
        }

        bool HasPointerOrControllerAncestor(UnityEngine::Transform* transform)
        {
            for(int depth = 0; transform && depth < 64; ++depth)
            {
                std::string name(transform->get_gameObject()->get_name());
                std::transform(
                    name.begin(), name.end(), name.begin(),
                    [](unsigned char value)
                    {
                        return static_cast<char>(std::tolower(value));
                    });
                if(name.find("controller") != std::string::npos ||
                   name.find("pointer") != std::string::npos ||
                   name.find("cursor") != std::string::npos ||
                   name.find("ray") != std::string::npos)
                {
                    return true;
                }
                transform = transform->get_parent();
            }
            return false;
        }

        UnityW<UnityEngine::Transform> ResolveMenuEnvironmentRoot()
        {
            // Older Beat Saber releases exposed this direct hierarchy path.
            // Keep it as the cheapest and most precise lookup when available.
            auto environment = UnityEngine::GameObject::Find("/Environment");
            if(!environment)
                environment = UnityEngine::GameObject::Find("Environment");
            if(environment && environment->get_transform())
                return environment->get_transform();

            // Beat Saber 1.40.8 no longer exposes a root with that name, but
            // BasicMenuGround remains part of the actual menu-environment
            // hierarchy. Resolve its top-level scene object instead of using
            // a brittle hard-coded replacement name. UI panels use their own
            // hierarchy and Big Screen's world surfaces are excluded below.
            for(auto* renderer : UnityEngine::Object::FindObjectsOfType<
                    UnityEngine::Renderer*>(true))
            {
                if(!renderer || !renderer->get_gameObject() ||
                   std::string(renderer->get_gameObject()->get_name()) !=
                       "BasicMenuGround")
                {
                    continue;
                }

                auto root = renderer->get_transform();
                while(root && root->get_parent())
                    root = root->get_parent();
                if(root)
                {
                    PaperLogger.info(
                        "Resolved Beat Saber 1.40.8 menu environment through BasicMenuGround root '{}'",
                        std::string(root->get_gameObject()->get_name()));
                    return root;
                }
            }
            return nullptr;
        }

        template<class T>
        void CacheEnvironmentLights(
            UnityEngine::Transform* environmentRoot,
            std::vector<UnityW<UnityEngine::Behaviour>>& known)
        {
            for(auto* component : UnityEngine::Object::FindObjectsOfType<T*>(true))
            {
                if(!component || !component->get_gameObject() ||
                   !IsOwnedByEnvironment(
                        component->get_transform(), environmentRoot))
                    continue;
                known.emplace_back(component);
            }
        }
    }

    MenuEnvironmentVisibility& MenuEnvironmentVisibility::Instance()
    {
        static MenuEnvironmentVisibility visibility;
        return visibility;
    }

    void MenuEnvironmentVisibility::PrewarmCache()
    {
        auto root = UnityW<UnityEngine::Transform>::isAlive(
                environmentRoot_.unsafePtr())
            ? environmentRoot_
            : ResolveMenuEnvironmentRoot();
        if(!root)
        {
            cacheInitialized_ = false;
            PaperLogger.warn(
                "Could not prewarm the menu-environment cache because no compatible hierarchy was found");
            return;
        }
        environmentRoot_ = root;

        const bool cachedSceneInvalid =
            !UnityW<UnityEngine::Transform>::isAlive(
                environmentRoot_.unsafePtr()) ||
            std::any_of(
                knownRenderers_.begin(), knownRenderers_.end(),
                [](UnityW<UnityEngine::Renderer> renderer)
                {
                    return !UnityW<UnityEngine::Renderer>::isAlive(
                        renderer.unsafePtr());
                }) ||
            std::any_of(
                knownLights_.begin(), knownLights_.end(),
                [](UnityW<UnityEngine::Behaviour> light)
                {
                    return !UnityW<UnityEngine::Behaviour>::isAlive(
                        light.unsafePtr());
                });
        if(cacheInitialized_ && !cachedSceneInvalid)
            return;

        knownRenderers_.clear();
        knownLights_.clear();
        for(auto* renderer : UnityEngine::Object::FindObjectsOfType<
                UnityEngine::Renderer*>(true))
        {
            if(!renderer || !renderer->get_gameObject() ||
               !IsOwnedByEnvironment(renderer->get_transform(), root) ||
               HasBigScreenAncestor(renderer->get_transform()) ||
               HasPointerOrControllerAncestor(renderer->get_transform()))
                continue;
            knownRenderers_.emplace_back(renderer);
        }
        CacheEnvironmentLights<UnityEngine::Light>(root, knownLights_);
        CacheEnvironmentLights<GlobalNamespace::BloomPrePassLight>(
            root, knownLights_);
        CacheEnvironmentLights<GlobalNamespace::LineLight>(
            root, knownLights_);
        CacheEnvironmentLights<GlobalNamespace::PointLight>(
            root, knownLights_);
        CacheEnvironmentLights<GlobalNamespace::DirectionalLight>(
            root, knownLights_);
        CacheEnvironmentLights<GlobalNamespace::LightWithIdMonoBehaviour>(
            root, knownLights_);
        cacheInitialized_ = true;
        PaperLogger.info(
            "Prewarmed {} menu-environment renderers and {} lighting components for this scene",
            knownRenderers_.size(),
            knownLights_.size());
    }

    void MenuEnvironmentVisibility::Apply()
    {
        const auto& settings = Settings::Instance();
        if(!settings.ModEnabled() || settings.ShowMenuEnvironment() ||
           !IsBigScreenMenuActive())
        {
            Restore();
            return;
        }
        HideVisualComponents();
    }

    void MenuEnvironmentVisibility::HideVisualComponents()
    {
        PrewarmCache();
        if(!cacheInitialized_ ||
           !UnityW<UnityEngine::Transform>::isAlive(
               environmentRoot_.unsafePtr()))
        {
            PaperLogger.warn(
                "Show Menu Environment is off, but no compatible menu-environment hierarchy was found");
            return;
        }

        hidden_ = true;
        int renderersDisabled = 0;
        for(auto renderer : knownRenderers_)
        {
            if(!UnityW<UnityEngine::Renderer>::isAlive(renderer.unsafePtr()) ||
               !renderer->get_enabled() ||
               !renderer->get_gameObject() ||
               !renderer->get_gameObject()->get_activeInHierarchy())
                continue;
            hiddenRenderers_.emplace_back(renderer);
            renderer->set_enabled(false);
            ++renderersDisabled;
        }

        int lightsDisabled = 0;
        for(auto light : knownLights_)
        {
            if(!UnityW<UnityEngine::Behaviour>::isAlive(light.unsafePtr()) ||
               !light->get_enabled() || !light->get_gameObject() ||
               !light->get_gameObject()->get_activeInHierarchy())
                continue;
            hiddenLights_.emplace_back(light);
            light->set_enabled(false);
            ++lightsDisabled;
        }

        PaperLogger.info(
            "Show Menu Environment off: disabled {} environment renderers and {} lighting components",
            renderersDisabled,
            lightsDisabled);
    }

    void MenuEnvironmentVisibility::Restore() noexcept
    {
        if(!hidden_ && hiddenRenderers_.empty() && hiddenLights_.empty())
            return;
        hidden_ = false;

        int renderersRestored = 0;
        for(auto renderer : hiddenRenderers_)
        {
            try
            {
                if(renderer)
                {
                    renderer->set_enabled(true);
                    ++renderersRestored;
                }
            }
            catch(...)
            {
                ErrorManager::Instance().RecordError(
                    "Restoring the menu environment",
                    "Beat Saber rejected one cached environment renderer");
            }
        }
        hiddenRenderers_.clear();

        int lightsRestored = 0;
        for(auto light : hiddenLights_)
        {
            try
            {
                if(light)
                {
                    light->set_enabled(true);
                    ++lightsRestored;
                }
            }
            catch(...)
            {
                ErrorManager::Instance().RecordError(
                    "Restoring the menu environment",
                    "Beat Saber rejected one cached environment light");
            }
        }
        hiddenLights_.clear();

        PaperLogger.info(
            "Restored {} menu-environment renderers and {} lighting components",
            renderersRestored,
            lightsRestored);
    }
}
