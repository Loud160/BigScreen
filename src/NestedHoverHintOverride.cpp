// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/NestedHoverHintOverride.hpp"

DEFINE_TYPE(BigScreen, NestedHoverHintOverride);

namespace BigScreen {
    void NestedHoverHintOverride::Configure(
        HMUI::HoverHint* targetParentHint,
        StringW targetParentText,
        StringW targetNestedText)
    {
        parentHint = targetParentHint;
        parentText = targetParentText;
        nestedText = targetNestedText;
    }

    void NestedHoverHintOverride::OnPointerEnter(
        UnityEngine::EventSystems::PointerEventData*)
    {
        // The nested button receives pointer-enter before its ancestors. Set
        // the parent's text now so the parent's own HoverHint displays the
        // reset explanation when its event runs immediately afterward.
        if(parentHint)
            parentHint->set_text(nestedText);
    }

    void NestedHoverHintOverride::OnPointerExit(
        UnityEngine::EventSystems::PointerEventData*)
    {
        if(parentHint)
            parentHint->set_text(parentText);
    }
}
