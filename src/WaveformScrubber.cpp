// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/WaveformScrubber.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

DEFINE_TYPE(BigScreen, WaveformScrubber);

namespace BigScreen
{
void WaveformScrubber::Configure(UnityEngine::RectTransform* viewport,
                                 std::function<double()> currentSeconds,
                                 std::function<double()> durationSeconds,
                                 std::function<double()> visibleSeconds,
                                 std::function<void(double)> seek,
                                 std::function<double()> startMarkerSeconds,
                                 std::function<double()> endMarkerSeconds,
                                 std::function<void(double, bool)> setStartMarker,
                                 std::function<void(double, bool)> setEndMarker,
                                 std::function<bool()> markersEditable)
{
    viewport_ = viewport;
    currentSeconds_ = std::move(currentSeconds);
    durationSeconds_ = std::move(durationSeconds);
    visibleSeconds_ = std::move(visibleSeconds);
    seek_ = std::move(seek);
    startMarkerSeconds_ = std::move(startMarkerSeconds);
    endMarkerSeconds_ = std::move(endMarkerSeconds);
    setStartMarker_ = std::move(setStartMarker);
    setEndMarker_ = std::move(setEndMarker);
    markersEditable_ = std::move(markersEditable);
}

std::optional<float>
WaveformScrubber::PointerX(UnityEngine::EventSystems::PointerEventData* eventData) const
{
    if(!viewport_ || !eventData || !eventData->pointerCurrentRaycast.isValid)
        return std::nullopt;
    const auto world = eventData->pointerCurrentRaycast.worldPosition;
    const auto local = viewport_->InverseTransformPoint(world);
    return local.x;
}

void WaveformScrubber::SeekAtPointer(
    UnityEngine::EventSystems::PointerEventData* eventData)
{
    const auto x = PointerX(eventData);
    if(!x || !seek_ || !durationSeconds_ || !visibleSeconds_ || !currentSeconds_)
        return;
    const double duration = durationSeconds_();
    // Read Rect's generated value field directly. Calling Rect::get_width()
    // introduces a non-exported Unity value-type accessor on Quest 1.40.8 and
    // fails the mod's strict --no-undefined native link.
    const double width = viewport_ ? viewport_->get_rect().m_Width : 0.0;
    if(!std::isfinite(duration) || duration <= 0 || !std::isfinite(width) || width <= 0)
        return;
    const double visible = std::clamp(visibleSeconds_(), 0.001, duration);

    double target = 0.0;
    if(visible >= duration - .001)
    {
        // Full-track mode behaves like an ordinary timeline: the pointer's
        // absolute position identifies the requested song time.
        target = ((*x / width) + .5) * duration;
    }
    else if(dragTarget_ == DragTarget::Playhead)
    {
        // Zoomed mode behaves like an editor timeline. The yellow playhead
        // remains fixed and the waveform moves beneath it, so pointer motion
        // represents a time delta within the currently visible window.
        target = dragStartSeconds_ + (*x - dragStartX_) / width * visible;
    }
    else
    {
        // A direct press elsewhere in the zoomed window seeks relative to the
        // current center; subsequent drag callbacks use the stable origin set
        // below instead of accumulating rounding error from each UI frame.
        target = currentSeconds_() + *x / width * visible;
    }
    seek_(std::clamp(target, 0.0, duration));
}

std::optional<double> WaveformScrubber::TimeAtPointer(
    UnityEngine::EventSystems::PointerEventData* eventData) const
{
    const auto x = PointerX(eventData);
    if(!x || !durationSeconds_ || !visibleSeconds_ || !currentSeconds_ || !viewport_)
        return std::nullopt;
    const double duration = durationSeconds_();
    const double width = viewport_->get_rect().m_Width;
    if(!std::isfinite(duration) || duration <= 0 || !std::isfinite(width) || width <= 0)
        return std::nullopt;
    const double visible = std::clamp(visibleSeconds_(), 0.001, duration);
    const double value = visible >= duration - .001
        ? ((*x / width) + .5) * duration
        : currentSeconds_() + *x / width * visible;
    return std::clamp(value, 0.0, duration);
}

float WaveformScrubber::MarkerX(double seconds) const
{
    if(!viewport_ || !durationSeconds_ || !visibleSeconds_ || !currentSeconds_)
        return std::numeric_limits<float>::infinity();
    const double duration = durationSeconds_();
    const double width = viewport_->get_rect().m_Width;
    if(!std::isfinite(seconds) || !std::isfinite(duration) || duration <= 0 ||
       !std::isfinite(width) || width <= 0)
        return std::numeric_limits<float>::infinity();
    const double visible = std::clamp(visibleSeconds_(), 0.001, duration);
    return static_cast<float>(
        visible >= duration - .001
            ? (seconds / duration - .5) * width
            : (seconds - currentSeconds_()) / visible * width);
}

WaveformScrubber::DragTarget WaveformScrubber::MarkerAtPointer(
    UnityEngine::EventSystems::PointerEventData* eventData) const
{
    if(!markersEditable_ || !markersEditable_() || !startMarkerSeconds_ ||
       !endMarkerSeconds_ || !setStartMarker_ || !setEndMarker_)
        return DragTarget::None;
    const auto x = PointerX(eventData);
    if(!x || !viewport_)
        return DragTarget::None;

    // The visible line is deliberately narrow for timing precision, but a
    // controller ray needs a larger acquisition target. Selection is resolved
    // by proximity inside the waveform's parent raycast, so no invisible UI
    // object can sit over the graph and steal normal seeking gestures.
    constexpr float GrabRadius = 2.0f;
    const float startDistance = std::abs(*x - MarkerX(startMarkerSeconds_()));
    const float endDistance = std::abs(*x - MarkerX(endMarkerSeconds_()));
    if(std::min(startDistance, endDistance) > GrabRadius)
        return DragTarget::None;
    return startDistance <= endDistance ? DragTarget::StartMarker
                                        : DragTarget::EndMarker;
}

void WaveformScrubber::SetMarkerAtPointer(
    UnityEngine::EventSystems::PointerEventData* eventData,
    bool finished)
{
    if(const auto value = TimeAtPointer(eventData))
        lastMarkerSeconds_ = *value;
    if(dragTarget_ == DragTarget::StartMarker && setStartMarker_)
        setStartMarker_(lastMarkerSeconds_, finished);
    else if(dragTarget_ == DragTarget::EndMarker && setEndMarker_)
        setEndMarker_(lastMarkerSeconds_, finished);
}

void WaveformScrubber::OnPointerDown(
    UnityEngine::EventSystems::PointerEventData* eventData)
{
    dragTarget_ = MarkerAtPointer(eventData);
    if(dragTarget_ == DragTarget::StartMarker ||
       dragTarget_ == DragTarget::EndMarker)
    {
        lastMarkerSeconds_ = dragTarget_ == DragTarget::StartMarker
            ? startMarkerSeconds_()
            : endMarkerSeconds_();
        return;
    }
    dragTarget_ = DragTarget::None;
    SeekAtPointer(eventData);
    if(const auto x = PointerX(eventData))
    {
        dragStartX_ = *x;
        dragStartSeconds_ = currentSeconds_ ? currentSeconds_() : 0.0;
        dragTarget_ = DragTarget::Playhead;
    }
}

void WaveformScrubber::OnInitializePotentialDrag(
    UnityEngine::EventSystems::PointerEventData* eventData)
{
    if(eventData)
        eventData->useDragThreshold = false;
    if(dragTarget_ == DragTarget::StartMarker ||
       dragTarget_ == DragTarget::EndMarker)
        return;
    if(const auto x = PointerX(eventData))
    {
        dragStartX_ = *x;
        dragStartSeconds_ = currentSeconds_ ? currentSeconds_() : 0.0;
        dragTarget_ = DragTarget::Playhead;
    }
}

void WaveformScrubber::OnDrag(UnityEngine::EventSystems::PointerEventData* eventData)
{
    if(dragTarget_ == DragTarget::StartMarker ||
       dragTarget_ == DragTarget::EndMarker)
        SetMarkerAtPointer(eventData, false);
    else
        SeekAtPointer(eventData);
}

void WaveformScrubber::OnEndDrag(UnityEngine::EventSystems::PointerEventData* eventData)
{
    if(dragTarget_ == DragTarget::StartMarker ||
       dragTarget_ == DragTarget::EndMarker)
        SetMarkerAtPointer(eventData, true);
    dragTarget_ = DragTarget::None;
}
} // namespace BigScreen
