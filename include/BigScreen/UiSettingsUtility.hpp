// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "GlobalNamespace/PlayerData.hpp"
#include "GlobalNamespace/PlayerDataModel.hpp"
#include "GlobalNamespace/PlayerSensitivityFlag.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"
#include "bsml/shared/Helpers/getters.hpp"

namespace BigScreen::UiUtility {
    /// Mirrors a stored preference into a BSML toggle without invoking its
    /// change callback and writing the same setting a second time.
    inline void SetToggleWithoutNotification(
        BSML::ToggleSetting* setting,
        bool value)
    {
        if(!setting)
            return;
        setting->currentValue = value;
        if(setting->toggle)
            setting->toggle->SetIsOnWithoutNotify(value);
    }

    /// Forces a retained BSML switch graphic to redraw without firing its
    /// settings callback. When a hidden tab is reset, ToggleSetting and the
    /// underlying Unity Toggle can already contain the new bool while the
    /// animated visual still shows the old state. Passing through the inverse
    /// value guarantees Unity rebuilds the graphic before the desired value is
    /// restored.
    inline void RefreshToggleVisualWithoutNotification(
        BSML::ToggleSetting* setting,
        bool value)
    {
        if(!setting)
            return;
        setting->currentValue = !value;
        if(setting->toggle)
            setting->toggle->SetIsOnWithoutNotify(!value);
        SetToggleWithoutNotification(setting, value);
    }

    /// Uses Beat Saber's own parental-content preference for download checks,
    /// keeping every Big Screen download surface on the same policy.
    inline bool ExplicitContentAllowed()
    {
        auto* container = BSML::Helpers::GetDiContainer();
        auto* model = container
            ? container->Resolve<GlobalNamespace::PlayerDataModel*>()
            : nullptr;
        auto* data = model ? model->get_playerData() : nullptr;
        return data && data->get_desiredSensitivityFlag().value__ >=
            GlobalNamespace::PlayerSensitivityFlag::Explicit.value__;
    }
}
