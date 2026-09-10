// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "UnityEngine/EventSystems/IDragHandler.hpp"
#include "UnityEngine/EventSystems/IEndDragHandler.hpp"
#include "UnityEngine/EventSystems/IEventSystemHandler.hpp"
#include "UnityEngine/EventSystems/IInitializePotentialDragHandler.hpp"
#include "UnityEngine/EventSystems/IPointerDownHandler.hpp"
#include "UnityEngine/EventSystems/PointerEventData.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "custom-types/shared/macros.hpp"
#include <functional>
#include <optional>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif
DECLARE_CLASS_CODEGEN_INTERFACES(
    BigScreen,
    WaveformScrubber,
    UnityEngine::MonoBehaviour,
    UnityEngine::EventSystems::IEventSystemHandler*,
    UnityEngine::EventSystems::IPointerDownHandler*,
    UnityEngine::EventSystems::IInitializePotentialDragHandler*,
    UnityEngine::EventSystems::IDragHandler*,
    UnityEngine::EventSystems::IEndDragHandler*) {
    DECLARE_DEFAULT_CTOR();

    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        OnPointerDown,
        &UnityEngine::EventSystems::IPointerDownHandler::OnPointerDown,
        UnityEngine::EventSystems::PointerEventData* eventData);
    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        OnInitializePotentialDrag,
        &UnityEngine::EventSystems::IInitializePotentialDragHandler::OnInitializePotentialDrag,
        UnityEngine::EventSystems::PointerEventData* eventData);
    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        OnDrag,
        &UnityEngine::EventSystems::IDragHandler::OnDrag,
        UnityEngine::EventSystems::PointerEventData* eventData);
    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        OnEndDrag,
        &UnityEngine::EventSystems::IEndDragHandler::OnEndDrag,
        UnityEngine::EventSystems::PointerEventData* eventData);

  public:
    /// The waveform is a clipped, movable full-track image. In the unzoomed
    /// view, pointer X maps directly to the full song. In a zoomed view, a
    /// drag moves the audio time beneath the stationary yellow playhead. When
    /// point matching is active, the same component gives the narrow green/red
    /// marker lines a controller-friendly hit area and reports marker drags in
    /// source-timeline coordinates. Keeping these translations here avoids
    /// rebuilding textures or running analysis on Unity's UI thread.
    void Configure(UnityEngine::RectTransform* viewport,
                   std::function<double()> currentSeconds,
                   std::function<double()> durationSeconds,
                   std::function<double()> visibleSeconds,
                   std::function<void(double)> seek,
                   std::function<double()> startMarkerSeconds = {},
                   std::function<double()> endMarkerSeconds = {},
                   std::function<void(double, bool)> setStartMarker = {},
                   std::function<void(double, bool)> setEndMarker = {},
                   std::function<bool()> markersEditable = {});

  private:
    enum class DragTarget
    {
        None,
        Playhead,
        StartMarker,
        EndMarker
    };

    std::optional<float> PointerX(UnityEngine::EventSystems::PointerEventData* eventData) const;
    void SeekAtPointer(UnityEngine::EventSystems::PointerEventData* eventData);
    std::optional<double> TimeAtPointer(
        UnityEngine::EventSystems::PointerEventData* eventData) const;
    float MarkerX(double seconds) const;
    DragTarget MarkerAtPointer(
        UnityEngine::EventSystems::PointerEventData* eventData) const;
    void SetMarkerAtPointer(UnityEngine::EventSystems::PointerEventData* eventData,
                            bool finished);

    UnityEngine::RectTransform* viewport_ = nullptr;
    std::function<double()> currentSeconds_;
    std::function<double()> durationSeconds_;
    std::function<double()> visibleSeconds_;
    std::function<void(double)> seek_;
    std::function<double()> startMarkerSeconds_;
    std::function<double()> endMarkerSeconds_;
    std::function<void(double, bool)> setStartMarker_;
    std::function<void(double, bool)> setEndMarker_;
    std::function<bool()> markersEditable_;
    double dragStartSeconds_ = 0.0;
    float dragStartX_ = 0.0f;
    double lastMarkerSeconds_ = 0.0;
    DragTarget dragTarget_ = DragTarget::None;
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
