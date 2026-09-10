// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/AudioSyncMenu.hpp"
#include "BigScreen/AudioSyncLimits.hpp"
#include "BigScreen/AudioSyncService.hpp"
#include "BigScreen/AudioSyncUnityCapture.hpp"
#include "BigScreen/AudioSyncUnityPlayer.hpp"
#include "BigScreen/AudioSyncVisualization.hpp"
#include "BigScreen/DiagnosticSessionLogger.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/MenuModal.hpp"
#include "BigScreen/Settings.hpp"
#include "BigScreen/UiSettingsUtility.hpp"
#include "BigScreen/UiUtility.hpp"
#include "BigScreen/VideoLibraryMenu.hpp"
#include "BigScreen/WaveformScrubber.hpp"
#include "HMUI/ImageView.hpp"
#include "HMUI/TextSegmentedControl.hpp"
#include "HMUI/ViewController.hpp"
#include "System/IntPtr.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/AudioSettings.hpp"
#include "UnityEngine/Canvas.hpp"
#include "UnityEngine/CanvasGroup.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Rect.hpp"
#include "UnityEngine/RectOffset.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Sprite.hpp"
#include "UnityEngine/SystemInfo.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutRebuilder.hpp"
#include "UnityEngine/UI/RectMask2D.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Components/ExternalComponents.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/Components/ScrollView.hpp"
#include "bsml/shared/BSML/Components/Settings/DropdownListSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "fmt/format.h"
#include "main.hpp"
#include "rapidjson/document.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <fstream>

namespace BigScreen
{
namespace
{
using namespace AudioSync;
// Saber Stage's proven full-center tab scaffold uses a 116-unit content span.
// This is the width of the scrollable content INSIDE the center tab, not the
// 72-unit width used by one of Saber Stage's narrower nested layouts. Keeping
// this distinction here matters: treating the full center surface like a side
// menu wastes almost forty percent of its usable width and makes coarse slider
// dragging needlessly difficult in VR.
constexpr float FullWidth = 116.0f;
constexpr float HalfWidth = (FullWidth - 1.0f) * .5f;
// Two timing controls share one 116-unit row. Four units still leaves a clear
// visual break between the left and right label/control groups, while giving
// each stock slider enough width for its reset glyph, track, value, and both
// arrow buttons. The former eight-unit gap made the native slider chrome clip
// even though unused space remained in the middle of the center workspace.
constexpr float PairedSettingGap = 4.0f;
constexpr float PairedSettingWidth = (FullWidth - PairedSettingGap) * .5f;
constexpr float PairedSettingLabelFraction = .36f;
constexpr float TimingResetGlyphTextSize = 6.0f;
constexpr float TimingResetButtonSize = 7.0f;
constexpr float TimingResetGap = 1.5f;
constexpr float CenterPageInset = 5.0f;
constexpr float CenterScrollHorizontalInset = 3.0f;
constexpr float CenterScrollVerticalInset = 0.0f;
constexpr float CenterTabStripHeight = 10.0f;
constexpr float ModalProgressWidth = 48.0f;
constexpr float BodyTextSize = 3.0f;
constexpr float HeaderTextSize = 3.6f;
constexpr double ApplySaveProgressDelaySeconds = .25;
constexpr double ApplySaveProgressMinimumVisibleSeconds = 3.0;

void RequireUiParent(UnityEngine::Component* parent, const char* operation)
{
    // Passing a null Component into BSML-Lite reaches Component::get_transform
    // before BSML can throw a managed exception. On Quest that is a native
    // null dereference, which bypasses ErrorManager and terminates Beat Saber.
    // Convert a broken staged-parent invariant into a normal C++ exception so
    // the existing guard, persistent log, and circuit breaker remain in charge.
    if(!UnityW<UnityEngine::Component>::isAlive(parent))
        throw std::runtime_error(std::string("Audio Sync cannot ") + operation +
                                 " because its UI parent is unavailable");
}

void FitInsideParent(UnityEngine::RectTransform* rect, float left, float bottom,
                     float right, float top)
{
    if(!rect)
        return;
    rect->set_anchorMin({0.0f, 0.0f});
    rect->set_anchorMax({1.0f, 1.0f});
    rect->set_pivot({0.5f, 0.5f});
    rect->set_offsetMin({left, bottom});
    rect->set_offsetMax({-right, -top});
}

void Layout(UnityEngine::Component* component, float width, float height)
{
    RequireUiParent(component, "size a control");
    auto* element = UiUtility::EnsureLayout(component);
    element->set_preferredWidth(width);
    element->set_minWidth(width);
    element->set_preferredHeight(height);
    element->set_minHeight(height);
    element->set_flexibleWidth(0);
}

void NeutralizeContentSizeFitter(UnityEngine::Component* component)
{
    if(!component)
        return;
    if(auto* fitter = component->get_gameObject()
                          ->GetComponent<UnityEngine::UI::ContentSizeFitter*>())
    {
        fitter->set_horizontalFit(
            UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
        fitter->set_verticalFit(
            UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    }
}

UnityEngine::RectTransform* ResolveSettingRow(UnityEngine::Component* control)
{
    if(!control)
        return nullptr;
    // DropdownListSetting is attached to its selector child while the other
    // BSML setting components normally live on their outer row. Walk only to
    // the first transform whose parent owns layout, exactly as Saber Stage's
    // proven full-center menu does, so a compact dropdown cannot retain the
    // stock 90-unit prefab width behind a smaller visible selector.
    auto row = control->get_transform();
    while(row && row->get_parent())
    {
        auto parent = row->get_parent();
        if(parent->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>() ||
           parent->GetComponent<UnityEngine::UI::HorizontalLayoutGroup*>())
            break;
        row = parent;
    }
    return row ? row->get_gameObject()->GetComponent<UnityEngine::RectTransform*>() : nullptr;
}

void SetSettingVisualEnabled(UnityEngine::Component* control, bool enabled)
{
    auto* row = ResolveSettingRow(control);
    if(!row)
        return;
    auto* group = row->get_gameObject()->GetComponent<UnityEngine::CanvasGroup*>();
    if(!group)
        group = row->get_gameObject()->AddComponent<UnityEngine::CanvasGroup*>();
    // BSML settings do not all tint their title/value hierarchy when their
    // underlying Selectable is disabled. Dimming the complete setting row
    // gives every slider, toggle, and dropdown the same readable unavailable
    // state without placing a raycast blocker over its hover explanation.
    group->set_alpha(enabled ? 1.0f : 0.48f);
}

void FitRectToParentRegion(UnityEngine::RectTransform* rect, float left, float right,
                           float leftInset = 0.0f, float rightInset = 0.0f)
{
    if(!rect)
        return;
    rect->set_anchorMin({left, 0.0f});
    rect->set_anchorMax({right, 1.0f});
    rect->set_pivot({.5f, .5f});
    rect->set_offsetMin({leftInset, 0.0f});
    rect->set_offsetMax({-rightInset, 0.0f});
    const auto position = rect->get_localPosition();
    rect->set_localPosition({position.x, position.y, 0.0f});
}

void FitSettingRow(UnityEngine::Component* control, float width, float height = 8.0f)
{
    auto* row = ResolveSettingRow(control);
    if(!row)
        return;
    NeutralizeContentSizeFitter(row);
    Layout(row, width, height);
    if(auto* element = row->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>())
        element->set_minWidth(width);
}

void FitSliderSetting(BSML::SliderSetting* slider, float width,
                      float titleFraction = .42f, bool keepPairTogether = false,
                      float controlLeadingInset = 0.0f,
                      float titleFontSize = 2.8f)
{
    if(!slider)
        return;
    FitSettingRow(slider, width);
    auto* row = ResolveSettingRow(slider);
    if(!row)
        return;
    if(auto title = row->get_transform()->Find("Title"))
    {
        FitRectToParentRegion(
            title->get_gameObject()->GetComponent<UnityEngine::RectTransform*>(),
            0.0f, titleFraction, .3f, .3f);
        if(auto* text = title->GetComponent<TMPro::TextMeshProUGUI*>())
        {
            // A two-column timing row contains two independent label/control
            // pairs. Right-aligning only those labels puts each caption next
            // to its own slider; the larger spacing between the two setting
            // roots then reads as the group boundary instead of allowing the
            // right caption to look attached to the left slider.
            text->set_alignment(keepPairTogether
                                    ? TMPro::TextAlignmentOptions::MidlineRight
                                    : TMPro::TextAlignmentOptions::MidlineLeft);
            text->set_enableWordWrapping(false);
            text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
            text->set_fontSize(titleFontSize);
        }
    }
    if(slider->slider)
        FitRectToParentRegion(
            slider->slider->get_transform().cast<UnityEngine::RectTransform>(),
            titleFraction + controlLeadingInset / width, 1.0f, .3f, .3f);
}

void ConfigureTimingResetButton(UnityEngine::UI::Button* button,
                                BSML::SliderSetting* slider)
{
    if(!button || !slider || !slider->slider)
        return;
    BSML::Lite::SetButtonTextSize(button, TimingResetGlyphTextSize);
    if(auto* layout = button->get_gameObject()
                          ->GetComponent<UnityEngine::UI::LayoutElement*>())
    {
        layout->set_minWidth(TimingResetButtonSize);
        layout->set_preferredWidth(TimingResetButtonSize);
        layout->set_preferredHeight(TimingResetButtonSize);
        layout->set_flexibleWidth(0.0f);
    }

    // Match the reset glyph used by Big Screen's basic playback controls:
    // make it an overlay owned by the setting's value area rather than a new
    // child of the two-column layout. Number() reserves this exact span
    // between the caption and slider, preserving both row grouping and the
    // established reset-button appearance.
    auto control = slider->slider->get_transform()
                       .cast<UnityEngine::RectTransform>();
    auto reset = button->get_transform().cast<UnityEngine::RectTransform>();
    reset->SetParent(control, false);
    reset->set_anchorMin({0.0f, 0.5f});
    reset->set_anchorMax({0.0f, 0.5f});
    reset->set_pivot({1.0f, 0.5f});
    reset->set_anchoredPosition({-TimingResetGap, 0.0f});
    reset->set_sizeDelta({TimingResetButtonSize, TimingResetButtonSize});
    reset->SetAsLastSibling();
}

void FitBareSlider(BSML::SliderSetting* slider, float width)
{
    if(!slider)
        return;
    FitSettingRow(slider, width);
    if(auto* row = ResolveSettingRow(slider))
        if(auto title = row->get_transform()->Find("Title"))
            title->get_gameObject()->SetActive(false);
    if(slider->slider)
        FitRectToParentRegion(
            slider->slider->get_transform().cast<UnityEngine::RectTransform>(),
            0.0f, 1.0f, .25f, .25f);
}

void FitDropdownSetting(BSML::DropdownListSetting* dropdown, float width,
                        float titleFraction = .43f, bool keepPairTogether = false)
{
    if(!dropdown)
        return;
    FitSettingRow(dropdown, width);
    if(auto* row = ResolveSettingRow(dropdown))
    {
        // DropdownListSetting is cloned from Beat Saber's environment selector.
        // Unlike BSML's slider setting (whose caption is named "Title"), this
        // prefab stores its caption in the root-level "Label" object. Looking
        // for "Title" silently skipped every caption adjustment below: the
        // compact row width was applied, but TextMeshPro kept its stock wrapping
        // rules and rendered captions such as "Wave Zoom" on two lines. Keep
        // this dropdown-specific path tied to the prefab BSML 0.4.55 actually
        // creates so the visible caption and selector receive matching regions.
        if(auto title = row->get_transform()->Find("Label"))
        {
            FitRectToParentRegion(
                title->get_gameObject()->GetComponent<UnityEngine::RectTransform*>(),
                0.0f, titleFraction, .25f, .25f);
            if(auto* text = title->GetComponent<TMPro::TextMeshProUGUI*>())
            {
                text->set_alignment(keepPairTogether
                                        ? TMPro::TextAlignmentOptions::MidlineRight
                                        : TMPro::TextAlignmentOptions::MidlineLeft);
                text->set_enableWordWrapping(false);
                text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
                text->set_maxVisibleLines(1);
                text->set_fontSize(2.8f);
            }
        }
        FitRectToParentRegion(
            dropdown->get_transform().cast<UnityEngine::RectTransform>(),
            titleFraction, 1.0f, .25f, .25f);
    }
    if(dropdown->dropdown && dropdown->dropdown->__cordl_internal_get__text())
        dropdown->dropdown->__cordl_internal_get__text()->set_fontSize(2.8f);
}

void FitToggleSetting(BSML::ToggleSetting* toggle, float width)
{
    if(!toggle)
        return;
    FitSettingRow(toggle, width, 7.0f);
    auto* row = ResolveSettingRow(toggle);
    if(!row)
        return;
    constexpr float switchWidth = 10.0f;
    if(auto name = row->get_transform()->Find("NameText"))
    {
        auto rect = name.cast<UnityEngine::RectTransform>();
        rect->set_anchorMin({0.0f, 0.0f});
        rect->set_anchorMax({1.0f, 1.0f});
        rect->set_pivot({.5f, .5f});
        rect->set_offsetMin({.5f, 0.0f});
        rect->set_offsetMax({-(switchWidth + 1.0f), 0.0f});
    }
    if(toggle->text)
    {
        toggle->text->set_alignment(TMPro::TextAlignmentOptions::MidlineRight);
        toggle->text->set_enableWordWrapping(false);
        toggle->text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        toggle->text->set_fontSize(2.8f);
    }
    if(auto switchView = row->get_transform()->Find("SwitchView"))
        if(auto* rect = switchView->get_gameObject()
                            ->GetComponent<UnityEngine::RectTransform*>())
        {
            rect->set_anchorMin({1.0f, .5f});
            rect->set_anchorMax({1.0f, .5f});
            rect->set_pivot({1.0f, .5f});
            rect->set_anchoredPosition({-.5f, 0.0f});
            const auto size = rect->get_sizeDelta();
            rect->set_sizeDelta({switchWidth, size.y});
        }
}
UnityEngine::UI::HorizontalLayoutGroup* Row(UnityEngine::Component* parent,
                                             float height = 8)
{
    RequireUiParent(parent, "create a row");
    auto* row = BSML::Lite::CreateHorizontalLayoutGroup(parent);
    row->set_spacing(1);
    row->set_childForceExpandHeight(false);
    row->set_childForceExpandWidth(false);
    row->set_childControlHeight(true);
    row->set_childControlWidth(true);
    row->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
    Layout(row, FullWidth, height);
    return row;
}
TMPro::TextMeshProUGUI* Text(UnityEngine::Component* parent, const std::string& value,
                             float height = 8, float width = FullWidth)
{
    RequireUiParent(parent, "create text");
    auto* text = BSML::Lite::CreateText(parent, value, BodyTextSize);
    Layout(text, width, height);
    text->set_enableWordWrapping(true);
    text->set_alignment(TMPro::TextAlignmentOptions::Center);
    text->set_overflowMode(TMPro::TextOverflowModes::Overflow);
    return text;
}
TMPro::TextMeshProUGUI* Header(UnityEngine::Component* parent, const std::string& value,
                               float width = FullWidth)
{
    // Match Big Screen's established side-menu section headings: blue bold
    // text, left aligned with a small inset, and one consistent font size.
    // A narrower layout width supplies the inset without absolute positioning
    // or a transparent spacer that could intercept the pointer.
    RequireUiParent(parent, "create a section header");
    auto* header = BSML::Lite::CreateText(parent, value, HeaderTextSize);
    Layout(header, width - 4.0f, 6.0f);
    header->set_fontStyle(TMPro::FontStyles::Bold);
    header->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
    header->set_color({0.35f, 0.85f, 1.0f, 1.0f});
    header->set_enableWordWrapping(false);
    return header;
}
void StyleBlueButton(UnityEngine::UI::Button* button)
{
    if(!button)
        return;
    // PracticeButton's inherited tint can look disabled against the dark
    // center workspace. Match the existing Video Library transport control so
    // every enabled action has the same unmistakable blue resting state.
    auto colors = button->get_colors();
    colors.set_normalColor({0.05f, 0.55f, 0.90f, 1.0f});
    colors.set_highlightedColor({0.20f, 0.75f, 1.0f, 1.0f});
    colors.set_pressedColor({0.03f, 0.35f, 0.65f, 1.0f});
    colors.set_selectedColor({0.10f, 0.65f, 0.95f, 1.0f});
    button->set_transition(UnityEngine::UI::Selectable::Transition::ColorTint);
    button->set_colors(colors);
    if(auto target = button->get_targetGraphic())
        target->set_color(UnityEngine::Color::get_white());
}
UnityEngine::UI::Button* Button(UnityEngine::Component* parent, const std::string& name,
                                 std::function<void()> action, float width = FullWidth)
{
    RequireUiParent(parent, "create a button");
    auto* button = BSML::Lite::CreateUIButton(
        parent, name, UnityEngine::Vector2{0, 0}, UnityEngine::Vector2{width, 7},
        [action = std::move(action)]
        { ErrorManager::Instance().Guard("Audio Sync control", action); });
    NeutralizeContentSizeFitter(button);
    Layout(button, width, 7);
    BSML::Lite::SetButtonTextSize(button, 3);
    StyleBlueButton(button);
    return button;
}
UnityEngine::UI::Button* DialogButton(UnityEngine::Component* parent,
                                      const std::string& name,
                                      std::function<void()> action,
                                      float width = 24.0f)
{
    // Modal callbacks are native Unity event boundaries too. Use the same
    // circuit-breaker guard as normal controls, including Save/Discard and
    // cancellation; never let a scene-invalidated object escape the callback.
    RequireUiParent(parent, "create a dialog button");
    auto* button = BSML::Lite::CreateUIButton(
        parent, name, UnityEngine::Vector2{0, 0}, UnityEngine::Vector2{width, 7},
        [action = std::move(action)] {
            ErrorManager::Instance().Guard("Audio Sync dialog", action);
        });
    NeutralizeContentSizeFitter(button);
    Layout(button, width, 7);
    BSML::Lite::SetButtonTextSize(button, 3);
    StyleBlueButton(button);
    return button;
}
UnityEngine::UI::VerticalLayoutGroup* ModalColumn(BSML::ModalView* modal,
                                                   float inset = 4.0f)
{
    RequireUiParent(modal, "create dialog content");
    auto* column = BSML::Lite::CreateVerticalLayoutGroup(modal);
    column->set_spacing(1.5f);
    column->set_childForceExpandHeight(false);
    column->set_childForceExpandWidth(false);
    column->set_childControlHeight(true);
    column->set_childControlWidth(true);
    column->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
    FitInsideParent(column->get_transform().cast<UnityEngine::RectTransform>(),
                    inset, inset, inset, inset);
    return column;
}
HMUI::ImageView* ModalProgressTrack(UnityEngine::Component* parent,
                                    HMUI::ImageView*& fill)
{
    RequireUiParent(parent, "create a progress track");
    auto* track = BSML::Lite::CreateImage(
        parent, BSML::Utilities::ImageResources::GetBlankSprite());
    Layout(track, ModalProgressWidth, 2.5f);
    track->set_color({.08f, .1f, .13f, .95f});
    track->set_preserveAspect(false);
    track->set_raycastTarget(false);
    fill = BSML::Lite::CreateImage(
        track->get_transform(), BSML::Utilities::ImageResources::GetBlankSprite());
    fill->set_color({.1f, .75f, 1, 1});
    fill->set_preserveAspect(false);
    fill->set_raycastTarget(false);
    if(auto rect = fill->get_transform().cast<UnityEngine::RectTransform>())
    {
        rect->set_anchorMin({0, .5f});
        rect->set_anchorMax({0, .5f});
        rect->set_pivot({0, .5f});
        rect->set_anchoredPosition({0, 0});
        rect->set_sizeDelta({ModalProgressWidth * .2f, 2.5f});
    }
    return track;
}
Source SongFile(const std::filesystem::path& directory, std::string& error)
{
    // SongCore supplies the installed map directory; reading its small
    // Info document happens on our worker, not in the catalog/UI path.
    auto path = directory / "Info.dat";
    std::error_code ec;
    if(!std::filesystem::is_regular_file(path, ec))
        path = directory / "info.dat";
    const auto size = std::filesystem::file_size(path, ec);
    if(ec || size > 1024 * 1024)
    {
        error = "Cannot read this map's audio filename.";
        return {};
    }
    std::ifstream stream(path, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(stream)), {});
    rapidjson::Document document;
    document.Parse(data.c_str());
    if(document.HasParseError() || !document.IsObject())
    {
        error = "The map's Info.dat cannot be parsed for audio.";
        return {};
    }
    const rapidjson::Value* value = nullptr;
    if(document.HasMember("_songFilename"))
        value = &document["_songFilename"];
    else if(document.HasMember("audio") && document["audio"].IsObject() &&
            document["audio"].HasMember("songFilename"))
        value = &document["audio"]["songFilename"];
    if(!value || !value->IsString())
    {
        error = "The map does not identify its song audio file.";
        return {};
    }
    const std::filesystem::path relative(value->GetString());
    if(relative.empty() || relative.is_absolute())
    {
        error = "The map's audio filename is unsafe.";
        return {};
    }
    for(const auto& part : relative)
        if(part == "..")
        {
            error = "The map's audio filename leaves its folder.";
            return {};
        }
    const auto audio = directory / relative;
    return {audio, SourceFingerprint(audio), 0};
}
Profile Baseline(const VideoDescriptor& descriptor, Bounds bounds)
{
    Timing timing;
    if(descriptor.mapperDefinition)
        timing = {descriptor.mapperDefinition->offsetSeconds,
                  descriptor.mapperDefinition->playbackRate};
    return InitialProfile(timing, bounds);
}
} // namespace
struct AudioSyncMenu::Impl
{
    HMUI::ViewController* center = nullptr;
    HMUI::ViewController* editor = nullptr;
    std::function<void(bool)> navigate;
    Draft draft;
    VideoDescriptor descriptor;
    Bounds bounds;
    std::uint64_t generation = 0;
    std::string operationKey;
    bool refreshing = false, closingAfterSave = false, enableOnly = false,
         outputStarted = false;
    bool preparationHandled = true, analysisHandled = true, persistenceHandled = true;
    bool saveWhenReady = false;
    double auditionSpeed = 1, position = 0;
    unsigned paintDivider = 0;
    Preparation preparation;
    Analysis analysis;
    Operation<Record> persistence;
    std::shared_ptr<const PreparedPair> pair;
    // A second bounded audition worker is reserved for live seeking. The
    // current audio continues playing while the replacement fills its startup
    // buffer, then Unity swaps sources in one UI tick. This prevents dragging
    // either timeline from producing the quarter-second silence that occurred
    // when the only worker was stopped before its replacement was ready.
    std::array<std::unique_ptr<Audition>, 2> auditions{
        std::make_unique<Audition>(), std::make_unique<Audition>()};
    int activeAudition = 0;
    std::optional<double> pendingSeek;
    std::optional<double> preparedSeek;
    std::chrono::steady_clock::time_point seekChanged = std::chrono::steady_clock::now();
    UnityPlayer player;
    UnityCapture capture;
    bool capturing = false;
    std::shared_ptr<const MatchResult> proposal;
    UnityEngine::GameObject* autoPage = nullptr;
    UnityEngine::GameObject* autoAnalysisSettingsRow = nullptr;
    UnityEngine::GameObject* manualPage = nullptr;
    TMPro::TextMeshProUGUI* status = nullptr;
    TMPro::TextMeshProUGUI* timing = nullptr;
    UnityEngine::UI::Button* transport = nullptr;
    BSML::SliderSetting* timeline = nullptr;
    BSML::ModalView* progress = nullptr;
    BSML::ModalView* confirm = nullptr;
    BSML::ModalView* analysisResult = nullptr;
    TMPro::TextMeshProUGUI* analysisResultText = nullptr;
    UnityEngine::UI::Button* analysisResultApply = nullptr;
    BSML::ModalView* leaveConfirm = nullptr;
    std::function<void()> pendingNavigation;
    BSML::ModalView* enableProgress = nullptr;
    TMPro::TextMeshProUGUI* enableProgressText = nullptr;
    HMUI::ImageView* enableProgressFill = nullptr;
    BSML::ModalView* enableError = nullptr;
    TMPro::TextMeshProUGUI* enableErrorText = nullptr;
    TMPro::TextMeshProUGUI* progressText = nullptr;
    HMUI::ImageView* progressFill = nullptr;
    UnityEngine::UI::Button* progressCancel = nullptr;
    UnityEngine::UI::Button* apply = nullptr;
    UnityEngine::UI::Button* resetTrack = nullptr;
    UnityEngine::UI::Button* analyze = nullptr;
    std::vector<std::function<void()>> refreshControls;
    std::deque<std::function<void()>> uiSteps;
    BSML::ScrollView* uiScroll = nullptr;
    UnityEngine::UI::VerticalLayoutGroup* uiRoot = nullptr;
    UnityEngine::UI::VerticalLayoutGroup* uiContent = nullptr;
    UnityEngine::UI::VerticalLayoutGroup* uiAuto = nullptr;
    UnityEngine::UI::VerticalLayoutGroup* uiManual = nullptr;
    UnityEngine::UI::VerticalLayoutGroup* overviewRoot = nullptr;
    std::array<UnityEngine::UI::HorizontalLayoutGroup*, 2> overviewRows{};
    std::array<UnityEngine::UI::VerticalLayoutGroup*, 2> overviewGroups{};
    std::array<TMPro::TextMeshProUGUI*, 2> overviewLabels{};
    std::array<HMUI::ImageView*, 2> overviewViewports{};
    std::array<HMUI::ImageView*, 2> overviews{};
    std::array<WaveformScrubber*, 2> overviewScrubbers{};
    // [source][0=whole-track filled envelope, 1=zoom-detail outline]. Unity
    // objects are uploaded lazily one per UI tick; native masks were already
    // prepared on the worker.
    std::array<std::array<UnityW<UnityEngine::Texture2D>, 2>, 2> overviewTextures{};
    std::array<std::array<UnityW<UnityEngine::Sprite>, 2>, 2> overviewSprites{};
    std::array<std::array<HMUI::ImageView*, 3>, 2> overviewLines{};
    UnityEngine::GameObject* manualDirectTimingRow = nullptr;
    std::array<UnityEngine::GameObject*, 2> manualMarkerRows{};
    UnityEngine::GameObject* manualMarkerOptionsRow = nullptr;
    TMPro::TextMeshProUGUI* manualMethodHelp = nullptr;
    int visibleMode = -1;
    int visibleTimingMethod = -1;
    // Dynamic Wave Height changes can increase this page by more than one
    // viewport. BSML's cloned EULA scroll container normally notices a child
    // dimension change on a later frame, but nested layout groups do not
    // reliably propagate 4x/8x changes that far on Quest. Two explicit layout
    // passes update the graph first and the enclosing scroll range second.
    int pendingScrollLayoutPasses = 0;
    unsigned uploadedOverviews = 0;
    bool progressShown = false;
    // Apply usually persists one small JSON record in far less than a frame.
    // Showing a modal immediately made that normal fast path look like a UI
    // flash. The delayed state is used only by the explicit Apply Changes
    // action: saves below 250 ms remain silent, while a save that crosses the
    // threshold gets a stable message for at least three seconds. The record
    // is still accepted as soon as the worker finishes; this timer controls
    // only how long the already-visible confirmation remains on screen.
    bool delayedApplySaveProgress = false;
    std::optional<std::chrono::steady_clock::time_point> applySaveProgressShownAt;
    // Mirrors of the candidate profile's durable per-map editor view. Rendering
    // reads these compact integers frequently; every user change first passes
    // through Draft::Edit so Apply/Discard and reopening remain coherent.
    // 0 = side by side, 1 = stacked, 2 = overlay.
    int overviewLayout = 0;
    int overviewZoom = 0;
    // 0=Default, 1=2x, 2=4x, 3=8x. This changes only the viewport's layout
    // height. The already-prepared Alpha8 waveform texture is stretched by
    // Unity, so increasing visual detail does not rebuild audio data or add
    // analyzer work on the UI thread.
    int overviewHeightScale = 0;
    float overviewGraphWidth = HalfWidth;
    float overviewGraphHeight = 18.0f;

