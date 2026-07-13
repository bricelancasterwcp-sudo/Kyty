#ifndef EMULATOR_INCLUDE_EMULATOR_KEYBOARD_H_
#define EMULATOR_INCLUDE_EMULATOR_KEYBOARD_H_

#include "Kyty/Core/Common.h"

#include "Emulator/Common.h"

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs::Keyboard {

struct KeyboardData;
struct KeyboardKey2Char;

int KYTY_SYSV_ABI KeyboardInit();
int KYTY_SYSV_ABI KeyboardOpen(int user_id, int type, int index, const void* param);
int KYTY_SYSV_ABI KeyboardClose(int handle);
int KYTY_SYSV_ABI KeyboardReadState(int handle, KeyboardData* data);
int KYTY_SYSV_ABI KeyboardGetKey2Char(int handle, int unknown, uint32_t locks, uint32_t mods, uint32_t keycode, KeyboardKey2Char* out);

// Called from the window thread's SDL event loop
void KeyboardHandleEvent(int scan_code, bool down, uint16_t sdl_mod);

} // namespace Kyty::Libs::Keyboard

#endif // KYTY_EMU_ENABLED

#endif /* EMULATOR_INCLUDE_EMULATOR_KEYBOARD_H_ */
