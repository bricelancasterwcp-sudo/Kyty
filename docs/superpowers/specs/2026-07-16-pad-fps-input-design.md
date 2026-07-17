# Keyboard/Mouse FPS Input Profile → ScePad

**Date:** 2026-07-16
**Status:** Approved
**Goal:** Make pad-polling titles (immediate target: ClassiCube) playable from the host
keyboard and mouse, with no physical controller attached.

## Background

Kyty's pad stack is already complete on the HLE side:

- `Controller.cpp` implements `scePadInit/Open/Read/ReadState` over a thread-safe
  state buffer with an injection API (`ControllerConnect/ControllerButton/ControllerAxis`).
- `Window.cpp` already routes physical SDL game controllers (buttons + all 6 axes) and a
  small keyboard→button map (arrows = D-pad, X/C/Z/S = face buttons, A/D = L1/R1,
  TAB = options) into a lazily-connected virtual pad (`KEYBOARD_PAD_ID`).

What is missing is any way to produce **analog axes** without a controller. ClassiCube
binds camera-look to the right stick, so the world is reachable but not playable.
ClassiCube's other binds (from `defaults_ps4` in the guest port): movement = D-pad,
place/delete block = L1/R1, jump = circle, inventory = square, chat = triangle,
menu = OPTIONS.

## Design

### New module: `PadFps` (env-gated FPS input profile)

Files: `source/emulator/src/PadFps.cpp`, `source/emulator/include/Emulator/PadFps.h`
(mirrors the `MouseEvdev.cpp` precedent — a focused translation unit; `Window.cpp`
stays a thin router).

Interface (namespace `Kyty::Libs::PadFps`):

| Function | Behavior |
|---|---|
| `Enabled()` | true iff env `KYTY_PAD_FPS=1` (read once) |
| `HandleKey(key_code, down) → bool` | W/A/S/D → D-pad up/left/down/right; Space → circle (jump); E → square (inventory); T → triangle (chat); Esc → OPTIONS. Injects via `ControllerButton(KEYBOARD_PAD_ID, …)`; returns true when consumed |
| `HandleMouseButton(left/right, down) → bool` | LMB → R1 (delete block), RMB → L1 (place block); returns true when consumed |
| `HandleMouseMotion(dx, dy)` | accumulates raw deltas (atomic accumulator, same pattern as LibMouse host input) |
| `FrameTick()` | drains the accumulator, converts to right-stick deflection, injects `ControllerAxis(RightX/RightY)`; recenters to 128 on motionless frames |

Mouse→stick conversion: `deflection = 128 + clamp(delta * sens, -127, 127)` per axis,
where `sens` comes from `KYTY_PAD_FPS_SENS` (default 4.0). The default is sized so a
modest per-frame delta clears ClassiCube's guest-side deadzone of 32 stick units.

### Window.cpp integration

- `game_event_keyboard`: when the profile is enabled, `PadFps::HandleKey` runs first;
  if it consumes the key, the legacy letter map (A=L1/D=R1/S=triangle) is skipped for
  that key. The Space-pause and Esc-quit emulator conveniences are suppressed while the
  profile is on (Esc becomes the OPTIONS button). Ctrl+Q remains the unconditional quit.
  Arrows/X/C/Z remain mapped for menu navigation.
- `game_event_mouse`: motion/buttons are offered to `PadFps` first when enabled. The
  SceMouse injection path is skipped for consumed events so a title is driven by exactly
  one input model at a time.
- `WindowDrawBuffer`: calls `PadFps::FrameTick()` next to the existing demo ticks
  (present thread; `Controller`'s mutex makes cross-thread injection safe).

### Scripted verification: `KYTY_PAD_FPS_DEMO=1`

A per-frame tick (same shape as `kyty_mouse_demo_tick`) pushes **real SDL events**
(SDL_KEYDOWN/KEYUP for W, SDL_MOUSEMOTION sweeps, SDL_MOUSEBUTTONDOWN) on a fixed
flip-count schedule, exercising the full path:
SDL event → `game_event_*` → PadFps → Controller → `scePadReadState` → guest.
Evidence = frame dumps (`KYTY_DUMP_FRAME`) showing the HUD position text change and the
view rotate. This keeps verification headless and deterministic — no window focus or
hardware needed.

### Guest-side fix (ClassiCube PS4 port)

`Gamepads_Process` currently calls `scePadRead(handle, &data, 1)` and consumes `data`
even when 0 new states are returned — an uninitialized-stack read. Switch to
`scePadReadState`, which always returns the current state. One line.

## Alternatives rejected

- **Always-on remap (no env gate):** would steal A/D/S from the existing letter→button
  map and double-drive titles that read both mouse and pad (e.g. M.I.C.E.).
- **Guest-side sceMouse/sceKeyboard input in the ClassiCube port:** 1:1 mouse feel, but
  ClassiCube-only, more work, and leaves the ScePad path (the stated goal) unexercised.

## Trade-offs / limits

- Mouse→stick emulation maps mouse speed to **turn rate**, not 1:1 aim. Inherent to
  driving a pad API; sensitivity is tunable via `KYTY_PAD_FPS_SENS`.
- Single virtual pad only (`KEYBOARD_PAD_ID`); physical-controller passthrough is
  unchanged and remains the multi-pad path.

## Test plan

1. Build; boot ClassiCube with `KYTY_PAD_FPS=1 KYTY_PAD_FPS_DEMO=1` + frame dumps →
   HUD position changes, view rotates, a block breaks (R1 held).
2. Regression: ClassiCube boot **without** the env vars → behavior identical to `49c9b95`.
3. Regression: Doom boot → keyboard HLE path untouched (profile off by default).

No unit-test framework exists for this interactive layer in Kyty; the scripted
end-to-end run is the verification, consistent with prior features in this fork.