    static const char* ConfidenceName(Confidence confidence)
    {
        switch(confidence)
        {
        case Confidence::High:
            return "High";
        case Confidence::Medium:
            return "Medium";
        case Confidence::Low:
            return "Low";
        }
        return "Low";
    }

    static std::string SummaryStatus(const AnalysisSummary& summary)
    {
        return fmt::format(
            "Last automatic match: {} confidence, {}/{} points\n"
            "Offset {:+.4f} s    Speed {:.4f}x",
            ConfidenceName(summary.confidence), summary.acceptedPoints,
            summary.requestedPoints, summary.timing.offsetSeconds,
            summary.timing.playbackRate);
    }

    std::string ResultMessage(const MatchResult& result) const
    {
        const auto& current = draft.Candidate().profile.timing;
        std::string message = fmt::format(
            "Match confidence: {}\nMatch points: {}/{} accepted ({} rejected)",
            ConfidenceName(result.confidence), result.accepted,
            draft.Candidate().profile.anchorCount, result.rejected);
        if(result.validationCount)
        {
            message += fmt::format("\nIndependent checks: {}", result.validationCount);
            if(std::isfinite(result.validationMaxSeconds))
                message += fmt::format("; maximum error {:.2f} ms",
                                       result.validationMaxSeconds * 1000.0);
            else
                message += "; alignment disagreement detected";
        }
        if(result.timing)
        {
            message += fmt::format(
                "\n\nVideo offset: {:+.4f} s (change {:+.4f} s)"
                "\nVideo speed: {:.4f}x (change {:+.3f}%)",
                result.timing->offsetSeconds,
                result.timing->offsetSeconds - current.offsetSeconds,
                result.timing->playbackRate,
                (result.timing->playbackRate / current.playbackRate - 1.0) * 100.0);
        }
        else
            message += "\n\nNo usable offset or playback speed was found.";
        if(!result.explanation.empty())
            message += "\n\n" + result.explanation;
        return message;
    }

    Audition& ActiveAudition() { return *auditions[activeAudition]; }
    const Audition& ActiveAudition() const { return *auditions[activeAudition]; }
    Audition& SpareAudition() { return *auditions[1 - activeAudition]; }

    double OverviewSongWindow() const
    {
        const double duration = std::max(.001, bounds.songDuration);
        if(overviewZoom <= 0 || duration <= 5.0)
            return duration;
        // The labels are progressive editor zoom levels. Interpolating their
        // effective scale makes 20x reliably end at a five-second window even
        // for a four-minute song, rather than allowing duration/20 to leave a
        // twelve-second view on long tracks. Intermediate even-numbered levels
        // remain smooth and monotonic, and the full-track option is exact.
        const double amount = std::clamp(overviewZoom / 20.0, 0.0, 1.0);
        const double factor = 1.0 + (duration / 5.0 - 1.0) * amount;
        return std::clamp(duration / factor, 5.0, duration);
    }

    void RefreshScrollableLayout()
    {
        if(!uiContent)
            return;

        // Rebuild from the innermost changing group outward. Merely changing
        // LayoutElement::preferredHeight leaves the cloned ScrollView holding
        // its previous content extent on Quest, which clips the center of 4x
        // and 8x waveforms and makes them appear blank. No waveform pixels are
        // regenerated here; this is a bounded Unity layout update performed
        // only after the user changes a view or timing mode.
        if(overviewRoot)
            UnityEngine::UI::LayoutRebuilder::ForceRebuildLayoutImmediate(
                overviewRoot->get_transform().cast<UnityEngine::RectTransform>());
        auto contentRect =
            uiContent->get_transform().cast<UnityEngine::RectTransform>();
        UnityEngine::UI::LayoutRebuilder::ForceRebuildLayoutImmediate(contentRect);
        UnityEngine::Canvas::ForceUpdateCanvases();
        if(uiScroll)
        {
            // SetContentSize writes the content RectTransform's actual height.
            // Reading that same height back here creates a feedback loop: after
            // one taller graph layout, later shorter layouts can retain the old
            // assigned extent and repeated switches appear to add blank space
            // above the waveforms. VerticalLayoutGroup::preferredHeight is the
            // freshly calculated sum of active children and is independent of
            // the previous ScrollView assignment, so every layout selection is
            // absolute and reversible.
            const float preferredHeight = uiContent->get_preferredHeight();
            if(std::isfinite(preferredHeight) && preferredHeight > 0.0f)
                uiScroll->SetContentSize(preferredHeight);
            uiScroll->RefreshButtons();
        }
        if(pendingScrollLayoutPasses > 0)
            --pendingScrollLayoutPasses;
    }

