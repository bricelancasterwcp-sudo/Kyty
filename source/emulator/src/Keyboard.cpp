#include "Emulator/Keyboard.h"

#include "Kyty/Core/DbgAssert.h"
#include "Kyty/Core/Threads.h"

#include "Emulator/Kernel/Pthread.h"
#include "Emulator/Libs/Errno.h"
#include "Emulator/Libs/Libs.h"

#include <cstring>

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs::Keyboard {

LIB_NAME("Keyboard", "Keyboard");

constexpr int      KEYCODES_MAX  = 32;
constexpr uint32_t SCANCODES_MAX = 256;

// Guest layout (usb hid usage ids, which sdl scancodes match 1:1)
struct KeyboardData
{
	uint32_t timestamp; // microseconds
	uint8_t  padding[12];
	int32_t  unk1; // always 1
	int32_t  nkeys;
	uint32_t locks; // num lock = 1, caps lock = 2, scroll lock = 4
	uint32_t mods;  // lctrl 1, lshift 2, lalt 4, lmeta 8, rctrl 16, rshift 32, ralt 64, rmeta 128
	uint16_t keycodes[KEYCODES_MAX];
};

struct KeyboardKey2Char
{
	int32_t ok;
	int32_t ok2;
	int32_t keycode;
	char    unk[8];
};

struct KeyboardState
{
	Core::Mutex mutex;
	bool        initialized = false;
	bool        opened      = false;
	bool        keys[SCANCODES_MAX] = {};
	uint32_t    mods  = 0;
	uint32_t    locks = 0;
};

static KeyboardState g_keyboard_state;

// sdl KMOD_* bits -> sce modifier/lock bits
static uint32_t mods_from_sdl(uint16_t sdl_mod)
{
	uint32_t mods = 0;
	mods |= ((sdl_mod & 0x0040u) != 0 ? 0x01u : 0u); // lctrl
	mods |= ((sdl_mod & 0x0001u) != 0 ? 0x02u : 0u); // lshift
	mods |= ((sdl_mod & 0x0100u) != 0 ? 0x04u : 0u); // lalt
	mods |= ((sdl_mod & 0x0400u) != 0 ? 0x08u : 0u); // lmeta
	mods |= ((sdl_mod & 0x0080u) != 0 ? 0x10u : 0u); // rctrl
	mods |= ((sdl_mod & 0x0002u) != 0 ? 0x20u : 0u); // rshift
	mods |= ((sdl_mod & 0x0200u) != 0 ? 0x40u : 0u); // ralt
	mods |= ((sdl_mod & 0x0800u) != 0 ? 0x80u : 0u); // rmeta
	return mods;
}

static uint32_t locks_from_sdl(uint16_t sdl_mod)
{
	uint32_t locks = 0;
	locks |= ((sdl_mod & 0x1000u) != 0 ? 0x01u : 0u); // num
	locks |= ((sdl_mod & 0x2000u) != 0 ? 0x02u : 0u); // caps
	locks |= ((sdl_mod & 0x8000u) != 0 ? 0x04u : 0u); // scroll
	return locks;
}

bool KeyboardIsOpen()
{
	Core::LockGuard lock(g_keyboard_state.mutex);

	return g_keyboard_state.opened;
}

void KeyboardHandleEvent(int scan_code, bool down, uint16_t sdl_mod)
{
	Core::LockGuard lock(g_keyboard_state.mutex);

	if (scan_code >= 0 && static_cast<uint32_t>(scan_code) < SCANCODES_MAX)
	{
		g_keyboard_state.keys[scan_code] = down;
	}

	g_keyboard_state.mods  = mods_from_sdl(sdl_mod);
	g_keyboard_state.locks = locks_from_sdl(sdl_mod);
}

int KYTY_SYSV_ABI KeyboardInit()
{
	PRINT_NAME();

	Core::LockGuard lock(g_keyboard_state.mutex);

	g_keyboard_state.initialized = true;

	return OK;
}

int KYTY_SYSV_ABI KeyboardOpen(int user_id, int type, int index, const void* param)
{
	PRINT_NAME();

	printf("\t user_id = %d\n", user_id);
	printf("\t type    = %d\n", type);
	printf("\t index   = %d\n", index);

	EXIT_NOT_IMPLEMENTED(type != 0);
	EXIT_NOT_IMPLEMENTED(index != 0);
	EXIT_NOT_IMPLEMENTED(param != nullptr);

	Core::LockGuard lock(g_keyboard_state.mutex);

	if (!g_keyboard_state.initialized || g_keyboard_state.opened)
	{
		return -1;
	}

	g_keyboard_state.opened = true;

	return 1;
}

int KYTY_SYSV_ABI KeyboardClose(int handle)
{
	PRINT_NAME();

	Core::LockGuard lock(g_keyboard_state.mutex);

	if (handle != 1 || !g_keyboard_state.opened)
	{
		return -1;
	}

	g_keyboard_state.opened = false;

	return OK;
}

int KYTY_SYSV_ABI KeyboardReadState(int handle, KeyboardData* data)
{
	// PRINT_NAME();

	if (handle != 1 || data == nullptr)
	{
		return -1;
	}

	Core::LockGuard lock(g_keyboard_state.mutex);

	if (!g_keyboard_state.opened)
	{
		return -1;
	}

	std::memset(data, 0, sizeof(KeyboardData));

	data->timestamp = static_cast<uint32_t>(LibKernel::KernelGetProcessTime());
	data->unk1      = 1;
	data->locks     = g_keyboard_state.locks;
	data->mods      = g_keyboard_state.mods;

	int n = 0;
	for (uint32_t scan = 4; scan < SCANCODES_MAX && n < KEYCODES_MAX; scan++)
	{
		if (g_keyboard_state.keys[scan])
		{
			data->keycodes[n++] = static_cast<uint16_t>(scan);
		}
	}
	data->nkeys = n;

	return OK;
}

// us layout, hid usage id -> character
static int key_to_char(uint32_t keycode, uint32_t locks, uint32_t mods)
{
	bool shift = ((mods & (0x02u | 0x20u)) != 0);
	bool caps  = ((locks & 0x02u) != 0);
	bool num   = ((locks & 0x01u) != 0);

	// letters a..z
	if (keycode >= 4 && keycode <= 29)
	{
		bool upper = (shift != caps);
		return (upper ? 'A' : 'a') + static_cast<int>(keycode) - 4;
	}

	// digit row 1..9, 0
	if (keycode >= 30 && keycode <= 39)
	{
		static const char plain[]   = "1234567890";
		static const char shifted[] = "!@#$%^&*()";
		return (shift ? shifted : plain)[keycode - 30];
	}

	switch (keycode)
	{
		case 40: return '\n'; // return
		case 42: return '\b'; // backspace
		case 43: return '\t'; // tab
		case 44: return ' ';  // space
		case 45: return (shift ? '_' : '-');
		case 46: return (shift ? '+' : '=');
		case 47: return (shift ? '{' : '[');
		case 48: return (shift ? '}' : ']');
		case 49: return (shift ? '|' : '\\');
		case 51: return (shift ? ':' : ';');
		case 52: return (shift ? '"' : '\'');
		case 53: return (shift ? '~' : '`');
		case 54: return (shift ? '<' : ',');
		case 55: return (shift ? '>' : '.');
		case 56: return (shift ? '?' : '/');
		case 84: return '/';  // numpad
		case 85: return '*';
		case 86: return '-';
		case 87: return '+';
		case 88: return '\n';
		default: break;
	}

	// numpad digits honor num lock
	if (num && keycode >= 89 && keycode <= 98)
	{
		static const char numpad[] = "1234567890";
		return numpad[keycode - 89];
	}
	if (num && keycode == 99)
	{
		return '.';
	}

	return -1;
}

int KYTY_SYSV_ABI KeyboardGetKey2Char(int handle, int /*unknown*/, uint32_t locks, uint32_t mods, uint32_t keycode,
                                      KeyboardKey2Char* out)
{
	// PRINT_NAME();

	if (handle != 1 || out == nullptr)
	{
		return -1;
	}

	std::memset(out, 0, sizeof(KeyboardKey2Char));

	int ch = key_to_char(keycode, locks, mods);

	if (ch >= 0)
	{
		out->ok      = 1;
		out->keycode = ch;
	}

	return OK;
}

} // namespace Kyty::Libs::Keyboard

#endif // KYTY_EMU_ENABLED
