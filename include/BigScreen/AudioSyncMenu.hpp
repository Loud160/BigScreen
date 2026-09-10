// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
// Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once
#include "BigScreen/VideoLibrary.hpp"
#include <functional>
#include <memory>
namespace HMUI
{
class ViewController;
}
namespace UnityEngine
{
class AudioClip;
class AudioSource;
} // namespace UnityEngine
namespace BigScreen
{
/// Retained center workspace. Workers receive immutable native snapshots;
/// no callback holds a BeatmapLevel or a Unity control across an await.
/// UI completion is consumed only by Tick for the matching map generation.
class AudioSyncMenu final
{
  public:
    static AudioSyncMenu& Instance();
    bool CreateUi(HMUI::ViewController* center, HMUI::ViewController* editor,
                  std::function<void(bool)> navigate);
    void ForgetUi();
    void SetEnabled(VideoDescriptor descriptor, bool enabled);
    void Open(VideoDescriptor descriptor, std::filesystem::path songDirectory);
    void RequestClose();
    // Returns false while navigation is blocked or awaiting confirmation.
    // Only used outside the editor; an open draft has its Save/Discard flow.
    bool ConfirmNavigation(std::function<void()> leave);
    void Abort();
    void Tick();
    bool IsOpen() const;
    bool IsBusy() const;
    bool OwnsAudition() const;
    bool Ready() const;
    const AudioSync::Profile* PreviewProfile() const;
    void ToggleAudition();
    void Seek(double seconds);

  private:
    AudioSyncMenu();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace BigScreen