    void ScheduleScrollableLayoutRefresh()
    {
        // The first pass makes the modified graph/visibility hierarchy current.
        // The following Tick lets ContentSizeFitter publish that new height to
        // the outer scroll content before its range is measured again.
        pendingScrollLayoutPasses = 2;
    }

    void ApplyOverviewLayout()
    {
        if(!overviewRoot || !overviewRows[0] || !overviewRows[1])
            return;
        const bool stacked = overviewLayout == 1;
        const bool overlay = overviewLayout == 2;
        const float defaultHeight = stacked ? 14.0f : overlay ? 22.0f : 18.0f;
        const float heightMultiplier = static_cast<float>(1 << overviewHeightScale);
        overviewGraphHeight = defaultHeight * heightMultiplier;
        const float groupHeight = overviewGraphHeight + 4.0f;
        overviewGraphWidth = stacked || overlay ? FullWidth : HalfWidth;
        Layout(overviewRoot, FullWidth,
               stacked ? groupHeight * 2.0f + 1.0f : groupHeight);
        Layout(overviewRows[0], FullWidth, groupHeight);
        Layout(overviewRows[1], FullWidth, groupHeight);
        overviewRows[1]->get_gameObject()->SetActive(stacked);
        for(int i = 0; i < 2; ++i)
        {
            if(overviewGroups[i])
            {
                // Unity forbids HorizontalLayoutGroup and VerticalLayoutGroup
                // components on the same GameObject. Side-by-side and stacked
                // views therefore use two permanent legal rows, and only the
                // second graph changes parent. This is a normal Transform
                // reparent, not a runtime component swap, so changing the
                // dropdown cannot recreate the prewarm crash caused by the
                // earlier conflicting-component design.
                auto destination = overviewRows[stacked && i == 1 ? 1 : 0]->get_transform();
                if(overviewGroups[i]->get_transform()->get_parent() != destination)
                    overviewGroups[i]->get_transform()->SetParent(destination, false);
                Layout(overviewGroups[i], overviewGraphWidth, groupHeight);
                overviewGroups[i]->get_gameObject()->SetActive(!overlay || i == 0);
            }
            if(overviewLabels[i])
            {
                Layout(overviewLabels[i], overviewGraphWidth, 4.0f);
                overviewLabels[i]->set_text(
                    overlay && i == 0 ? "Map Audio (cyan) + Video Audio (magenta)"
                                      : i == 0 ? "Map Audio (cyan)"
                                               : "Video Audio (magenta)");
            }
            if(overviewViewports[i])
                Layout(overviewViewports[i], overviewGraphWidth, overviewGraphHeight);

            // Overlay mode shares one clipped viewport. Only the image and
            // its source-specific marker lines change parent; no layout
            // component is added or replaced, preserving the crash fix that
            // forbids two LayoutGroups on one object.
            auto* destinationViewport = overlay ? overviewViewports[0] : overviewViewports[i];
            if(destinationViewport && overviews[i] &&
               overviews[i]->get_transform()->get_parent() !=
                   destinationViewport->get_transform())
                overviews[i]->get_transform()->SetParent(
                    destinationViewport->get_transform(), false);
            if(overviews[i])
                overviews[i]->get_transform()->SetAsFirstSibling();
            for(auto* marker : overviewLines[i])
                if(marker)
                {
                    if(destinationViewport && marker->get_transform()->get_parent() !=
                                                  destinationViewport->get_transform())
                    {
                        marker->get_transform()->SetParent(
                            destinationViewport->get_transform(), false);
                    }
                    auto markerRect =
                        marker->get_transform().cast<UnityEngine::RectTransform>();
                    // CreateImage inherits anchor values from its template.
                    // Marker positions are calculated around the waveform's
                    // center, so leaving those inherited anchors in place can
                    // put the line outside the clipped viewport even when X=0.
                    // Stretch vertically with the viewport instead of copying
                    // a requested numeric height into a fixed child rect. The
                    // parent layout can settle at a slightly different actual
                    // height on Quest; using the parent's live height keeps
                    // the bar and waveform flush with the dark viewing area.
                    markerRect->set_anchorMin({.5f, 0.0f});
                    markerRect->set_anchorMax({.5f, 1.0f});
                    markerRect->set_pivot({.5f, .5f});
                    markerRect->set_sizeDelta({.65f, 0.0f});
                    marker->get_transform()->SetAsLastSibling();
                }
        }
        PaintOverviews();
        ScheduleScrollableLayoutRefresh();
    }

    void ClearOverviews()
    {
        for(int i = 0; i < 2; ++i)
        {
            if(overviews[i])
                overviews[i]->set_sprite(BSML::Utilities::ImageResources::GetBlankSprite());
            for(int level = 0; level < 2; ++level)
            {
                if(overviewSprites[i][level])
                    UnityEngine::Object::Destroy(overviewSprites[i][level]);
                if(overviewTextures[i][level])
                    UnityEngine::Object::Destroy(overviewTextures[i][level]);
                overviewSprites[i][level] = nullptr;
                overviewTextures[i][level] = nullptr;
            }
        }
        uploadedOverviews = 0;
    }
    void PaintOverviews()
    {
        if(!pair)
            return;
        const bool detailed = overviewZoom > 0;
        const int level = detailed ? 1 : 0;
        // Upload at most one required worker-generated mask per UI tick. A
        // full-track summary and zoom-detail outline intentionally remain
        // separate LODs: enlarging a dense summary can never reveal waveform
        // detail. Alpha8 preserves tinting while avoiding redundant RGB memory.
        for(int i = 0; i < 2; ++i)
        {
            const unsigned bit = 1u << (i * 2 + level);
            if(uploadedOverviews & bit)
                continue;
            const auto& overview = i == 0 ? pair->song.overview : pair->video.overview;
            const auto& pixels = detailed ? overview.detail : overview.summary;
            const int width = detailed ? overview.detailWidth : OverviewSummaryWidth;
            auto texture = UnityEngine::Texture2D::New_ctor(width, OverviewHeight,
                                                            UnityEngine::TextureFormat::Alpha8,
                                                            false, false);
            texture->LoadRawTextureData(
                System::IntPtr(const_cast<std::uint8_t*>(pixels.data())), pixels.size());
            texture->Apply(false, true);
            overviewTextures[i][level] = texture;
            overviewSprites[i][level] = UnityEngine::Sprite::Create(
                texture, UnityEngine::Rect(0, 0, width, OverviewHeight), {.5f, .5f},
                100);
            uploadedOverviews |= bit;
            break;
        }
        const auto& profile = draft.Candidate().profile;
        const bool zoomed = overviewZoom > 0 && OverviewSongWindow() < bounds.songDuration;
        const double songWindow = OverviewSongWindow();
        for(int i = 0; i < 2; ++i)
        {
            const auto duration = i == 0 ? bounds.songDuration : bounds.videoDuration;
            const auto current = i == 0 ? std::optional<double>{position}
                                        : VideoTime(profile.timing, position);
            // Both graphs cover the same amount of audible map time. The
            // video image therefore uses playbackRate times the map window so
            // peaks that should coincide remain visually comparable.
            const double visible = std::clamp(
                i == 0 ? songWindow : songWindow * profile.timing.playbackRate,
                .001, std::max(.001, duration));
            const float contentWidth =
                duration > 0 ? overviewGraphWidth * static_cast<float>(duration / visible)
                             : overviewGraphWidth;
            const float contentX = duration > 0 && current
                                       ? -static_cast<float>((*current / duration - .5) *
                                                            contentWidth)
                                       : 0.0f;
            if(overviews[i])
            {
                const unsigned bit = 1u << (i * 2 + level);
                if(uploadedOverviews & bit)
                    overviews[i]->set_sprite(overviewSprites[i][level].ptr());
                else
                    overviews[i]->set_sprite(
                        BSML::Utilities::ImageResources::GetBlankSprite());
                auto rect = overviews[i]->get_transform().cast<UnityEngine::RectTransform>();
                // Horizontal size/position represents the full track and its
                // current zoom window. Vertically, always stretch to the
                // viewport that Unity actually laid out. This prevents the
                // background from growing above a fixed-height waveform when
                // Wave Height or Graph Layout changes.
                rect->set_anchorMin({.5f, 0.0f});
                rect->set_anchorMax({.5f, 1.0f});
                rect->set_pivot({.5f, .5f});
                rect->set_anchoredPosition({zoomed ? contentX : 0.0f, 0.0f});
                rect->set_sizeDelta({zoomed ? contentWidth : overviewGraphWidth, 0.0f});
            }
            const std::array<double, 3> times =
                i == 0
                    ? std::array{position, profile.markers.songStart, profile.markers.songEnd}
                    : std::array{VideoTime(profile.timing, position).value_or(-1),
                                 profile.markers.videoStart, profile.markers.videoEnd};
            for(int line = 0; line < 3; ++line)
            {
                auto* image = overviewLines[i][line];
                float x = 0.0f;
                if(duration > 0)
                    x = zoomed
                            ? contentX + static_cast<float>((times[line] / duration - .5) *
                                                           contentWidth)
                            : static_cast<float>((times[line] / duration - .5) *
                                                 overviewGraphWidth);
                // At zoom, current playback is the stationary yellow bar and
                // the waveform moves underneath it. Start/end markers travel
                // with their source and are hidden once outside the mask.
                if(line == 0 && zoomed)
                    x = 0.0f;
                const bool visibleLine =
                    duration > 0 && times[line] >= 0 && times[line] <= duration;
                const bool inViewport = std::abs(x) <= overviewGraphWidth * .5f + .2f;
                if(visibleLine && inViewport)
                {
                    // Keep the complete line inside the mask at the exact
                    // beginning/end of an unzoomed track. A centered marker at
                    // +/- halfWidth otherwise loses half of its already-narrow
                    // visible area to clipping and is effectively invisible.
                    constexpr float MarkerHalfWidth = .325f;
                    x = std::clamp(x, -overviewGraphWidth * .5f + MarkerHalfWidth,
                                   overviewGraphWidth * .5f - MarkerHalfWidth);
                }
                image->get_gameObject()->SetActive(
                    visibleLine && inViewport && !(overviewLayout == 2 && i == 1 && line == 0));
                if(visibleLine && inViewport)
                    image->get_transform()
                        .cast<UnityEngine::RectTransform>()
                        ->set_anchoredPosition({x, 0});
            }
        }
    }

