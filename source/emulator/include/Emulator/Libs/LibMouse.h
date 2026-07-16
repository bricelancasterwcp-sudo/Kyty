#ifndef EMULATOR_INCLUDE_EMULATOR_LIBS_LIBMOUSE_H_
#define EMULATOR_INCLUDE_EMULATOR_LIBS_LIBMOUSE_H_

#include "Kyty/Core/Common.h"

#include "Emulator/Common.h"

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs::Mouse {

// OrbisMouseData button bits (Left is the primary; SwapPrimarySecondary in guest
// code may reinterpret per-device).
constexpr uint32_t MOUSE_BUTTON_LEFT   = 0x01;
constexpr uint32_t MOUSE_BUTTON_RIGHT  = 0x02;
constexpr uint32_t MOUSE_BUTTON_MIDDLE = 0x04;
constexpr uint32_t MOUSE_BUTTON_SIDE1  = 0x08;
constexpr uint32_t MOUSE_BUTTON_SIDE2  = 0x10;

// Feed host input into an emulated mouse (index 0/1). Motion and wheel are
// relative deltas accumulated until the guest's next sceMouseRead; button state
// is held. Thread-safe.
void InjectMotion(int index, int dx, int dy);
void InjectButton(int index, uint32_t button, bool down);
void InjectWheel(int index, int wheel);

} // namespace Kyty::Libs::Mouse

#endif // KYTY_EMU_ENABLED

#endif /* EMULATOR_INCLUDE_EMULATOR_LIBS_LIBMOUSE_H_ */
