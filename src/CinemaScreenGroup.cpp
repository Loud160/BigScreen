// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/CinemaScreenGroup.hpp"

#include <exception>
#include <string>

#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Vector3.hpp"
#include "main.hpp"

namespace BigScreen {
    bool CinemaScreenGroup::Create(
        const MapVideoConfig& primary,
        int videoWidth,
        int videoHeight,
        UnityEngine::Texture* sharedTexture)
    {
        try
        {
            Destroy();
            if(!sharedTexture || primary.additionalScreens.empty())
                return true;

            screens_.reserve(primary.additionalScreens.size());
            for(std::size_t index = 0;
                index < primary.additionalScreens.size(); ++index)
            {
                const auto& authored = primary.additionalScreens[index];
                MapVideoConfig config = primary;
                config.additionalScreens.clear();
                if(authored.position)
                    config.screenPosition = *authored.position;
                if(authored.rotation)
                    config.screenRotation = *authored.rotation;

                auto surface = std::make_unique<ScreenSurface>();
                // Match PC Cinema's clone names exactly. Existing Chroma
                // regexes and mapper documentation can therefore address the
                // same hierarchy on Quest without a platform-specific map.
                const std::string name =
                    "CinemaScreen (" + std::to_string(index) + ")";
                if(!surface->CreateShared(
                       config,
                       videoWidth,
                       videoHeight,
                       sharedTexture,
                       name.c_str()))
                {
                    Destroy();
                    return false;
                }

                // Cinema applies scale to the cloned screen transform after
                // creating it. A missing scale therefore inherits Vector3.one
                // rather than changing the primary screen's authored size.
                if(authored.scale)
                {
                    surface->SetWorldScale({
                        authored.scale->x,
                        authored.scale->y,
                        authored.scale->z});
                }
                surface->SetVisible(false);
                screens_.push_back(std::move(surface));
            }
            PaperLogger.info(
                "Created {} Cinema additional video screen(s) sharing one decoded texture",
                screens_.size());
            return true;
        }
        catch(const std::exception& exception)
        {
            PaperLogger.error(
                "Could not create Cinema additional screens: {}",
                exception.what());
            try { Destroy(); } catch(...) {}
            return false;
        }
        catch(...)
        {
            PaperLogger.error(
                "Could not create Cinema additional screens because of an unknown native exception");
            try { Destroy(); } catch(...) {}
            return false;
        }
    }

    void CinemaScreenGroup::Destroy()
    {
        for(auto& screen : screens_)
        {
            if(screen)
                screen->Destroy();
        }
        screens_.clear();
    }

    void CinemaScreenGroup::SetVisible(bool visible)
    {
        for(auto& screen : screens_)
            screen->SetVisible(visible);
    }

    void CinemaScreenGroup::ShowLeadIn(bool black)
    {
        for(auto& screen : screens_)
            screen->ShowLeadIn(black);
    }

    bool CinemaScreenGroup::SetOpacity(float opacity)
    {
        bool succeeded = true;
        for(auto& screen : screens_)
            succeeded = screen->SetOpacity(opacity) && succeeded;
        return succeeded;
    }
}
