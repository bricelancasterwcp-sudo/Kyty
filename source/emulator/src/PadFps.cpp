#include "Emulator/PadFps.h"

#include "Kyty/Core/Common.h"
#include "Kyty/Core/Threads.h"

#include "Emulator/Controller.h"

#include "SDL_keycode.h"

#include <cstdlib>

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs::PadFps {

// Host mouse input accumulated between presented frames; drained by FrameTick.
struct FpsState
{
	bool pad_connected = false;
	int  acc_dx        = 0;
	int  acc_dy        = 0;
	int  acc_wheel     = 0;
};

static Core::Mutex g_fps_mutex;
static FpsState    g_fps;

bool Enabled()
{
	static const bool enabled = (getenv("KYTY_PAD_FPS") != nullptr);
	return enabled;
}

void EnsureVirtualPadConnected()
{
	{
		Core::LockGuard lock(g_fps_mutex);
		if (g_fps.pad_connected)
		{
			return;
		}
		g_fps.pad_connected = true;
	}
	Controller::ControllerConnect(VIRTUAL_PAD_ID);
}

static float Sensitivity()
{
	static const float sens = []()
	{
		const char* env = getenv("KYTY_PAD_FPS_SENS");
		float       v   = (env != nullptr ? strtof(env, nullptr) : 0.0f);
		return (v > 0.0f ? v : 4.0f);
	}();
	return sens;
}

bool HandleKey(int key_code, bool down, bool repeat)
{
	if (!Enabled())
	{
		return false;
	}

	uint32_t button = 0;
	switch (key_code)
	{
		case SDLK_w: button = Controller::PAD_BUTTON_UP; break;
		case SDLK_s: button = Controller::PAD_BUTTON_DOWN; break;
		case SDLK_a: button = Controller::PAD_BUTTON_LEFT; break;
		case SDLK_d: button = Controller::PAD_BUTTON_RIGHT; break;
		case SDLK_SPACE: button = Controller::PAD_BUTTON_CIRCLE; break;
		case SDLK_e: button = Controller::PAD_BUTTON_SQUARE; break;
		case SDLK_t: button = Controller::PAD_BUTTON_TRIANGLE; break;
		case SDLK_ESCAPE: button = Controller::PAD_BUTTON_OPTIONS; break;
		default: return false;
	}

	// key repeats don't change pad state, but the key stays consumed
	if (!repeat)
	{
		EnsureVirtualPadConnected();
		Controller::ControllerButton(VIRTUAL_PAD_ID, button, down);
	}

	return true;
}

bool HandleMouseButton(bool left, bool right, bool down)
{
	if (!Enabled() || (!left && !right))
	{
		return false;
	}

	EnsureVirtualPadConnected();

	// R1 = dig/delete (LMB), L1 = place (RMB) - matches ClassiCube-style binds
	uint32_t button = (left ? Controller::PAD_BUTTON_R1 : Controller::PAD_BUTTON_L1);
	Controller::ControllerButton(VIRTUAL_PAD_ID, button, down);

	return true;
}

void HandleMouseMotion(int dx, int dy)
{
	if (!Enabled())
	{
		return;
	}

	// cap the accumulator so a starved FrameTick can't drive it toward signed
	// overflow; anything past this saturates the stick for many frames anyway
	constexpr int ACC_MAX = 1 << 20;
	auto          cap     = [](int v) { return (v > ACC_MAX ? ACC_MAX : (v < -ACC_MAX ? -ACC_MAX : v)); };

	Core::LockGuard lock(g_fps_mutex);
	g_fps.acc_dx = cap(g_fps.acc_dx + dx);
	g_fps.acc_dy = cap(g_fps.acc_dy + dy);
}

void HandleWheel(int dy)
{
	if (!Enabled())
	{
		return;
	}

	Core::LockGuard lock(g_fps_mutex);
	g_fps.acc_wheel += dy;
}

void FrameTick()
{
	if (!Enabled())
	{
		return;
	}

	int dx    = 0;
	int dy    = 0;
	int wheel = 0;
	{
		Core::LockGuard lock(g_fps_mutex);
		dx              = g_fps.acc_dx;
		dy              = g_fps.acc_dy;
		wheel           = g_fps.acc_wheel;
		g_fps.acc_dx    = 0;
		g_fps.acc_dy    = 0;
		g_fps.acc_wheel = 0;
	}

	auto deflect = [](int delta)
	{
		// clamp in float space - an out-of-range float->int cast is UB, and the
		// accumulator is unbounded if FrameTick was starved while input flowed
		float f = static_cast<float>(delta) * Sensitivity();
		if (f > 127.0f)
		{
			return 255;
		}
		if (f < -128.0f)
		{
			return 0;
		}
		return 128 + static_cast<int>(f);
	};

	// this frame's deltas become this frame's stick deflection; an idle frame
	// recenters to 128 (injected once - identical states aren't repeated)
	static int last_x = 128;
	static int last_y = 128;

	int x = deflect(dx);
	int y = deflect(dy);

	if (x != last_x || y != last_y)
	{
		EnsureVirtualPadConnected();
		Controller::ControllerAxis(VIRTUAL_PAD_ID, Controller::Axis::RightX, x);
		Controller::ControllerAxis(VIRTUAL_PAD_ID, Controller::Axis::RightY, y);
		last_x = x;
		last_y = y;
	}

	// wheel = one-frame trigger pulse: release last frame's pulse first, then
	// press for this one. Fast same-direction scrolling can merge into one
	// guest-visible press (release and re-press land between two guest reads).
	static auto pulse_release = Controller::Axis::AxisMax;

	if (pulse_release != Controller::Axis::AxisMax)
	{
		Controller::ControllerAxis(VIRTUAL_PAD_ID, pulse_release, 0);
		pulse_release = Controller::Axis::AxisMax;
	}

	if (wheel != 0)
	{
		EnsureVirtualPadConnected();
		// wheel up = L2 (hotbar left), wheel down = R2 (hotbar right)
		auto trigger = (wheel > 0 ? Controller::Axis::TriggerLeft : Controller::Axis::TriggerRight);
		Controller::ControllerAxis(VIRTUAL_PAD_ID, trigger, 255);
		pulse_release = trigger;
	}
}

} // namespace Kyty::Libs::PadFps

#endif // KYTY_EMU_ENABLED