    bool Busy() const { return preparation.Busy() || analysis.Busy() || persistence.Busy(); }
    void ResetDelayedApplySaveProgress(bool hideModal)
    {
        if(!delayedApplySaveProgress)
            return;
        if(hideModal && progress && progressShown)
            progress->Hide();
        if(progressCancel)
            progressCancel->get_gameObject()->SetActive(true);
        delayedApplySaveProgress = false;
        applySaveProgressShownAt.reset();
        if(hideModal)
            progressShown = false;
    }
    void UpdateDelayedApplySaveProgress()
    {
        if(!delayedApplySaveProgress)
            return;
        const auto snapshot = persistence.Poll();
        if(!snapshot)
        {
            ResetDelayedApplySaveProgress(false);
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if(!applySaveProgressShownAt &&
           snapshot->wallSeconds >= ApplySaveProgressDelaySeconds &&
           snapshot->status != OperationStatus::Failed &&
           snapshot->status != OperationStatus::Cancelled)
        {
            ShowProgress();
            if(progressShown)
            {
                applySaveProgressShownAt = now;
                // Persistence cannot be rolled back reliably after its atomic
                // file replacement starts, so the preparation/analysis Cancel
                // action must not be presented during an Apply save.
                if(progressCancel)
                    progressCancel->get_gameObject()->SetActive(false);
            }
        }

        if(!applySaveProgressShownAt)
        {
            // A terminal save below the threshold deliberately never creates
            // the modal. Release the timing state after its result is handled.
            if(!persistence.Busy() && persistenceHandled)
                ResetDelayedApplySaveProgress(false);
            return;
        }

        if(progressText)
        {
            if(snapshot->status == OperationStatus::Succeeded)
                progressText->set_text("Synchronization settings saved.");
            else
                progressText->set_text(snapshot->stage + "\nWorking...");
        }
        if(progressFill && snapshot->status == OperationStatus::Succeeded)
        {
            auto rect = progressFill->get_transform().cast<UnityEngine::RectTransform>();
            rect->set_sizeDelta({ModalProgressWidth, 2});
            rect->set_anchoredPosition({0, 0});
        }

        const double visibleSeconds =
            std::chrono::duration<double>(now - *applySaveProgressShownAt).count();
        if(!persistence.Busy() && persistenceHandled &&
           visibleSeconds >= ApplySaveProgressMinimumVisibleSeconds)
            ResetDelayedApplySaveProgress(true);
    }
    void Error(const std::string& detail, bool internal = false)
    {
        if(progress)
            progress->Hide();
        if(enableProgress)
            enableProgress->Hide();
        progressShown = false;
        ResetDelayedApplySaveProgress(false);
        if(internal)
        {
            // Internal failures retain ErrorManager's circuit-breaker and
            // recovery behavior. ReportInternal already records the failure
            // and owns the eventual frontmost error dialog, so do not enqueue
            // the same failure a second time through ReportUserVisible.
            ErrorManager::Instance().ReportInternal("Audio Sync", detail);
        }
        else if(enableError && enableErrorText)
        {
            // All expected audio/media failures remain attached to the
            // right-hand video editor. That is where Advanced Sync is enabled
            // and Configure is pressed; routing preparation failures through
            // ErrorManager's shared prompt would move the popup to the left
            // settings menu even after the center workspace has opened.
            // Internal failures still take the circuit-breaker path above.
            BigScreenLogger.warn("Audio Sync operation failed: {}", detail);
            ErrorManager::Instance().RecordError("Audio Sync", detail);
            enableErrorText->set_text(detail);
            ShowModalInFront(enableError);
        }
        else
        {
            ErrorManager::Instance().ReportUserVisible(
                "Audio synchronization", detail);
        }
        if(status)
            status->set_text(detail);
    }
    void EditWithoutPlaybackChange(const std::function<void(Profile&)>& change)
    {
        ErrorManager::Instance().Guard(
            "Editing non-playback Audio Sync settings",
            [&]
            {
                if(refreshing || !draft.IsOpen() || persistence.Busy())
                    return;
                auto profile = draft.Candidate().profile;
                change(profile);
                std::string error;
                if(!draft.Edit(profile, bounds, error))
                {
                    if(status)
                        status->set_text(error);
                    Refresh();
                    return;
                }
                // Waveform presentation and analyzer effort are saved per map,
                // but neither changes timing, routing, pitch, or the video
                // surface. Keep these edits out of ApplyAudioSyncPreview and do
                // not rebuild a running audition for an unrelated UI choice.
                Refresh();
            });
    }
    void StartAnalysis()
    {
        if(Busy() || !pair || draft.Candidate().profile.mode != Mode::Automatic)
            return;
        if(analysisResult)
            analysisResult->Hide();
        proposal.reset();
        analysisHandled = false;
        const auto profile = draft.Candidate().profile;
        const auto prepared = pair;
        ++generation;
        // No draft offset/rate enters Analyze: switching from Manual cannot
        // bias an independent Automatic search toward the user's guess.
        analysis.Start(generation, operationKey,
                       [prepared, profile](const auto& context)
                       {
                           return Analyze(*prepared, profile.anchorCount,
                                          profile.windowSeconds, context);
                       });
        ShowProgress();
        Refresh();
    }
    void DiscardAnalysisResult()
    {
        if(analysisResult)
            analysisResult->Hide();
        proposal.reset();
        if(status)
        {
            const auto& saved = draft.Candidate().profile.lastAnalysis;
            status->set_text(saved ? SummaryStatus(*saved)
                                   : "Analysis discarded. Current timing was not changed.");
        }
        Refresh();
    }
    void ApplyAnalysisResult()
    {
        if(!proposal || !proposal->timing || proposal->incompatible || Busy())
            return;
        const auto& profile = draft.Candidate().profile;
        AnalysisSummary summary;
        summary.confidence = proposal->confidence;
        summary.timing = *proposal->timing;
        summary.requestedPoints = profile.anchorCount;
        summary.acceptedPoints = static_cast<int>(std::min<std::size_t>(proposal->accepted, 64));
        summary.rejectedPoints = static_cast<int>(std::min<std::size_t>(proposal->rejected, 64));
        summary.validationPoints =
            static_cast<int>(std::min<std::size_t>(proposal->validationCount, 64));
        summary.fitRmsSeconds =
            std::isfinite(proposal->fitRmsSeconds) ? proposal->fitRmsSeconds : 0.0;
        summary.validationFinite = std::isfinite(proposal->validationMaxSeconds);
        summary.validationMaxSeconds = summary.validationFinite
                                           ? proposal->validationMaxSeconds
                                           : 0.0;
        summary.incompatible = proposal->incompatible;
        std::string error;
        if(!draft.ProposeTiming(*proposal->timing, bounds, error, summary))
        {
            Error(error);
            return;
        }
        VideoLibraryMenu::Instance().ApplyAudioSyncPreview(draft.Candidate().profile);
        StopAudition();
        if(analysisResult)
            analysisResult->Hide();
        proposal.reset();
        if(status)
            status->set_text(SummaryStatus(*draft.Candidate().profile.lastAnalysis));
        Refresh();
    }
    void ShowAnalysisResult()
    {
        if(!proposal || !analysisResult || !analysisResultText)
            return;
        if(progress && progressShown)
            progress->Hide();
        progressShown = false;
        analysisResultText->set_text(ResultMessage(*proposal));
        if(analysisResultApply)
            analysisResultApply->set_interactable(
                proposal->timing.has_value() && !proposal->incompatible);
        ShowModalInFront(analysisResult);
    }
    void Refresh()
    {
        if(!draft.IsOpen())
            return;
        refreshing = true;
        struct ResetRefresh
        {
            bool& value;
            ~ResetRefresh() { value = false; }
        } resetRefresh{refreshing};
        const auto& profile = draft.Candidate().profile;
        overviewLayout = profile.waveformLayout;
        overviewZoom = profile.waveformZoom;
        overviewHeightScale = profile.waveformHeightScale;
        if(autoPage)
            autoPage->SetActive(profile.mode == Mode::Automatic);
        if(autoAnalysisSettingsRow)
            autoAnalysisSettingsRow->SetActive(profile.mode == Mode::Automatic);
        if(manualPage)
            manualPage->SetActive(profile.mode == Mode::Manual);
        const bool directTiming = profile.method == TimingMethod::ManualSpeed;
        if(manualDirectTimingRow)
            manualDirectTimingRow->SetActive(directTiming);
        for(auto* row : manualMarkerRows)
            if(row)
                row->SetActive(!directTiming);
        if(manualMarkerOptionsRow)
            manualMarkerOptionsRow->SetActive(
                profile.mode == Mode::Manual && !directTiming);
        if(manualMethodHelp)
            manualMethodHelp->set_text(
                directTiming
                    ? "Direct timing: set when the video starts and how quickly it plays."
                    : "Point matching: choose the same start and end moments in the map "
                      "and video audio. Big Screen calculates offset and speed.");
        const int mode = static_cast<int>(profile.mode);
        const int method = static_cast<int>(profile.method);
        if(mode != visibleMode || method != visibleTimingMethod)
        {
            visibleMode = mode;
            visibleTimingMethod = method;
            ScheduleScrollableLayoutRefresh();
        }
        if(timing)
            timing->set_text(fmt::format(
                "Offset {:+.4f} s    Speed {:.4f}x{}", profile.timing.offsetSeconds,
                profile.timing.playbackRate, draft.Dirty() ? "    (not saved)" : ""));
        if(apply)
            apply->set_interactable(!Busy() && pair != nullptr);
        if(transport)
            transport->set_interactable(!Busy() && pair != nullptr);
        if(timeline)
        {
            timeline->set_interactable(!Busy() && pair != nullptr);
            SetSettingVisualEnabled(timeline, !Busy() && pair != nullptr);
        }
        if(resetTrack)
            resetTrack->set_interactable(!Busy() && pair != nullptr);
        if(analyze)
        {
            analyze->get_gameObject()->SetActive(profile.mode == Mode::Automatic);
            analyze->set_interactable(!Busy() && pair != nullptr);
        }
        for(auto& refresh : refreshControls)
            refresh();
        refreshing = false;
    }
    void EditInternal(const std::function<void(Profile&)>& change,
                      bool restartAuditionAfterEdit)
    {
        ErrorManager::Instance().Guard(
            "Editing audio synchronization",
            [&]
            {
                if(refreshing || !draft.IsOpen() || persistence.Busy())
                    return;
                auto profile = draft.Candidate().profile;
                change(profile);
                if(profile.method == TimingMethod::FitMarkers)
                {
                    if(auto fitted = FitMarkers(profile.markers))
                        profile.timing = *fitted;
                }
                std::string error;
                if(!draft.Edit(profile, bounds, error))
                {
                    if(status)
                        status->set_text(error);
                    Refresh();
                    return;
                }
                VideoLibraryMenu::Instance().ApplyAudioSyncPreview(profile);
                // An old prebuffer contains the old affine
                // mapping/routing/pitch. Normal controls retire it
                // immediately. A dragged waveform marker updates the visible
                // draft continuously but restarts audition only on release;
                // otherwise every controller-move event would destroy and
                // recreate the audio worker before it could emit a buffer.
                if(outputStarted && restartAuditionAfterEdit)
                {
                    position = CurrentAuditionTime();
                    StopAudition();
                    restartAudition = true;
                }
                Refresh();
            });
    }
    void Edit(const std::function<void(Profile&)>& change)
    {
        EditInternal(change, true);
    }
    void EditMarker(const std::function<void(Profile&)>& change, bool finished)
    {
        EditInternal(change, finished);
    }
    bool restartAudition = false;
    double CurrentAuditionTime() const
    {
        if(const auto audible = player.SongTime())
            return *audible;
        if(const auto output = ActiveAudition().Output())
            return output->SongTime();
        return position;
    }
    void StopAudition()
    {
        restartAudition = false;
        if(outputStarted)
            if(const auto output = ActiveAudition().Output())
                BigScreenLogger.info("Audio Sync audition stopped: song={:.3f}s, consumed={} "
                                     "stereo frames, underruns={}",
                                     CurrentAuditionTime(), output->read.load(),
                                     output->underruns.load());
        for(auto& audition : auditions)
            audition->Stop();
        pendingSeek.reset();
        preparedSeek.reset();
        outputStarted = false;
        player.Stop();
        VideoLibraryMenu::Instance().EndSyncAudition();
        if(transport)
            BSML::Lite::SetButtonText(transport, "▶");
    }

    bool StartAuditionAt(Audition& target, double start, std::string& error)
    {
        if(!pair)
        {
            error = "The synchronization audio is not ready.";
            return false;
        }
        const auto& profile = draft.Candidate().profile;
        const double end = profile.loopSelection ? profile.markers.songEnd
                                                 : bounds.songDuration;
        if(start >= end - .001)
            start = profile.loopSelection ? profile.markers.songStart : 0.0;
        AuditionRequest request{pair, profile, start, end, auditionSpeed,
                                UnityEngine::AudioSettings::get_outputSampleRate()};
        return target.Start(std::move(request), error);
    }

    void QueueLiveSeek(double target)
    {
        position = std::clamp(target, 0.0, std::max(0.0, bounds.songDuration));
        pendingSeek = position;
        seekChanged = std::chrono::steady_clock::now();
        // Repeated controller motion coalesces to the newest position. The
        // spare worker can be cancelled freely because it is not connected to
        // Unity audio; the active worker and audible stream continue normally.
        if(preparedSeek || SpareAudition().Busy())
        {
            SpareAudition().Stop();
            preparedSeek.reset();
        }
    }

    void TickLiveSeek()
    {
        if(!outputStarted)
            return;
        auto& spare = SpareAudition();
        if(pendingSeek && !spare.Busy() && !preparedSeek &&
           std::chrono::steady_clock::now() - seekChanged >=
               std::chrono::milliseconds(70))
        {
            const double target = *pendingSeek;
            std::string error;
            if(StartAuditionAt(spare, target, error))
            {
                preparedSeek = target;
                pendingSeek.reset();
            }
            else if(!error.empty())
            {
                pendingSeek.reset();
                Error(error);
            }
        }
        if(!preparedSeek || pendingSeek)
            return;
        auto replacement = spare.Output();
        if(!replacement)
            return;
        const auto progress = spare.Progress();
        if(progress && progress->status == OperationStatus::Failed)
        {
            preparedSeek.reset();
            Error(progress->error, progress->failure == OperationFailure::Internal);
            return;
        }
        if(replacement->Buffered() < static_cast<std::size_t>(replacement->rate / 4) &&
           !replacement->complete.load())
            return;

        const int previous = activeAudition;
        player.Play(replacement, VideoLibraryMenu::Instance().PauseForSyncAudition());
        activeAudition = 1 - activeAudition;
        auditions[previous]->Stop();
        position = *preparedSeek;
        preparedSeek.reset();
        VideoLibraryMenu::Instance().SyncAuditionClock(
            position, draft.Candidate().profile.showPicture);
        if(transport)
            BSML::Lite::SetButtonText(transport, "Ⅱ");
    }
    void Close(bool restore)
    {
        const bool wasOpen = draft.IsOpen();
        const bool discarded = restore && draft.Dirty();
        preparation.Cancel();
        analysis.Cancel();
        // Release editing/navigation ownership before touching Unity.
        // Even a scene-invalidated AudioSource or image cannot leave an
        // apparently open draft blocking every map-selection action.
        draft.Close();
        proposal.reset();
        ErrorManager::Instance().Guard("Stopping Audio Sync audition", [&] { StopAudition(); });
        ErrorManager::Instance().Guard("Stopping Audio Sync capture", [&] { capture.Stop(); });
        capturing = false;
        preparationHandled = true;
        analysisHandled = true;
        persistenceHandled = true;
        pendingNavigation = {};
        if(leaveConfirm)
            leaveConfirm->Hide();
        saveWhenReady = false;
        pair.reset();
        restartAudition = false;
        ++generation;
        ClearOverviews();
        if(progress && progressShown)
            progress->Hide();
        if(enableProgress && progressShown)
            enableProgress->Hide();
        if(enableError)
            enableError->Hide();
        if(analysisResult)
            analysisResult->Hide();
        progressShown = false;
        if(confirm)
            confirm->Hide();
        ResetDelayedApplySaveProgress(false);
        if(wasOpen && navigate)
            navigate(false);
        VideoLibraryMenu::Instance().RefreshAudioSyncState();
        if(wasOpen)
            DiagnosticSessionLogger::Instance().MenuEvent(
                "audio_sync_close", "audio_sync",
                {{"discarded", discarded ? "true" : "false"}});
    }
    void Save(bool close)
    {
        if(!draft.IsOpen() || persistence.Busy())
            return;
        if(preparation.Busy() || analysis.Busy())
        {
            // Save from the close confirmation is valid even when audio
            // preparation is still running. Cancel that finite work and
            // wait asynchronously for its native cleanup before saving.
            if(close)
            {
                preparation.Cancel();
                analysis.Cancel();
                capture.Stop();
                saveWhenReady = true;
            }
            return;
        }
        saveWhenReady = false;
        const auto candidate = draft.Candidate();
        const auto id = draft.LevelId();
        closingAfterSave = close;
        enableOnly = false;
        delayedApplySaveProgress = !close;
        applySaveProgressShownAt.reset();
        operationKey = id;
        ++generation;
        persistenceHandled = false;
        persistence.Start(
            generation, id,
            [candidate, id](const auto& context) -> std::optional<Record>
            {
                context.Progress("Saving synchronization settings");
                try
                {
                    if(!VideoLibrary::Instance().UpdateAdvancedSync(id, candidate))
                    {
                        context.Fail("The video changed before settings could be saved. "
                                     "Close and reopen its configuration.");
                        return std::nullopt;
                    }
                }
                catch(const VideoLibraryPersistenceError& e)
                {
                    context.Fail(e.what());
                    return std::nullopt;
                }
                return candidate;
            });
        // Closing with Save retains the established immediate progress modal.
        // The non-closing Apply action is shown only if it exceeds the delay;
        // UpdateDelayedApplySaveProgress owns that timer and minimum lifetime.
        if(close)
            ShowProgress();
        Refresh();
    }
    void ShowProgress()
    {
        if(progressShown)
            return;
        if(enableOnly && !draft.IsOpen())
        {
            if(enableProgress)
            {
                enableProgressText->set_text("Checking matching audio...");
                ShowModalInFront(enableProgress);
                progressShown = true;
            }
        }
        else if(progress)
        {
            progressText->set_text("Preparing audio... You can still use the video "
                                   "preview on the right.");
            ShowModalInFront(progress);
            progressShown = true;
        }
    }
    void Number(UnityEngine::Component* parent, const std::string& label, double min,
                double max, std::function<double(const Profile&)> get,
                std::function<void(Profile&, double)> set, bool speed = false, int source = 0,
                float width = FullWidth, float labelFraction = .38f,
                std::string_view hint = {}, bool keepPairTogether = true,
                float titleFontSize = 2.45f)
    {
        RequireUiParent(parent, "create a timing control");
        // Use the same stock BSML slider as Big Screen's basic video controls.
        // The selected precision is applied to the prefab's own arrow buttons;
        // the grab handle remains available for coarse adjustment. Keeping the
        // complete native control also preserves its familiar label/value
        // typography instead of inventing separate +/- buttons.
        const auto extent = [this, source] {
            return source == 1 ? bounds.songDuration : source == 2 ? bounds.videoDuration : 1.0;
        };
        auto* slider = BSML::Lite::CreateSliderSetting(
            parent, label, .0001f, 0, source ? 0 : min, source ? 1 : max, .15f, true, {0, 0},
            [this, set, extent](float value)
            { Edit([&](Profile& p) { set(p, value * extent()); }); });
        FitSliderSetting(slider, width, labelFraction, keepPairTogether,
                         TimingResetButtonSize + TimingResetGap, titleFontSize);
        slider->digits = 2;
        slider->formatter = [this, get](float) -> StringW
        {
            const auto& profile = draft.Candidate().profile;
            return fmt::format("{:.{}f}", get(profile),
                               DecimalPlaces(profile.timePrecision));
        };

        // A field reset restores only this value from the map's initial
        // synchronization profile. It intentionally uses the same baseline
        // as Reset Track (mapper timing plus complete source ranges), while
        // preserving every other unsaved adjustment in the editor.
        auto* reset = BSML::Lite::CreateUIButton(
            parent, "↻", {0.0f, 0.0f}, {8.0f, 8.0f},
            [this, get, set]
            {
                const auto initial = get(draft.Initial());
                Edit([&](Profile& profile) { set(profile, initial); });
            });
        ConfigureTimingResetButton(reset, slider);
        BSML::Lite::AddHoverHint(reset, fmt::format("Reset {}", label));
        if(!hint.empty())
            BSML::Lite::AddHoverHint(slider, std::string(hint));
        refreshControls.push_back(
            [this, slider, reset, get, extent, source, speed]
            {
                const auto& profile = draft.Candidate().profile;
                const auto precision = Step(speed ? profile.speedPrecision
                                                  : profile.timePrecision);
                // Time Precision owns the visible decimal count for all six
                // timing fields. Speed Precision still controls only the
                // playback-speed arrow increment, as its label promises.
                slider->digits = DecimalPlaces(profile.timePrecision);
                slider->increments = static_cast<float>(
                    source ? precision / std::max(.001, extent()) : precision);
                if(slider->slider)
                {
                    // BSML copies `increments` into RangeValuesTextSlider's
                    // numberOfSteps only once during SliderSetting::Setup().
                    // Advanced Sync changes the increment later when Time Step
                    // or Speed Step changes, so updating only the public field
                    // left the native arrow buttons at the construction-time
                    // 0.0001 step. The value did technically move, but by less
                    // than the displayed precision, which looked like mapper
                    // timing had locked the arrows while coarse dragging still
                    // worked. Rebuild the native step count from the current
                    // range every refresh so mapper-authored and user-authored
                    // timing use the same selected arrow precision.
                    const double range = static_cast<double>(slider->slider->maxValue) -
                                         static_cast<double>(slider->slider->minValue);
                    const double increment = std::max(
                        static_cast<double>(slider->increments), 1.0e-9);
                    const auto steps = std::max(
                        1, static_cast<int>(std::llround(range / increment)));
                    slider->slider->set_numberOfSteps(steps + 1);
                }
                slider->set_Value(static_cast<float>(get(profile) / std::max(.001, extent())));
                const bool ready = pair != nullptr && !Busy();
                slider->set_interactable(ready);
                reset->set_interactable(ready);
                SetSettingVisualEnabled(slider, ready);
            });
    }
    BSML::ToggleSetting* Toggle(UnityEngine::Component* parent, const std::string& label,
                                std::function<bool(const Profile&)> get,
                                std::function<void(Profile&, bool)> set,
                                float width = FullWidth, std::string_view hint = {})
    {
        RequireUiParent(parent, "create a toggle");
        auto* control = BSML::Lite::CreateToggle(parent, label, false, [this, set](bool value)
                                                 { Edit([&](Profile& p) { set(p, value); }); });
        FitToggleSetting(control, width);
        if(!hint.empty())
            BSML::Lite::AddHoverHint(control, std::string(hint));
        refreshControls.push_back(
            [this, control, get] {
                UiUtility::SetToggleWithoutNotification(control,
                                                        get(draft.Candidate().profile));
                const bool ready = pair != nullptr && !Busy();
                control->set_interactable(ready);
                SetSettingVisualEnabled(control, ready);
            });
        return control;
    }
};
AudioSyncMenu::AudioSyncMenu() : impl_(std::make_unique<Impl>()) {}
AudioSyncMenu& AudioSyncMenu::Instance()
{
    static auto* menu = new AudioSyncMenu();
    return *menu;
}
bool AudioSyncMenu::IsOpen() const { return impl_->draft.IsOpen(); }
bool AudioSyncMenu::IsBusy() const { return impl_->Busy(); }
bool AudioSyncMenu::OwnsAudition() const
{
    return impl_->outputStarted || impl_->restartAudition;
}
bool AudioSyncMenu::Ready() const { return impl_->pair != nullptr; }
const AudioSync::Profile* AudioSyncMenu::PreviewProfile() const
{
    return IsOpen() ? &impl_->draft.Candidate().profile : nullptr;
}
void AudioSyncMenu::Abort()
{
    impl_->persistence.Cancel();
    impl_->Close(false);
}
void AudioSyncMenu::ForgetUi()
{
    // A scene reset can invalidate the old FlowCoordinator before this
    // retained singleton is reset. Never call its captured navigation
    // closure while abandoning that scene's UI.
    impl_->navigate = {};
    Abort();
    impl_->player.Forget();
    impl_->center = nullptr;
    impl_->editor = nullptr;
    impl_->navigate = {};
    impl_->refreshControls.clear();
    impl_->autoPage = nullptr;
    impl_->autoAnalysisSettingsRow = nullptr;
    impl_->manualPage = nullptr;
    impl_->uiSteps.clear();
    impl_->uiScroll = nullptr;
    impl_->uiRoot = nullptr;
    impl_->uiContent = nullptr;
    impl_->uiAuto = nullptr;
    impl_->uiManual = nullptr;
    impl_->overviewRoot = nullptr;
    impl_->overviewRows = {};
    impl_->overviewGroups = {};
    impl_->overviewLabels = {};
    impl_->overviewViewports = {};
    impl_->overviews = {};
    impl_->overviewScrubbers = {};
    impl_->overviewLines = {};
    impl_->manualDirectTimingRow = nullptr;
    impl_->manualMarkerRows = {};
    impl_->manualMarkerOptionsRow = nullptr;
    impl_->manualMethodHelp = nullptr;
    impl_->visibleMode = -1;
    impl_->visibleTimingMethod = -1;
    impl_->pendingScrollLayoutPasses = 0;
    impl_->overviewLayout = 0;
    impl_->overviewZoom = 0;
    impl_->overviewHeightScale = 0;
    impl_->status = nullptr;
    impl_->timing = nullptr;
    impl_->transport = nullptr;
    impl_->timeline = nullptr;
    impl_->progress = nullptr;
    impl_->confirm = nullptr;
    impl_->analysisResult = nullptr;
    impl_->analysisResultText = nullptr;
    impl_->analysisResultApply = nullptr;
    impl_->leaveConfirm = nullptr;
    impl_->progressText = nullptr;
    impl_->progressFill = nullptr;
    impl_->apply = nullptr;
    impl_->resetTrack = nullptr;
    impl_->analyze = nullptr;
    impl_->enableProgress = nullptr;
    impl_->enableProgressText = nullptr;
    impl_->enableProgressFill = nullptr;
    impl_->enableError = nullptr;
    impl_->enableErrorText = nullptr;
}
bool AudioSyncMenu::CreateUi(HMUI::ViewController* center, HMUI::ViewController* editor,
                             std::function<void(bool)> navigate)
{
    auto& s = *impl_;
    if(s.uiRoot && s.uiSteps.empty())
        return true;
    if(!s.uiRoot && s.uiSteps.empty())
    {
        s.center = center;
        s.editor = editor;
        s.navigate = std::move(navigate);
        auto dropdown = [&](UnityEngine::Component* parent, const std::string& label,
                            std::vector<std::string_view> choices,
                            std::function<int(const Profile&)> get,
                            std::function<void(Profile&, int)> set,
                            float width = FullWidth,
                            std::string_view hint = {}) -> BSML::DropdownListSetting*
        {
            RequireUiParent(parent, "create a dropdown");
            auto* control = BSML::Lite::CreateDropdown(
                parent, label, choices.front(), choices,
                [&s, choices, set](StringW value)
                {
                    const std::string text(value);
                    const auto it = std::find(choices.begin(), choices.end(), text);
                    if(it != choices.end())
                        s.Edit([&](Profile& p)
                               { set(p, static_cast<int>(it - choices.begin())); });
                });
            FitDropdownSetting(control, width);
            if(!hint.empty())
                BSML::Lite::AddHoverHint(control, std::string(hint));
            s.refreshControls.push_back(
                [&s, control, get]
                {
                    control->index = get(s.draft.Candidate().profile);
                    control->dropdown->SelectCellWithIdx(control->index);
                    control->UpdateState();
                    const bool ready = s.pair != nullptr && !s.Busy();
                    control->set_interactable(ready);
                    SetSettingVisualEnabled(control, ready);
                });
            return control;
        };

        // Native control construction must remain on Unity's thread, but
        // never all in one frame. The existing guarded prewarm driver calls
        // this repeatedly; one small group is created per scheduling slice.
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                // Copy Saber Stage's proven full-center scaffold exactly:
                // a native segmented tab anchored at the top, a page inset on
                // all four sides, and a scroll viewport inset again inside the
                // page. Neither the page nor scroll mask adds a visible fill;
                // the center workspace therefore remains transparent like Big
                // Screen's side menus while retaining the native clip bounds.
                static std::array<std::string_view, 1> tabs{"Audio Sync"};
                auto* tab = BSML::Lite::CreateTextSegmentedControl(
                    s.center, {0.0f, 0.0f}, {86.0f, 7.0f}, tabs, [](int) {});
                if(auto tabRect = tab->get_transform().cast<UnityEngine::RectTransform>())
                {
                    tabRect->set_anchorMin({0.0f, 1.0f});
                    tabRect->set_anchorMax({1.0f, 1.0f});
                    tabRect->set_pivot({0.5f, 1.0f});
                    tabRect->set_anchoredPosition({0.0f, -1.5f});
                    tabRect->set_sizeDelta({-4.0f, 7.0f});
                }
                tab->SelectCellWithNumber(0);

                auto* page = UnityEngine::GameObject::New_ctor("Big Screen Audio Sync Tab Page");
                if(!page)
                    throw std::runtime_error("could not create the Audio Sync center page");
                auto* pageRect = page->AddComponent<UnityEngine::RectTransform*>();
                page->get_transform()->SetParent(s.center->get_transform(), false);
                FitInsideParent(pageRect, CenterPageInset, CenterPageInset,
                                CenterPageInset, CenterTabStripHeight);

                auto* scroll = BSML::Lite::CreateScrollableSettingsContainer(
                    page->get_transform());
                if(!scroll)
                    throw std::runtime_error("could not create the Audio Sync scroll container");
                auto* external = scroll->GetComponent<BSML::ExternalComponents*>();
                auto* scrollRect = external
                                       ? external->Get<UnityEngine::RectTransform*>()
                                       : nullptr;
                if(!scrollRect)
                    throw std::runtime_error("Audio Sync scroll container has no viewport");

                // CreateScrollableSettingsContainer clones Beat Saber's EULA
                // TextPageScrollView. That prefab deliberately treats a short
                // thumbstick gesture as a page flick: after moving continuously
                // for a moment, releasing the stick snaps the destination by a
                // large fraction of the viewport. That is useful for legal text,
                // but it makes this editor's closely spaced sliders and waveform
                // controls appear to jump between pages. A zero quick-snap window
                // disables only that release-time page flick. The native HMUI
                // ScrollView remains enabled, so held-thumbstick movement stays
                // continuous and the existing clipping, scroll indicator, end
                // clamping, and explicit page buttons retain their normal roles.
                // The object returned by CreateScrollableSettingsContainer is
                // the content root. The cloned ScrollView lives on the outer
                // prefab and BSML exposes it through ExternalComponents; a
                // direct GetComponent on the content root always returns null.
                auto* nativeScroll = external
                    ? external->Get<BSML::ScrollView*>()
                    : nullptr;
                if(!nativeScroll)
                    throw std::runtime_error("Audio Sync scroll container has no ScrollView");
                s.uiScroll = nativeScroll;
                nativeScroll->_joystickQuickSnapMaxTime = 0.0f;

                FitInsideParent(scrollRect, CenterScrollHorizontalInset,
                                CenterScrollVerticalInset, CenterScrollHorizontalInset,
                                CenterScrollVerticalInset);

                root = s.uiRoot = scroll->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>();
                if(!root)
                    throw std::runtime_error("Audio Sync scroll container has no row layout");
                content = s.uiContent = root;
                content->set_spacing(1.0f);
                content->set_childForceExpandHeight(false);
                content->set_childForceExpandWidth(false);
                content->set_childControlWidth(true);
                content->set_childControlHeight(true);
                content->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
                // Match Saber Stage's 116-unit center content contract. The
                // outer page and scroll mask already provide the safe border,
                // so only a small inner margin is needed here.
                content->set_padding(UnityEngine::RectOffset::New_ctor(1, 1, 0, 0));
            });
        s.uiSteps.emplace_back(
            [&s, dropdown]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                // The most frequently used routing and transaction actions
                // stay visible at the very top. Their widths consume the real
                // 116-unit content row exactly (including two one-unit gaps),
                // so none retain a hidden stock-prefab span that can overlap a
                // neighbor. Apply and Reset use the same blue action treatment
                // as the preview transport instead of looking disabled.
                auto* topActions = Row(content);
                dropdown(
                    topActions, "Audio Monitoring",
                    {"Map left / Video right", "Video left / Map right", "Mix both tracks",
                     "Map only", "Video only"},
                    [](const Profile& p) { return static_cast<int>(p.routing); },
                    [](Profile& p, int v) { p.routing = static_cast<Routing>(v); }, 54.0f,
                    "Chooses how the map and video audio are routed while previewing. "
                    "This affects only the Audio Sync workspace.");
                s.apply = Button(topActions, "Apply Changes", [&s] { s.Save(false); }, 30.0f);
                BSML::Lite::AddHoverHint(
                    s.apply,
                    "Saves this map's Advanced Sync settings without closing the editor.");
                s.resetTrack = Button(
                    topActions, "Reset Track",
                    [&s]
                    {
                        if(!s.draft.IsOpen() || s.Busy())
                            return;
                        s.draft.Reset();
                        s.StopAudition();
                        VideoLibraryMenu::Instance().ApplyAudioSyncPreview(
                            s.draft.Candidate().profile);
                        s.Refresh();
                    },
                    30.0f);
                BSML::Lite::AddHoverHint(
                    s.resetTrack,
                    "Restores all Advanced Sync settings for this map to their original "
                    "values. Nothing is saved until Apply Changes is selected.");
                Header(content, "Synchronization Setup");
                auto* modeRow = Row(content);
                modeRow->set_childAlignment(UnityEngine::TextAnchor::MiddleLeft);
                modeRow->set_spacing(2.0f);
                constexpr float AnalyzeWidth = 37.0f;
                constexpr float WorkflowWidth = FullWidth - AnalyzeWidth - 2.0f;
                dropdown(
                    modeRow, "Sync Workflow", {"Automatic Match", "Manual Adjustment"},
                    [](const Profile& p) { return static_cast<int>(p.mode); },
                    [](Profile& p, int v) { p.mode = static_cast<Mode>(v); }, WorkflowWidth,
                    "Automatic Match analyzes both audio tracks. Manual Adjustment lets "
                    "you set timing directly or match two points by ear.");
                s.analyze = Button(modeRow, "Analyze Audio", [&s] { s.StartAnalysis(); },
                                   AnalyzeWidth);
                BSML::Lite::AddHoverHint(
                    s.analyze,
                    "Analyzes the map and video audio in the background and proposes an "
                    "offset and playback speed. It remains available so the analysis can "
                    "be run again with different matching settings.");

                // Analyzer effort belongs directly beneath the workflow that
                // invokes it. These compact dropdowns deliberately leave a
                // small amount of unused width at the right edge instead of
                // stretching four short values across the full center panel.
                // They change only the next analysis request, so neither
                // control restarts an active preview or touches video timing.
                auto* analysisSettings = Row(content);
                s.autoAnalysisSettingsRow = analysisSettings->get_gameObject();
                analysisSettings->set_childAlignment(UnityEngine::TextAnchor::MiddleLeft);
                analysisSettings->set_spacing(4.0f);
                static std::vector<std::string_view> MatchingPointChoices{
                    "5 points", "8 points", "12 points", "16 points"};
                auto* matchingPoints = BSML::Lite::CreateDropdown(
                    analysisSettings, "Matching Points", MatchingPointChoices[1],
                    MatchingPointChoices,
                    [&s](StringW value)
                    {
                        if(s.refreshing)
                            return;
                        const std::string text(value);
                        const int points = text.starts_with("5 ")    ? 5
                                           : text.starts_with("8 ")  ? 8
                                           : text.starts_with("12 ") ? 12
                                                                     : 16;
                        s.EditWithoutPlaybackChange(
                            [points](Profile& p) { p.anchorCount = points; });
                    });
                // Right-align the caption within its compact group so the
                // dropdown begins immediately after the label instead of at
                // the far edge of a large invisible title region.
                FitDropdownSetting(matchingPoints, 54.0f, .58f, true);
                BSML::Lite::AddHoverHint(
                    matchingPoints,
                    "Sets how many useful locations across both tracks Automatic Match "
                    "tries to confirm. More points can take longer.");
                s.refreshControls.push_back(
                    [&s, matchingPoints]
                    {
                        const int points = s.draft.Candidate().profile.anchorCount;
                        matchingPoints->index = points == 5    ? 0
                                                : points == 8  ? 1
                                                : points == 12 ? 2
                                                               : 3;
                        matchingPoints->dropdown->SelectCellWithIdx(
                            matchingPoints->index);
                        matchingPoints->UpdateState();
                        const bool ready = s.pair != nullptr && !s.Busy();
                        matchingPoints->set_interactable(ready);
                        SetSettingVisualEnabled(matchingPoints, ready);
                    });

                static std::vector<std::string_view> SampleDurationChoices{
                    "4", "8", "12", "16"};
                auto* sampleDuration = BSML::Lite::CreateDropdown(
                    analysisSettings, "Sample Duration", SampleDurationChoices[1],
                    SampleDurationChoices,
                    [&s](StringW value)
                    {
                        if(s.refreshing)
                            return;
                        const double seconds = std::clamp(
                            static_cast<double>(std::atoi(std::string(value).c_str())),
                            4.0, 16.0);
                        s.EditWithoutPlaybackChange(
                            [seconds](Profile& p) { p.windowSeconds = seconds; });
                    });
                // Sample Duration needs more one-line caption width than its
                // four very short values. Word wrapping is disabled by the
                // shared fitter and the selector is sized only for 4/8/12/16.
                FitDropdownSetting(sampleDuration, 50.0f, .68f, true);
                BSML::Lite::AddHoverHint(
                    sampleDuration,
                    "Sets how many seconds of audio are compared at each matching point. "
                    "Longer samples can improve difficult matches but take longer.");
                s.refreshControls.push_back(
                    [&s, sampleDuration]
                    {
                        const double seconds =
                            s.draft.Candidate().profile.windowSeconds;
                        sampleDuration->index = seconds <= 6.0    ? 0
                                                : seconds <= 10.0 ? 1
                                                : seconds <= 14.0 ? 2
                                                                  : 3;
                        sampleDuration->dropdown->SelectCellWithIdx(
                            sampleDuration->index);
                        sampleDuration->UpdateState();
                        const bool ready = s.pair != nullptr && !s.Busy();
                        sampleDuration->set_interactable(ready);
                        SetSettingVisualEnabled(sampleDuration, ready);
                    });
                s.status = Text(content, "Preparing map and video audio...", 12);
            });
        s.uiSteps.emplace_back(
            [this, &s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                Header(content, "Audio Overview");
                // Layout, horizontal zoom, and track height do not alter audio
                // timing, but they are part of this map's saved editor state.
                // Their values are all short, so use three deliberately sized
                // label/selector groups on one full-width row. Each label is
                // right-aligned against its own dropdown; the explicit widths
                // prevent BSML's stock dropdown from consuming unused space.
                auto* overviewControls = Row(content);
                overviewControls->set_childAlignment(UnityEngine::TextAnchor::MiddleLeft);
                overviewControls->set_spacing(2.0f);
                static std::vector<std::string_view> OverviewLayouts{
                    "Side by side", "Stacked", "Overlay"};
                auto* overviewLayout = BSML::Lite::CreateDropdown(
                    overviewControls, "Graph Layout", OverviewLayouts.front(),
                    OverviewLayouts,
                    [&s](StringW value)
                    {
                        if(s.refreshing)
                            return;
                        const auto text = std::string(value);
                        const int layout =
                            text == "Stacked" ? 1 : text == "Overlay" ? 2 : 0;
                        s.EditWithoutPlaybackChange(
                            [layout](Profile& p) { p.waveformLayout = layout; });
                        s.ApplyOverviewLayout();
                    });
                FitDropdownSetting(overviewLayout, 40.0f, .43f, true);
                BSML::Lite::AddHoverHint(
                    overviewLayout,
                    "Shows the map and video waveforms beside each other, stacked, or "
                    "overlaid for direct comparison.");
                s.refreshControls.push_back(
                    [&s, overviewLayout]
                    {
                        overviewLayout->index = s.draft.Candidate().profile.waveformLayout;
                        overviewLayout->dropdown->SelectCellWithIdx(overviewLayout->index);
                        overviewLayout->UpdateState();
                        overviewLayout->set_interactable(!s.persistence.Busy());
                    });
                static std::vector<std::string_view> WaveZooms{
                    "Full Track", "2x",  "4x",  "6x",  "8x",  "10x",
                    "12x",       "14x", "16x", "18x", "20x"};
                auto* waveZoom = BSML::Lite::CreateDropdown(
                    overviewControls, "Wave Zoom", WaveZooms.front(), WaveZooms,
                    [&s](StringW value)
                    {
                        if(s.refreshing)
                            return;
                        const std::string text(value);
                        const int zoom = text == "Full Track"
                                             ? 0
                                             : std::clamp(std::atoi(text.c_str()), 0, 20);
                        s.EditWithoutPlaybackChange(
                            [zoom](Profile& p) { p.waveformZoom = zoom; });
                        s.PaintOverviews();
                    });
                FitDropdownSetting(waveZoom, 38.0f, .45f, true);
                BSML::Lite::AddHoverHint(
                    waveZoom,
                    "Zooms horizontally around the yellow playhead. During playback the "
                    "waveform moves beneath the playhead so detail remains visible.");
                s.refreshControls.push_back(
                    [&s, waveZoom]
                    {
                        const int zoom = s.draft.Candidate().profile.waveformZoom;
                        waveZoom->index = zoom <= 0 ? 0 : std::clamp(zoom / 2, 1, 10);
                        waveZoom->dropdown->SelectCellWithIdx(waveZoom->index);
                        waveZoom->UpdateState();
                        waveZoom->set_interactable(!s.persistence.Busy());
                    });

                static std::vector<std::string_view> WaveHeights{
                    "Default", "2x", "4x", "8x"};
                auto* waveHeight = BSML::Lite::CreateDropdown(
                    overviewControls, "Wave Height", WaveHeights.front(), WaveHeights,
                    [&s](StringW value)
                    {
                        if(s.refreshing)
                            return;
                        const std::string text(value);
                        const int height = text == "8x" ? 3 : text == "4x" ? 2
                                                               : text == "2x" ? 1 : 0;
                        s.EditWithoutPlaybackChange(
                            [height](Profile& p) { p.waveformHeightScale = height; });
                        s.ApplyOverviewLayout();
                    });
                // Thirty-four units leaves only the width required by the
                // longest value (Default) after the one-line Wave Height label.
                FitDropdownSetting(waveHeight, 34.0f, .54f, true);
                BSML::Lite::AddHoverHint(
                    waveHeight,
                    "Makes the waveform viewing area taller without changing its audio "
                    "data. Larger views add scrollable height to this page.");
                s.refreshControls.push_back(
                    [&s, waveHeight]
                    {
                        waveHeight->index =
                            s.draft.Candidate().profile.waveformHeightScale;
                        waveHeight->dropdown->SelectCellWithIdx(waveHeight->index);
                        waveHeight->UpdateState();
                        waveHeight->set_interactable(!s.persistence.Busy());
                    });

                auto* overviewRoot = BSML::Lite::CreateVerticalLayoutGroup(content);
                s.overviewRoot = overviewRoot;
                overviewRoot->set_spacing(0);
                overviewRoot->set_childForceExpandHeight(false);
                overviewRoot->set_childForceExpandWidth(false);
                overviewRoot->set_childControlHeight(true);
                overviewRoot->set_childControlWidth(true);
                overviewRoot->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
                for(int rowIndex = 0; rowIndex < 2; ++rowIndex)
                {
                    auto* row = BSML::Lite::CreateHorizontalLayoutGroup(overviewRoot);
                    row->set_spacing(1);
                    row->set_childForceExpandHeight(false);
                    row->set_childForceExpandWidth(false);
                    row->set_childControlHeight(true);
                    row->set_childControlWidth(true);
                    row->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
                    s.overviewRows[rowIndex] = row;
                }
                for(int i = 0; i < 2; ++i)
                {
                    auto* group = BSML::Lite::CreateVerticalLayoutGroup(s.overviewRows[0]);
                    group->set_spacing(0);
                    group->set_childForceExpandHeight(false);
                    group->set_childForceExpandWidth(false);
                    group->set_childControlHeight(true);
                    group->set_childControlWidth(true);
                    group->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
                    s.overviewGroups[i] = group;
                    s.overviewLabels[i] = Text(
                        group, i == 0 ? "Map Audio (cyan)" : "Video Audio (magenta)", 4,
                        HalfWidth);
                    auto* viewport = BSML::Lite::CreateImage(
                        group->get_transform(),
                        BSML::Utilities::ImageResources::GetBlankSprite());
                    viewport->set_color({.035f, .055f, .08f, .96f});
                    viewport->set_preserveAspect(false);
                    viewport->set_raycastTarget(true);
                    viewport->get_gameObject()->AddComponent<UnityEngine::UI::RectMask2D*>();
                    s.overviewViewports[i] = viewport;

                    auto* image = BSML::Lite::CreateImage(
                        viewport->get_transform(),
                        BSML::Utilities::ImageResources::GetBlankSprite());
                    image->set_color(i == 0 ? UnityEngine::Color{.10f, .86f, 1.0f, .92f}
                                            : UnityEngine::Color{1.0f, .28f, .72f, .82f});
                    image->set_preserveAspect(false);
                    image->set_raycastTarget(false);
                    s.overviews[i] = image;
                    for(int line = 0; line < 3; ++line)
                    {
                        auto* marker = BSML::Lite::CreateImage(
                            viewport->get_transform(),
                            BSML::Utilities::ImageResources::GetBlankSprite());
                        marker->set_color(line == 0   ? UnityEngine::Color{1, .72f, .08f, 1}
                                          : line == 1 ? UnityEngine::Color{.1f, 1, .2f, 1}
                                                      : UnityEngine::Color{1, .2f, .15f, 1});
                        // The source is a square blank sprite. Without this,
                        // ImageView preserves that square inside the marker's
                        // tall RectTransform and the intended vertical line is
                        // reduced to a nearly invisible dot.
                        marker->set_preserveAspect(false);
                        marker->set_raycastTarget(false);
                        marker->get_transform()
                            .cast<UnityEngine::RectTransform>()
                            ->set_sizeDelta({.3f, 18});
                        s.overviewLines[i][line] = marker;
                    }
                    auto* scrubber = viewport->get_gameObject()
                                         ->AddComponent<WaveformScrubber*>();
                    scrubber->Configure(
                        viewport->get_rectTransform(),
                        [&s, i]
                        {
                            return i == 0
                                ? s.position
                                : VideoTime(s.draft.Candidate().profile.timing,
                                            s.position).value_or(0.0);
                        },
                        [&s, i]
                        {
                            return i == 0 ? s.bounds.songDuration
                                          : s.bounds.videoDuration;
                        },
                        [&s, i]
                        {
                            const auto songWindow = s.OverviewSongWindow();
                            return i == 0
                                ? songWindow
                                : std::clamp(
                                      songWindow * s.draft.Candidate().profile
                                                       .timing.playbackRate,
                                      .001,
                                      std::max(.001, s.bounds.videoDuration));
                        },
                        [this, &s, i](double sourceSeconds)
                        {
                            ErrorManager::Instance().Guard(
                                "Seeking from audio waveform",
                                [this, &s, i, sourceSeconds]
                                {
                                    if(i == 0)
                                    {
                                        Seek(sourceSeconds);
                                        return;
                                    }
                                    const auto& timing =
                                        s.draft.Candidate().profile.timing;
                                    if(timing.playbackRate > 0)
                                        Seek((sourceSeconds - timing.offsetSeconds) /
                                             timing.playbackRate);
                                });
                        },
                        [&s, i]
                        {
                            const auto& markers = s.draft.Candidate().profile.markers;
                            return i == 0 ? markers.songStart : markers.videoStart;
                        },
                        [&s, i]
                        {
                            const auto& markers = s.draft.Candidate().profile.markers;
                            return i == 0 ? markers.songEnd : markers.videoEnd;
                        },
                        [&s, i](double seconds, bool finished)
                        {
                            s.EditMarker(
                                [i, seconds](Profile& profile)
                                {
                                    if(i == 0)
                                        profile.markers.songStart = seconds;
                                    else
                                        profile.markers.videoStart = seconds;
                                },
                                finished);
                        },
                        [&s, i](double seconds, bool finished)
                        {
                            s.EditMarker(
                                [i, seconds](Profile& profile)
                                {
                                    if(i == 0)
                                        profile.markers.songEnd = seconds;
                                    else
                                        profile.markers.videoEnd = seconds;
                                },
                                finished);
                        },
                        [&s]
                        {
                            const auto& profile = s.draft.Candidate().profile;
                            return s.pair != nullptr && !s.Busy() &&
                                   profile.mode == Mode::Manual &&
                                   profile.method == TimingMethod::FitMarkers;
                        });
                    s.overviewScrubbers[i] = scrubber;
                    BSML::Lite::AddHoverHint(
                        viewport,
                        "Drag the yellow playhead to seek. In Match Start and End Points, "
                        "drag the green and red lines to set that track's matching points. "
                        "At a zoomed level, the waveform moves beneath the fixed playhead.");
                }
                s.ApplyOverviewLayout();

                // Keep transport physically attached to the waveforms. The
                // scroll view previously placed it above Audio Overview, so
                // moving between automatic/manual content could leave the
                // graphs visible while their playback controls jumped outside
                // the viewport. This row is deliberately created immediately
                // after the graphs and their legend, with no intervening
                // section header.
                auto* transport = Row(content);
                transport->set_spacing(3.0f);
                // Reuse the exact glyphs and blue PlayButton treatment from
                // the ordinary Video Library preview so transport meaning does
                // not change when the center workspace owns the audition.
                s.transport = BSML::Lite::CreateUIButton(
                    transport, "▶", "PlayButton", {0, 0}, {8.5f, 7.0f},
                    [this]
                    {
                        ErrorManager::Instance().Guard(
                            "Audio Sync transport", [this] { ToggleAudition(); });
                    });
                Layout(s.transport, 8.5f, 7.0f);
                BSML::Lite::SetButtonTextSize(s.transport, 3.6f);
                StyleBlueButton(s.transport);
                s.timeline = BSML::Lite::CreateSliderSetting(
                    transport, "", .0001f, 0, 0, 1, .15f, false, {0, 0},
                    [this](float value)
                    {
                        if(!impl_->refreshing)
                            ErrorManager::Instance().Guard(
                                "Seeking synchronization preview",
                                [&] { Seek(value * impl_->bounds.songDuration); });
                    });
                // These widths plus the two three-unit gaps consume the exact
                // 116-unit center row. The explicit gap before Playback Speed
                // prevents the timeline/value text from visually clipping the
                // separate speed setting.
                constexpr float PreviewSpeedWidth = 43.0f;
                constexpr float TimelineWidth = FullWidth - 8.5f - PreviewSpeedWidth - 6.0f;
                FitBareSlider(s.timeline, TimelineWidth);
                BSML::Lite::AddHoverHint(
                    s.timeline,
                    "Seeks both audio tracks together. The yellow playhead in the "
                    "waveforms follows this position.");
                s.timeline->formatter = [&s](float value) -> StringW
                {
                    return fmt::format("{:.3f} / {:.3f} s", value * s.bounds.songDuration,
                                       s.bounds.songDuration);
                };

                // Preview speed affects only what the user hears in this
                // workspace; it does not alter saved video playback speed.
                auto* slow = BSML::Lite::CreateSliderSetting(
                    transport, "Playback Speed", .05f, 1, .25f, 1, .15f, true,
                    {0, 0},
                    [&s](float value)
                    {
                        if(s.refreshing)
                            return;
                        ErrorManager::Instance().Guard(
                            "Changing audition speed",
                            [&]
                            {
                                s.auditionSpeed = value;
                                if(s.outputStarted)
                                {
                                    s.position = s.CurrentAuditionTime();
                                    s.StopAudition();
                                    s.restartAudition = true;
                                }
                            });
                    });
                FitSliderSetting(slow, PreviewSpeedWidth, .48f, true);
                slow->digits = 2;
                BSML::Lite::AddHoverHint(
                    slow,
                    "Slows both tracks together while listening for alignment. This does "
                    "not change the saved video playback speed.");
                s.refreshControls.push_back(
                    [&s, slow]
                    {
                        slow->set_Value(s.auditionSpeed);
                        const bool ready = s.pair != nullptr && !s.Busy();
                        slow->set_interactable(ready);
                        SetSettingVisualEnabled(slow, ready);
                    });
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                automatic = s.uiAuto = BSML::Lite::CreateVerticalLayoutGroup(content);
                s.autoPage = automatic->get_gameObject();
                automatic->set_childForceExpandHeight(false);
                // Automatic results enter the same timing fields used by the
                // manual workflow. Keeping these sliders visible in Automatic
                // Match lets the user make small corrections without changing
                // modes, while switching to Manual still retains every value.
                auto* timingRow = Row(automatic);
                timingRow->set_spacing(PairedSettingGap);
                s.Number(
                    timingRow, "Video Offset", -60, 60,
                    [](const Profile& p) { return p.timing.offsetSeconds; },
                    [](Profile& p, double v) { p.timing.offsetSeconds = v; }, false, 0,
                    PairedSettingWidth, PairedSettingLabelFraction,
                    "Fine-tunes the accepted automatic offset. Negative values delay the "
                    "video; positive values start farther into it.", false, 2.8f);
                s.Number(
                    timingRow, "Video Speed", .05, 8,
                    [](const Profile& p) { return p.timing.playbackRate; },
                    [](Profile& p, double v) { p.timing.playbackRate = v; }, true, 0,
                    PairedSettingWidth, PairedSettingLabelFraction,
                    "Fine-tunes the accepted automatic playback speed. 1.00x is the "
                    "original speed.", true, 2.8f);
            });
        s.uiSteps.emplace_back(
            [&s, dropdown]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                manual = s.uiManual = BSML::Lite::CreateVerticalLayoutGroup(content);
                s.manualPage = manual->get_gameObject();
                manual->set_childForceExpandHeight(false);
                Header(manual, "Manual Timing");
                auto* precision = Row(manual);
                // Each precision caption and selector is one compact group.
                // A twelve-unit center gap makes the two groups unmistakably
                // separate, while a wider caption region prevents TMP from
                // collapsing the labels into vertical stacks.
                constexpr float PrecisionGap = 12.0f;
                constexpr float PrecisionWidth = (FullWidth - PrecisionGap) * .5f;
                precision->set_spacing(PrecisionGap);
                auto* timePrecision = dropdown(
                    precision, "Time Step",
                    {"0.1 s", "0.01 s", "0.001 s", "0.0001 s"},
                    [](const Profile& p) { return static_cast<int>(p.timePrecision); },
                    [](Profile& p, int v) { p.timePrecision = static_cast<Precision>(v); },
                    PrecisionWidth,
                    "Sets the arrow-button step and displayed decimal places for video "
                    "offset and all four audio-point controls.");
                FitDropdownSetting(timePrecision, PrecisionWidth, .56f, true);
                auto* speedPrecision = dropdown(
                    precision, "Speed Step",
                    {"0.1x", "0.01x", "0.001x", "0.0001x"},
                    [](const Profile& p) { return static_cast<int>(p.speedPrecision); },
                    [](Profile& p, int v) { p.speedPrecision = static_cast<Precision>(v); },
                    PrecisionWidth,
                    "Sets the arrow-button step used by Video Playback Speed.");
                FitDropdownSetting(speedPrecision, PrecisionWidth, .56f, true);
                dropdown(
                    manual, "Adjustment Method",
                    {"Set Offset and Speed", "Match Start and End Points"},
                    [](const Profile& p) { return static_cast<int>(p.method); },
                    [](Profile& p, int v) { p.method = static_cast<TimingMethod>(v); },
                    FullWidth,
                    "Set Offset and Speed edits timing directly. Match Start and End Points "
                    "calculates both values from two corresponding moments in each track.");
                s.manualMethodHelp = Text(manual, "", 6);
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                auto* row = Row(manual);
                s.manualDirectTimingRow = row->get_gameObject();
                row->set_spacing(PairedSettingGap);
                s.Number(
                    row, "Video Offset", -60, 60,
                    [](const Profile& p) { return p.timing.offsetSeconds; },
                    [](Profile& p, double v) { p.timing.offsetSeconds = v; }, false, 0,
                    PairedSettingWidth, PairedSettingLabelFraction,
                    "Moves the video relative to the map audio. Negative values delay the "
                    "video; positive values start farther into it.");
                s.Number(
                    row, "Video Speed", .05, 8,
                    [](const Profile& p) { return p.timing.playbackRate; },
                    [](Profile& p, double v) { p.timing.playbackRate = v; }, true, 0,
                    PairedSettingWidth, PairedSettingLabelFraction,
                    "Controls how quickly the video and its audio advance. 1.00x is the "
                    "original speed.");
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                auto* row = Row(manual);
                s.manualMarkerRows[0] = row->get_gameObject();
                row->set_spacing(PairedSettingGap);
                s.Number(
                    row, "Map Audio Start", 0, 3600,
                    [](const Profile& p) { return p.markers.songStart; },
                    [](Profile& p, double v) { p.markers.songStart = v; }, false, 1,
                    PairedSettingWidth, PairedSettingLabelFraction,
                    "Selects the first recognizable moment in the map's song audio.");
                s.Number(
                    row, "Map Audio End", 0, 3600,
                    [](const Profile& p) { return p.markers.songEnd; },
                    [](Profile& p, double v) { p.markers.songEnd = v; }, false, 1,
                    PairedSettingWidth, PairedSettingLabelFraction,
                    "Selects a later recognizable moment in the map's song audio.");
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                auto* row = Row(manual);
                s.manualMarkerRows[1] = row->get_gameObject();
                row->set_spacing(PairedSettingGap);
                s.Number(
                    row, "Video Audio Start", 0, 3600,
                    [](const Profile& p) { return p.markers.videoStart; },
                    [](Profile& p, double v) { p.markers.videoStart = v; }, false, 2,
                    PairedSettingWidth, PairedSettingLabelFraction,
                    "Selects the matching first moment in the video's audio.");
                s.Number(
                    row, "Video Audio End", 0, 3600,
                    [](const Profile& p) { return p.markers.videoEnd; },
                    [](Profile& p, double v) { p.markers.videoEnd = v; }, false, 2,
                    PairedSettingWidth, PairedSettingLabelFraction,
                    "Selects the moment in the video audio matching Map Audio End. Big "
                    "Screen uses the two pairs to calculate playback speed.");
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                Header(content, "Preview Options");
                auto* options = Row(content, 7);
                options->set_spacing(8.0f);
                auto* pitchCorrection = s.Toggle(
                    options, "Pitch Correction",
                    [](const Profile& p) { return p.pitchCorrection; },
                    [](Profile& p, bool v) { p.pitchCorrection = v; }, 42.0f);
                auto* showVideo = s.Toggle(
                    options, "Show Video", [](const Profile& p)
                    { return p.showPicture; }, [](Profile& p, bool v) { p.showPicture = v; },
                    32.0f);
                BSML::Lite::AddHoverHint(
                    pitchCorrection,
                    "Keeps the video's audio pitch more natural when its playback speed "
                    "is changed. This affects only the synchronization preview.");
                BSML::Lite::AddHoverHint(
                    showVideo,
                    "Shows the video while previewing both audio tracks. Turn this off to "
                    "reduce decoder and rendering work while matching audio by ear.");
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                auto* pitchRow = Row(content);
                constexpr float AutoPitchWidth = 35.0f;
                constexpr float PitchSliderWidth = FullWidth - AutoPitchWidth - 1.0f;
                auto* pitch = BSML::Lite::CreateSliderSetting(
                    pitchRow, "Pitch Adjustment", 1, 0, -200, 200, .15f, true, {0, 0},
                    [&s](float v) { s.Edit([&](Profile& p) { p.finePitchCents = v; }); });
                Layout(pitch, PitchSliderWidth, 8);
                auto* autoPitch = Button(
                    pitchRow, "Set Automatically",
                    [&s]
                    {
                        s.Edit(
                            [](Profile& p)
                            {
                                p.autoPitchSemitones =
                                    AutoPitch(p.timing.playbackRate).value_or(0);
                                p.finePitchCents = 0;
                            });
                    },
                    AutoPitchWidth);
                BSML::Lite::AddHoverHint(
                    pitch,
                    "Fine-tunes the automatically corrected video-audio pitch by up to "
                    "200 cents in either direction.");
                BSML::Lite::AddHoverHint(
                    autoPitch,
                    "Sets pitch correction from the current video playback speed. You can "
                    "still fine-tune it with the slider afterward.");
                s.refreshControls.push_back(
                    [&s, pitch, autoPitch]
                    {
                        pitch->set_Value(s.draft.Candidate().profile.finePitchCents);
                        const bool enabled = s.pair != nullptr && !s.Busy() &&
                            s.draft.Candidate().profile.pitchCorrection;
                        pitch->set_interactable(enabled);
                        SetSettingVisualEnabled(pitch, enabled);
                        autoPitch->set_interactable(
                            enabled);
                    });
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                auto* options = Row(content, 7);
                s.manualMarkerOptionsRow = options->get_gameObject();
                s.Toggle(
                    options, "Loop Matched Section",
                    [](const Profile& p) { return p.loopSelection; },
                    [](Profile& p, bool v) { p.loopSelection = v; }, HalfWidth,
                    "Repeats Map Audio Start through Map Audio End while previewing. It "
                    "does not loop the Beat Saber map during gameplay.");
                s.Toggle(
                    options, "Hide After Video End",
                    [](const Profile& p) { return p.stopAtEndMarker; },
                    [](Profile& p, bool v) { p.stopAtEndMarker = v; }, HalfWidth,
                    "Hides the video after Video Audio End instead of continuing to show "
                    "content beyond the selected matching point.");
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                s.timing = Text(content, "", 8);
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                s.progress = BSML::Lite::CreateModal(s.center, {60, 32}, nullptr, false);
                auto* column = ModalColumn(s.progress);
                s.progressText = BSML::Lite::CreateText(column, "", 3);
                Layout(s.progressText, 52, 11);
                s.progressText->set_enableWordWrapping(true);
                s.progressText->set_enableAutoSizing(true);
                s.progressText->set_fontSizeMin(2.2f);
                s.progressText->set_fontSizeMax(3.0f);
                s.progressText->set_alignment(TMPro::TextAlignmentOptions::Center);
                ModalProgressTrack(column, s.progressFill);
                s.progressCancel = DialogButton(
                    column, "Cancel",
                    [&s]
                    {
                        s.preparation.Cancel();
                        s.analysis.Cancel();
                        s.capture.Stop();
                        if(s.enableOnly)
                            s.persistence.Cancel();
                        if(!s.persistence.Busy())
                        {
                            s.progress->Hide();
                            s.progressShown = false;
                        }
                    },
                    24);
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                s.analysisResult =
                    BSML::Lite::CreateModal(s.center, {76, 52}, nullptr, false);
                auto* column = ModalColumn(s.analysisResult);
                auto* title = BSML::Lite::CreateText(column, "Analysis Complete", 4);
                Layout(title, 68, 5);
                title->set_alignment(TMPro::TextAlignmentOptions::Center);
                s.analysisResultText = BSML::Lite::CreateText(column, "", 3);
                Layout(s.analysisResultText, 68, 31);
                s.analysisResultText->set_enableWordWrapping(true);
                s.analysisResultText->set_enableAutoSizing(true);
                s.analysisResultText->set_fontSizeMin(2.8f);
                s.analysisResultText->set_fontSizeMax(3.4f);
                s.analysisResultText->set_alignment(TMPro::TextAlignmentOptions::Center);
                // Result details are diagnostic and actionable. Ellipsis hid
                // the analyzer's final explanation, so reserve enough height
                // for the complete wrapped message and never truncate it.
                s.analysisResultText->set_overflowMode(TMPro::TextOverflowModes::Overflow);
                auto* buttons = Row(column, 7);
                Layout(buttons, 68, 7);
                buttons->set_spacing(2.0f);
                DialogButton(buttons, "Discard", [&s] { s.DiscardAnalysisResult(); }, 29.0f);
                s.analysisResultApply =
                    DialogButton(buttons, "Apply", [&s] { s.ApplyAnalysisResult(); }, 29.0f);
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                [[maybe_unused]] auto* root = s.uiRoot;
                [[maybe_unused]] auto* content = s.uiContent;
                [[maybe_unused]] auto* automatic = s.uiAuto;
                [[maybe_unused]] auto* manual = s.uiManual;
                s.confirm = BSML::Lite::CreateModal(s.center, {60, 27}, nullptr, false);
                auto* column = ModalColumn(s.confirm);
                auto* message = BSML::Lite::CreateText(
                    column, "Save changes to this map before closing Audio Sync?", 3);
                Layout(message, 52, 11);
                message->set_enableWordWrapping(true);
                message->set_alignment(TMPro::TextAlignmentOptions::Center);
                auto* buttons = Row(column, 7);
                Layout(buttons, 50, 7);
                DialogButton(buttons, "Save",
                             [&s]
                             {
                                 s.confirm->Hide();
                                 s.Save(true);
                             }, 24.5f);
                DialogButton(buttons, "Discard", [&s] { s.Close(true); }, 24.5f);
            });
        s.uiSteps.emplace_back(
            [&s]
            {
                s.enableProgress = BSML::Lite::CreateModal(s.editor, {60, 30}, nullptr, false);
                auto* progressColumn = ModalColumn(s.enableProgress);
                s.enableProgressText = BSML::Lite::CreateText(
                    progressColumn, "Checking matching audio...", 3);
                Layout(s.enableProgressText, 52, 10);
                s.enableProgressText->set_enableWordWrapping(true);
                s.enableProgressText->set_alignment(TMPro::TextAlignmentOptions::Center);
                ModalProgressTrack(progressColumn, s.enableProgressFill);
                DialogButton(progressColumn, "Cancel", [&s] { s.persistence.Cancel(); }, 24);

                // This modal deliberately belongs to the right editor rather
                // than ErrorManager's left settings view. It handles both the
                // initial audio check and expected preparation/analysis media
                // failures, and ShowModalInFront keeps it above the side menu.
                s.enableError = BSML::Lite::CreateModal(s.editor, {66, 38}, nullptr, false);
                auto* errorColumn = ModalColumn(s.enableError);
                auto* title = BSML::Lite::CreateText(
                    errorColumn, "Advanced Sync Error", 4);
                Layout(title, 58, 6);
                title->set_alignment(TMPro::TextAlignmentOptions::Center);
                s.enableErrorText = BSML::Lite::CreateText(
                    errorColumn, "", 3);
                Layout(s.enableErrorText, 58, 18);
                s.enableErrorText->set_enableWordWrapping(true);
                s.enableErrorText->set_enableAutoSizing(true);
                s.enableErrorText->set_fontSizeMin(2.0f);
                s.enableErrorText->set_fontSizeMax(3.0f);
                s.enableErrorText->set_alignment(TMPro::TextAlignmentOptions::Center);
                s.enableErrorText->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
                DialogButton(errorColumn, "OK", [&s] { s.enableError->Hide(); }, 24);
            });
        s.uiSteps.emplace_back(
            [&s, this]
            {
                s.leaveConfirm = BSML::Lite::CreateModal(s.editor, {62, 30}, nullptr, false);
                auto* column = ModalColumn(s.leaveConfirm);
                auto* message =
                    BSML::Lite::CreateText(column,
                                           "Audio checking is still running. Leaving "
                                           "will cancel it. Leave this map?",
                                           3);
                Layout(message, 54, 12);
                message->set_enableWordWrapping(true);
                message->set_alignment(TMPro::TextAlignmentOptions::Center);
                auto* buttons = Row(column, 7);
                Layout(buttons, 50, 7);
                DialogButton(buttons, "Stay",
                             [&s]
                             {
                                 s.pendingNavigation = {};
                                 s.leaveConfirm->Hide();
                             }, 24.5f);
                DialogButton(buttons, "Leave",
                             [&s, this]
                             {
                                 auto leave = std::move(s.pendingNavigation);
                                 Abort();
                                 if(leave)
                                     leave();
                             }, 24.5f);
            });
    }
    // Do not remove a stage until it actually succeeds. The old pop-before-run
    // order corrupted the builder after one recoverable exception: the next
    // frame advanced to a row stage with uiContent still null, and BSML then
    // dereferenced that null Component inside IL2CPP. Retaining the failed step
    // makes a retry fail at the same guarded boundary instead of changing a
    // logged setup error into an uncatchable SIGSEGV.
    auto& step = s.uiSteps.front();
    step();
    s.uiSteps.pop_front();
    if(s.uiSteps.empty())
        s.ScheduleScrollableLayoutRefresh();
    return s.uiSteps.empty();
}
void AudioSyncMenu::SetEnabled(VideoDescriptor descriptor, bool enabled)
{
    auto& s = *impl_;
    if(s.Busy() || IsOpen() || !descriptor.playableConfig)
        return;
    if(s.enableError)
        s.enableError->Hide();
    s.enableOnly = true;
    s.closingAfterSave = false;
    s.operationKey = descriptor.levelId;
    s.persistenceHandled = false;
    const auto generation = ++s.generation;
    s.persistence.Start(
        generation, s.operationKey,
        [descriptor = std::move(descriptor),
         enabled](const auto& context) -> std::optional<Record>
        {
            context.Progress(enabled ? "Checking matching audio"
                                     : "Restoring basic video controls");
            const auto fingerprint = SourceFingerprint(descriptor.playableConfig->videoPath);
            if(fingerprint.empty())
            {
                context.Fail("The assigned video is no longer available.");
                return std::nullopt;
            }
            Record record = descriptor.advancedSync.value_or(Record{});
            record.sourceKey = fingerprint;
            record.enabled = enabled;
            if(enabled)
            {
                const auto audio =
                    ProbeAudio(descriptor.syncAudioPath, [&] { return context.Cancelled(); });
                if(!audio.available)
                {
                    context.Fail("Advanced Sync needs usable audio for this video. " +
                                 audio.error +
                                 (descriptor.activeMapFileName
                                      ? " Choose a local video that includes audio."
                                      : " Remove and download this video again to "
                                        "obtain matching audio."));
                    return std::nullopt;
                }
                ReadRequest probe;
                probe.path = descriptor.syncAudioPath;
                probe.startSeconds = std::max(0.0, audio.streamStartSeconds);
                probe.endSeconds = probe.startSeconds + .25;
                probe.cancelled = [&] { return context.Cancelled(); };
                const auto decoded = ReadAudio(probe, [](auto, double) { return true; });
                if(context.Cancelled())
                    return std::nullopt;
                if(!decoded.error.empty() || !decoded.outputSamples)
                {
                    context.Fail("The audio stream could not be decoded. " + decoded.error +
                                 " Basic video playback remains available.");
                    return std::nullopt;
                }
                if(!descriptor.advancedSync)
                    record.profile = Baseline(
                        descriptor, {descriptor.songDurationSeconds,
                                     descriptor.playableConfig->declaredDurationSeconds > 0
                                         ? descriptor.playableConfig->declaredDurationSeconds
                                         : audio.durationSeconds});
            }
            if(context.Cancelled())
                return std::nullopt;
            try
            {
                if(!VideoLibrary::Instance().UpdateAdvancedSync(descriptor.levelId, record))
                {
                    context.Fail("The video assignment changed; reopen its "
                                 "configuration and try again.");
                    return std::nullopt;
                }
            }
            catch(const VideoLibraryPersistenceError& e)
            {
                context.Fail(e.what());
                return std::nullopt;
            }
            return record;
        });
}
void AudioSyncMenu::Open(VideoDescriptor descriptor, std::filesystem::path songDirectory)
{
    auto& s = *impl_;
    if(s.Busy() || IsOpen() || !descriptor.advancedSync || !descriptor.advancedSync->enabled)
        return;
    s.descriptor = descriptor;
    s.bounds = {descriptor.songDurationSeconds,
                descriptor.playableConfig->declaredDurationSeconds};
    s.enableOnly = false;
    if(s.bounds.videoDuration <= 0)
        s.bounds.videoDuration = descriptor.advancedSync->profile.markers.videoEnd;
    s.draft.Open(descriptor.levelId, *descriptor.advancedSync, Baseline(descriptor, s.bounds));
    s.position = VideoLibraryMenu::Instance().SyncSongTime();
    s.auditionSpeed = 1;
    s.operationKey = descriptor.levelId;
    ++s.generation;
    s.pair.reset();
    s.proposal.reset();
    if(s.analysisResult)
        s.analysisResult->Hide();
    if(s.status)
        s.status->set_text("Preparing map and video audio...");
    if(s.navigate)
        s.navigate(true);
    s.Refresh();
    s.ApplyOverviewLayout();
    VideoLibraryMenu::Instance().RefreshAudioSyncState();
    const auto cache = VideoLibrary::Instance().RootPath() / "Audio Sync Cache";
    VideoLibraryMenu::Instance().ApplyAudioSyncPreview(s.draft.Candidate().profile);
    const auto limits = LimitsForModel(std::string(UnityEngine::SystemInfo::get_deviceModel()));
    BigScreenLogger.info("Audio Sync opened: map='{}', profile={}, feature limit={}",
                         descriptor.levelId, limits.family, limits.featureFrames);
    DiagnosticSessionLogger::Instance().MenuEvent(
        "audio_sync_open", "audio_sync",
        {{"levelId", descriptor.levelId}, {"memoryProfile", limits.family}});
    s.preparationHandled = false;
    s.capturing = songDirectory.empty();
    auto capture = s.capturing ? s.capture.Begin() : std::shared_ptr<CaptureBuffer>{};
    s.preparation.Start(
        s.generation, s.operationKey,
        [descriptor, songDirectory, cache, capture,
         limits](const auto& context) -> std::optional<PreparedPair>
        {
            std::string error;
            Source song;
            if(capture)
            {
                context.Progress("Preparing built-in song audio. Streaming songs may "
                                 "need one silent pass.");
                auto lease = PcmCache::Capture(
                    capture, "1.40.8|" + descriptor.levelId, cache,
                    [&] { return context.Cancelled(); },
                    [&](double time)
                    {
                        context.Progress("Preparing built-in song audio",
                                         static_cast<std::uint64_t>(time * 1000),
                                         static_cast<std::uint64_t>(capture->duration * 1000));
                    },
                    error);
                if(context.Cancelled())
                    return std::nullopt;
                if(!lease)
                {
                    context.Fail(error.empty() ? "Song audio capture was interrupted." : error);
                    return std::nullopt;
                }
                song = {lease->path, descriptor.levelId, 0, lease};
            }
            else
                song = SongFile(songDirectory, error);
            if(!error.empty())
            {
                context.Fail(error);
                return std::nullopt;
            }
            Source video{descriptor.syncAudioPath, SourceFingerprint(descriptor.syncAudioPath),
                         0};
            if(video.path != descriptor.playableConfig->videoPath)
            {
                auto shift = VideoStartTime(
                    descriptor.playableConfig->videoPath, [&] { return context.Cancelled(); },
                    error);
                if(!shift)
                {
                    context.Fail(error);
                    return std::nullopt;
                }
                video.timelineShift = *shift;
            }
            auto result = Prepare(song, video, context, cache, limits.featureFrames);
            if(result)
            {
                const auto preparedBounds = result->bounds;
                result->bounds.songDuration =
                    std::max(result->bounds.songDuration, descriptor.songDurationSeconds);
                result->bounds.videoDuration =
                    std::max(result->bounds.videoDuration,
                             descriptor.playableConfig->declaredDurationSeconds);
                // A silent video tail remains part of its media timeline.
                // Paint silence across that tail instead of stretching the
                // shorter audio waveform to fill the entire video range. Do
                // not redraw the larger zoom-capable mask when Prepare already
                // used the same authoritative duration.
                if(result->bounds.songDuration != preparedBounds.songDuration)
                    result->song.overview =
                        BuildOverview(result->song.features, result->bounds.songDuration);
                if(result->bounds.videoDuration != preparedBounds.videoDuration)
                    result->video.overview =
                        BuildOverview(result->video.features, result->bounds.videoDuration);
            }
            if(result)
                BigScreenLogger.info(
                    "Audio Sync prepared: map frames={}, video "
                    "frames={}, map duration={:.3f}s, "
                    "video duration={:.3f}s",
                    result->song.features.frames.size(), result->video.features.frames.size(),
                    result->song.audition->duration, result->video.audition->duration);
            return result;
        });
    s.ShowProgress();
}
void AudioSyncMenu::RequestClose()
{
    auto& s = *impl_;
    if(!IsOpen())
        return;
    if(s.persistence.Busy())
        return;
    if(s.draft.Dirty())
        ShowModalInFront(s.confirm);
    else
        s.Close(true);
}
bool AudioSyncMenu::ConfirmNavigation(std::function<void()> leave)
{
    auto& s = *impl_;
    if(IsOpen())
        return false;
    if(!s.enableOnly || !s.persistence.Busy() || s.persistenceHandled)
        return true;
    s.pendingNavigation = std::move(leave);
    ShowModalInFront(s.leaveConfirm);
    return false;
}
void AudioSyncMenu::Seek(double seconds)
{
    auto& s = *impl_;
    s.position = std::clamp(seconds, 0.0, std::max(0.0, s.bounds.songDuration));
    if(s.outputStarted)
        s.QueueLiveSeek(s.position);
    else if(s.restartAudition)
        s.position = std::clamp(seconds, 0.0, std::max(0.0, s.bounds.songDuration));
    else
        VideoLibraryMenu::Instance().SeekSyncPreview(s.position);
}
void AudioSyncMenu::ToggleAudition()
{
    auto& s = *impl_;
    if(!IsOpen() || !s.pair)
        return;
    if(s.outputStarted || s.restartAudition)
    {
        if(s.outputStarted)
            s.position = s.CurrentAuditionTime();
        s.StopAudition();
        return;
    }
    if(s.ActiveAudition().Busy())
    {
        s.restartAudition = true;
        return;
    }
    std::string error;
    if(!s.StartAuditionAt(s.ActiveAudition(), s.position, error))
    {
        s.Error(error);
        return;
    }
    s.restartAudition = false;
    s.outputStarted = true;
    if(s.transport)
        BSML::Lite::SetButtonText(s.transport, "…");
    VideoLibraryMenu::Instance().PauseForSyncAudition();
    // Tick waits for a quarter-second of actual prepared stereo frames;
    // no busy wait and no guessed sleep delays block the menu here.
}
void AudioSyncMenu::Tick()
{
    if(!Settings::Instance().ModEnabled())
    {
        if(IsOpen() || IsBusy())
            Abort();
        return;
    }
    auto& s = *impl_;
    s.player.Tick();
    s.TickLiveSeek();
    if(s.pendingScrollLayoutPasses > 0)
        s.RefreshScrollableLayout();
    if(s.capturing)
        s.capture.Tick(VideoLibraryMenu::Instance().SyncSongClip());
    auto check = [&](auto& operation, auto receive)
    {
        const auto progress = operation.Poll();
        if(!progress)
            return;
        if(operation.Busy())
            return;
        if(auto result = operation.TakeResult(s.generation, s.operationKey))
            receive(result);
        else if(progress->status == OperationStatus::Failed && !progress->error.empty())
        {
            s.Error(progress->error, progress->failure == OperationFailure::Internal);
            operation.Cancel(); // result ownership is consumed below with a terminal latch
        }
        // Failure and cancellation also release the busy state. Restore
        // controls for every terminal outcome, not only successful saves.
        s.Refresh();
    };
    // Terminal results have explicit once-only ownership. See observed
    // generation/status latches: a retained failed operation must not
    // reopen its error popup every Unity frame.
    if(!s.preparation.Busy() && s.preparation.Poll() && !s.preparationHandled)
    {
        s.preparationHandled = true;
        s.capturing = false;
        s.capture.Stop();
        check(s.preparation,
              [&](auto result)
              {
                  s.pair = result;
                  s.bounds = result->bounds;
                  if(s.status)
                  {
                      const auto& previous = s.draft.Candidate().profile.lastAnalysis;
                      s.status->set_text(
                          previous ? Impl::SummaryStatus(*previous)
                                   : "Audio ready. Analyze automatically or switch to Manual "
                                     "Adjustment.");
                  }
                  s.Refresh();
              });
    }
    if(!s.analysis.Busy() && s.analysis.Poll() && !s.analysisHandled)
    {
        s.analysisHandled = true;
        check(
            s.analysis,
            [&](auto result)
            {
                s.proposal = result;
                if(s.status)
                    s.status->set_text(
                        "Analysis complete. Review the result before applying it to the draft.");
                BigScreenLogger.info(
                    "Audio Sync analysis: confidence={}, accepted={}, rejected={}, "
                    "held-out={}, "
                    "residual={:.3f} ms, incompatible={}",
                    static_cast<int>(result->confidence), result->accepted, result->rejected,
                    result->validationCount, result->validationMaxSeconds * 1000,
                    result->incompatible);
                s.Refresh();
                // The result is a decision, not an implicit edit. Present its
                // evidence immediately and let Discard leave timing untouched;
                // Apply moves both timing and the durable summary into the
                // existing unsaved per-map draft.
                s.ShowAnalysisResult();
            });
    }
    if(!s.persistence.Busy() && s.persistence.Poll() && !s.persistenceHandled)
    {
        s.persistenceHandled = true;
        check(s.persistence,
              [&](auto result)
              {
                  if(!s.enableOnly && IsOpen())
                  {
                      s.draft.AcceptSaved(*result);
                      if(s.closingAfterSave)
                          s.Close(false);
                      else
                          s.Refresh();
                  }
                  VideoLibraryMenu::Instance().RefreshAudioSyncState();
              });
        VideoLibraryMenu::Instance().RefreshAudioSyncState();
    }
    if(s.saveWhenReady && !s.Busy())
        s.Save(true);
    // This runs before the waveform paint divider so the 250 ms threshold and
    // three-second minimum are based on real time, not on an every-eighth-frame
    // visualization refresh. Save completion itself was already published
    // above and is never delayed by this presentation-only timer.
    s.UpdateDelayedApplySaveProgress();
    if(!IsOpen())
    {
        if(s.persistence.Busy())
        {
            const auto progress = s.persistence.Poll();
            if(progress && progress->wallSeconds > 1)
                s.ShowProgress();
            if(progress && s.enableProgressText)
                s.enableProgressText->set_text(progress->stage + "...");
            if(progress && s.enableProgressFill)
                s.enableProgressFill->get_transform()
                    .cast<UnityEngine::RectTransform>()
                    ->set_anchoredPosition(
                        {static_cast<float>((std::sin(progress->wallSeconds * 3) + 1) *
                                            (ModalProgressWidth * .4f)),
                         0});
        }
        else if(s.progressShown)
        {
            s.enableProgress->Hide();
            s.progressShown = false;
        }
        return;
    }
    if(s.restartAudition && !s.ActiveAudition().Busy())
    {
        s.restartAudition = false;
        ToggleAudition();
    }
    if(s.outputStarted)
    {
        auto output = s.ActiveAudition().Output();
        if(output->paused.load() &&
           (output->Buffered() >= static_cast<std::size_t>(output->rate / 4) ||
            output->complete.load()))
        {
            s.player.Play(output, VideoLibraryMenu::Instance().PauseForSyncAudition());
            if(s.transport)
                BSML::Lite::SetButtonText(s.transport, "Ⅱ");
        }
        // Drive the picture from Unity's continuously advancing audible
        // playhead. The PCM read counter moves only when Unity asks for its
        // next large streaming block and is not a presentation clock.
        const double audiblePosition = s.CurrentAuditionTime();
        if(!s.pendingSeek && !s.preparedSeek)
            s.position = audiblePosition;
        VideoLibraryMenu::Instance().SyncAuditionClock(audiblePosition,
                                                       s.draft.Candidate().profile.showPicture);
        const auto progress = s.ActiveAudition().Progress();
        if(progress && progress->status == OperationStatus::Failed)
        {
            s.StopAudition();
            s.Error(progress->error, progress->failure == OperationFailure::Internal);
        }
        else if(output->complete.load() && output->Buffered() == 0)
        {
            s.StopAudition();
            if(s.draft.Candidate().profile.loopSelection)
            {
                s.position = s.draft.Candidate().profile.markers.songStart;
                s.restartAudition = true;
            }
        }
    }
    // A zoomed waveform must move smoothly beneath its stationary playhead.
    // Only RectTransforms change here; texture construction and audio analysis
    // remain on workers. Static/full-track views retain the old low-rate paint
    // cadence to avoid unnecessary layout work.
    const bool animateWaveform = s.overviewZoom > 0 && s.outputStarted;
    if(!animateWaveform && ++s.paintDivider % 8)
        return;
    if(!OwnsAudition())
        s.position = VideoLibraryMenu::Instance().SyncSongTime();
    s.PaintOverviews();
    if(s.timeline)
    {
        s.refreshing = true;
        s.timeline->set_Value(s.bounds.songDuration > 0 ? s.position / s.bounds.songDuration
                                                        : 0);
        s.refreshing = false;
    }
    std::optional<OperationProgress> progress;
    if(s.preparation.Busy())
        progress = s.preparation.Poll();
    else if(s.analysis.Busy())
        progress = s.analysis.Poll();
    else if(s.persistence.Busy())
        progress = s.persistence.Poll();
    if(progress && s.progressText)
    {
        s.progressText->set_text(progress->stage +
                                 (progress->fraction
                                      ? fmt::format("\n{:.0f}%", *progress->fraction * 100)
                                      : "\nWorking..."));
        if(s.progressFill)
        {
            auto rect = s.progressFill->get_transform().cast<UnityEngine::RectTransform>();
            rect->set_sizeDelta(
                {static_cast<float>(ModalProgressWidth * progress->fraction.value_or(.2)), 2});
            // A moving segment means no truthful total is available. It
            // must never masquerade as an estimated completion percentage.
            rect->set_anchoredPosition(
                {progress->fraction
                     ? 0.0f
                     : static_cast<float>((std::sin(progress->wallSeconds * 3) + 1) *
                                          (ModalProgressWidth * .4f)),
                 0});
        }
    }
    else if(s.progress && s.progressShown && !s.delayedApplySaveProgress)
    {
        s.progress->Hide();
        s.progressShown = false;
    }
}
} // namespace BigScreen
