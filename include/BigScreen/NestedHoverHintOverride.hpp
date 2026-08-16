// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "HMUI/HoverHint.hpp"
#include "UnityEngine/EventSystems/IPointerEnterHandler.hpp"
#include "UnityEngine/EventSystems/IPointerExitHandler.hpp"
#include "UnityEngine/EventSystems/PointerEventData.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

/// A reset glyph may be parented inside a larger setting control so it can sit
/// beside that control without consuming another layout column. Unity sends
/// pointer-enter events to both the glyph and its parent, so the parent's
/// HoverHint otherwise replaces the glyph's more specific tooltip. This small
/// bridge temporarily gives the parent the glyph's text while the pointer is
/// over the nested button, then restores the parent's normal explanation.
DECLARE_CLASS_CODEGEN_INTERFACES(
    BigScreen,
    NestedHoverHintOverride,
    UnityEngine::MonoBehaviour,
    UnityEngine::EventSystems::IPointerEnterHandler*,
    UnityEngine::EventSystems::IPointerExitHandler*) {
    DECLARE_INSTANCE_FIELD(HMUI::HoverHint*, parentHint);
    DECLARE_INSTANCE_FIELD(StringW, parentText);
    DECLARE_INSTANCE_FIELD(StringW, nestedText);

    DECLARE_INSTANCE_METHOD(
        void,
        Configure,
        HMUI::HoverHint* parentHint,
        StringW parentText,
        StringW nestedText);

    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        OnPointerEnter,
        &UnityEngine::EventSystems::IPointerEnterHandler::OnPointerEnter,
        UnityEngine::EventSystems::PointerEventData* eventData);
    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        OnPointerExit,
        &UnityEngine::EventSystems::IPointerExitHandler::OnPointerExit,
        UnityEngine::EventSystems::PointerEventData* eventData);
};
