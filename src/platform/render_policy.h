#pragma once

/// Superseded-frame compositor skip (GitHub issue #9 / Task 63).
///
/// A frontend tick may run SEVERAL emulator frames back-to-back (audio-pacing
/// catch-up doubles, fastload bursts) but pushes the framebuffer to the
/// display exactly ONCE, after the last of them. Every earlier frame's
/// end-of-frame compositor pass writes an image the last frame overwrites
/// before any paint can serve it — the `superseded` bucket of the cadence
/// report (present_cadence.h). That pass is pure waste, and on a host with
/// no headroom it is exactly the work that pushes a catch-up double tick to
/// twice the frame budget (issue #9: ~21 wasted passes/s on the reporter's
/// machine). So all but the tick's final frame run with
/// Emulator::set_render_enabled(false).
///
/// The decision lives here, pure and unit-tested (render_policy_test); the
/// QtApp/SdlApp loops consume this helper verbatim. Consumers that need
/// every frame composited (video recording, active debugger) are re-forced
/// inside Emulator::run_frame() and never see the hint. A tick with a due
/// --delayed-screenshot composites in full (`force_all`): the captured frame
/// must have gone through the compositor with the layer mask armed.
#include <cstdint>

namespace render_policy {

/// Should frame `frame_index` (0-based) of a presenting tick that runs
/// `frames_in_tick` frames go through the compositor? Only the last frame's
/// image can reach the screen — the frontend presents once, after its frame
/// loops — so only the last composites. `force_all` (screenshot due this
/// tick) composites every frame. The final frame of a tick composites for
/// every `frames_in_tick`, including the degenerate <= 1 cases.
constexpr bool composite_frame_in_tick(int frame_index, int frames_in_tick,
                                       bool force_all)
{
    return force_all || frame_index >= frames_in_tick - 1;
}

/// GH #428 — `last_uploaded` value meaning "nothing uploaded yet".
constexpr uint64_t NOTHING_UPLOADED = ~uint64_t{0};

/// Does the display need the framebuffer pushed again? A paused machine
/// re-presented an unchanged 640x256 frame fifty times a second (copy,
/// software prescale, repaint / GPU upload). The frame only needs to go out
/// when the emulator wrote it (`generation` differs from the generation last
/// uploaded), when the target it is drawn into changed (size, scale,
/// fullscreen, pixel ratio, an exposed window: `target_changed`), or when
/// nothing has been uploaded yet. Used by EmulatorWidget (Qt) and SdlApp::tick.
constexpr bool upload_needed(uint64_t generation, uint64_t last_uploaded,
                             bool target_changed)
{
    return last_uploaded == NOTHING_UPLOADED || generation != last_uploaded ||
           target_changed;
}

}  // namespace render_policy
