// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "UnityEngine/Component.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"

namespace BigScreen::UiUtility {
    /// Returns the existing layout component or creates one for a newly-built
    /// BSML row. Individual menus retain their own sizing policy; only this
    /// null-safe component lookup is shared.
    inline UnityEngine::UI::LayoutElement* EnsureLayout(
        UnityEngine::Component* component)
    {
        if(!component)
            return nullptr;
        auto object = component->get_gameObject();
        if(!object)
            return nullptr;
        if(auto* layout =
               object->GetComponent<UnityEngine::UI::LayoutElement*>())
            return layout;
        return object->AddComponent<UnityEngine::UI::LayoutElement*>();
    }

}
