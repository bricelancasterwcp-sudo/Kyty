#ifndef EMULATOR_INCLUDE_EMULATOR_PADFPS_H_
#define EMULATOR_INCLUDE_EMULATOR_PADFPS_H_

#include "Kyty/Core/Common.h"

#include "Emulator/Common.h"

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs::PadFps {

// The single keyboard/mouse-backed virtual DualShock, shared with Window.cpp's
// always-on keyboard->button map. Connected lazily on first use; connecting the
// same id twice would abort in Controller, so all users go through
// EnsureVirtualPadConnected().
constexpr int VIRTUAL_PAD_ID = 0x6B6579; // 'key'

void EnsureVirtualPadConnected();

// FPS input profile (env KYTY_PAD_FPS): WASD = D-pad, mouse motion = right
// stick (KYTY_PAD_FPS_SENS scales deflection per delta pixel, default 4.0),
// LMB/RMB = R1/L1, Space = circle, E = square, T = triangle, Esc = OPTIONS,
// wheel = one-frame L2/R2 pulse. Handle* return true when the
// event was consumed, so the caller skips its legacy keyboard map / SceMouse
// routing for that event (a title is driven by exactly one input model).
bool Enabled();
bool HandleKey(int key_code, bool down, bool repeat);
bool HandleMouseButton(bool left, bool right, bool down);
void HandleMouseMotion(int dx, int dy);
void HandleWheel(int dy);

// Per presented frame: drains the mouse accumulator into a right-stick
// deflection (recentering on idle frames) and releases the previous frame's
// trigger pulse. Runs on the present thread; injection is thread-safe.
void FrameTick();

} // namespace Kyty::Libs::PadFps

#endif // KYTY_EMU_ENABLED

#endif /* EMULATOR_INCLUDE_EMULATOR_PADFPS_H_ */
