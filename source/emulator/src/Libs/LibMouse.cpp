#include "Kyty/Core/Common.h"
#include "Kyty/Core/Threads.h"

#include "Emulator/Common.h"
#include "Emulator/Kernel/Pthread.h"
#include "Emulator/Libs/Errno.h"
#include "Emulator/Libs/LibMouse.h"
#include "Emulator/Libs/Libs.h"
#include "Emulator/Loader/SymbolDatabase.h"

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs {

LIB_VERSION("Mouse", 1, "Mouse", 1, 1);

namespace Mouse {

LIB_NAME("Mouse", "Mouse");

constexpr int32_t ORBIS_MOUSE_ERROR_INVALID_HANDLE = 0x80DF0003;

// Layout must match the guest's OrbisMouseData exactly (see homebrew mouse.h):
// x_axis / y_axis carry RELATIVE motion, buttons is a bitmask (Left=1, Right=2...).
struct MouseData
{
	uint64_t timestamp;
	bool     connected;
	uint32_t buttons;
	int32_t  x_axis;
	int32_t  y_axis;
	int32_t  wheel;
	int32_t  tilt;
	uint8_t  reserve[8];
};

constexpr int MOUSE_COUNT = 2;

// Host input accumulated between guest reads. Motion/wheel are relative and drain
// on each read; button state is a held bitmask.
struct MouseSlot
{
	bool     opened   = false;
	int32_t  acc_dx   = 0;
	int32_t  acc_dy   = 0;
	int32_t  acc_wheel = 0;
	uint32_t buttons  = 0;
};

static Core::Mutex g_mouse_mutex;
static MouseSlot   g_mouse[MOUSE_COUNT];

void InjectMotion(int index, int dx, int dy)
{
	if (index < 0 || index >= MOUSE_COUNT)
	{
		return;
	}
	Core::LockGuard lock(g_mouse_mutex);
	g_mouse[index].acc_dx += dx;
	g_mouse[index].acc_dy += dy;
}

void InjectButton(int index, uint32_t button, bool down)
{
	if (index < 0 || index >= MOUSE_COUNT)
	{
		return;
	}
	Core::LockGuard lock(g_mouse_mutex);
	if (down)
	{
		g_mouse[index].buttons |= button;
	} else
	{
		g_mouse[index].buttons &= ~button;
	}
}

void InjectWheel(int index, int wheel)
{
	if (index < 0 || index >= MOUSE_COUNT)
	{
		return;
	}
	Core::LockGuard lock(g_mouse_mutex);
	g_mouse[index].acc_wheel += wheel;
}

int KYTY_SYSV_ABI MouseInit()
{
	PRINT_NAME();
	return OK;
}

int KYTY_SYSV_ABI MouseOpen(int user_id, int type, int index, const void* param)
{
	PRINT_NAME();

	printf("\t user_id = %d, type = %d, index = %d\n", user_id, type, index);

	EXIT_NOT_IMPLEMENTED(index < 0 || index >= MOUSE_COUNT);

	Core::LockGuard lock(g_mouse_mutex);
	g_mouse[index].opened = true;

	// Handle encodes the mouse index (1-based, mirroring the Pad single-handle model).
	return index + 1;
}

int KYTY_SYSV_ABI MouseClose(int handle)
{
	PRINT_NAME();

	int index = handle - 1;
	EXIT_NOT_IMPLEMENTED(index < 0 || index >= MOUSE_COUNT);

	Core::LockGuard lock(g_mouse_mutex);
	g_mouse[index].opened = false;

	return OK;
}

int KYTY_SYSV_ABI MouseRead(int handle, MouseData* data, int num)
{
	// PRINT_NAME();  // polled every ~8ms per mouse; too noisy

	int index = handle - 1;

	if (index < 0 || index >= MOUSE_COUNT)
	{
		return ORBIS_MOUSE_ERROR_INVALID_HANDLE;
	}

	EXIT_NOT_IMPLEMENTED(data == nullptr);
	EXIT_NOT_IMPLEMENTED(num < 1);

	int32_t  dx      = 0;
	int32_t  dy      = 0;
	int32_t  wheel   = 0;
	uint32_t buttons = 0;

	{
		Core::LockGuard lock(g_mouse_mutex);
		auto&           slot = g_mouse[index];

		dx      = slot.acc_dx;
		dy      = slot.acc_dy;
		wheel   = slot.acc_wheel;
		buttons = slot.buttons;

		// Relative motion is consumed; held buttons persist for the next read.
		slot.acc_dx    = 0;
		slot.acc_dy    = 0;
		slot.acc_wheel = 0;
	}

	// Emit a single sample carrying the motion since the last read. connected must
	// be true or the guest tears down its mouse reader thread.
	data[0].timestamp = LibKernel::KernelGetProcessTime();
	data[0].connected = true;
	data[0].buttons   = buttons;
	data[0].x_axis    = dx;
	data[0].y_axis    = dy;
	data[0].wheel     = wheel;
	data[0].tilt      = 0;
	for (unsigned char& b: data[0].reserve)
	{
		b = 0;
	}

	return 1;
}

} // namespace Mouse

LIB_DEFINE(InitMouse_1)
{
	PRINT_NAME_ENABLE(true);

	LIB_FUNC("Qs0wWulgl7U", Mouse::MouseInit);  // sceMouseInit
	LIB_FUNC("RaqxZIf6DvE", Mouse::MouseOpen);  // sceMouseOpen
	LIB_FUNC("cAnT0Rw-IwU", Mouse::MouseClose); // sceMouseClose
	LIB_FUNC("x8qnXqh-tiM", Mouse::MouseRead);  // sceMouseRead
}

} // namespace Kyty::Libs

#endif // KYTY_EMU_ENABLED
