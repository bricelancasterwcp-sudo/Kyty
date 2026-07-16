#include "Kyty/Core/Common.h"
#include "Kyty/Core/Threads.h"

#include "Emulator/Common.h"
#include "Emulator/Kernel/Pthread.h"
#include "Emulator/Libs/Errno.h"
#include "Emulator/Libs/Libs.h"
#include "Emulator/Loader/SymbolDatabase.h"

#include <cmath>
#include <cstdlib>

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

struct MouseSlot
{
	bool    opened      = false;
	double  demo_prev_x = 0.0; // last synthetic cursor point (for delta emission)
	double  demo_prev_y = 0.0;
	int64_t demo_tick   = 0;
};

static Core::Mutex g_mouse_mutex;
static MouseSlot   g_mouse[MOUSE_COUNT];

static bool demo_enabled()
{
	static const bool enabled = (getenv("KYTY_MOUSE_DEMO") != nullptr);
	return enabled;
}

// Synthetic mouse motion so a mouse-driven title is demonstrably controllable
// with no physical device attached (headless fc_script). Each mouse traces a
// figure-eight; the second mouse runs in anti-phase so the two arms M.I.C.E.
// binds to mouse 0 / mouse 1 sweep distinctly. Returns the per-read delta.
static void demo_delta(int index, int32_t* dx, int32_t* dy)
{
	auto& slot = g_mouse[index];

	constexpr double kAmplitude = 120.0; // px, pre M.I.C.E. 2x scale
	constexpr double kPeriodSec = 2.5;
	constexpr double kReadSec   = 0.008; // M.I.C.E. polls every ~8ms

	double t     = static_cast<double>(slot.demo_tick++) * kReadSec;
	double phase = (index == 0 ? 0.0 : 3.14159265358979323846); // mouse 1 anti-phase
	double w     = 2.0 * 3.14159265358979323846 / kPeriodSec;

	double x = kAmplitude * std::sin(w * t + phase);
	double y = kAmplitude * 0.5 * std::sin(2.0 * w * t + phase);

	*dx = static_cast<int32_t>(std::lround(x - slot.demo_prev_x));
	*dy = static_cast<int32_t>(std::lround(y - slot.demo_prev_y));

	slot.demo_prev_x = x;
	slot.demo_prev_y = y;
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

	int32_t dx = 0;
	int32_t dy = 0;

	{
		Core::LockGuard lock(g_mouse_mutex);
		if (demo_enabled())
		{
			demo_delta(index, &dx, &dy);
		}
	}

	// Emit a single sample carrying the relative motion since the last read.
	// connected must be true or the guest tears down its mouse reader thread.
	data[0].timestamp = LibKernel::KernelGetProcessTime();
	data[0].connected = true;
	data[0].buttons   = 0;
	data[0].x_axis    = dx;
	data[0].y_axis    = dy;
	data[0].wheel     = 0;
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
