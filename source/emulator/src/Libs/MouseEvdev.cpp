#include "Emulator/Libs/LibMouse.h"

#ifdef KYTY_EMU_ENABLED

#ifdef __linux__

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace Kyty::Libs::Mouse {

namespace {

constexpr int MAX_MICE = 2;

constexpr int BITS_PER_LONG_ = sizeof(long) * 8;

bool test_bit(int bit, const unsigned long* arr)
{
	return ((arr[bit / BITS_PER_LONG_] >> (bit % BITS_PER_LONG_)) & 1UL) != 0;
}

// A device is a mouse if it reports relative X/Y motion and a left button.
bool is_mouse(int fd)
{
	unsigned long ev[(EV_MAX / BITS_PER_LONG_) + 1]  = {};
	unsigned long rel[(REL_MAX / BITS_PER_LONG_) + 1] = {};
	unsigned long key[(KEY_MAX / BITS_PER_LONG_) + 1] = {};

	if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0)
	{
		return false;
	}
	if (!test_bit(EV_REL, ev) || !test_bit(EV_KEY, ev))
	{
		return false;
	}
	if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel) < 0 || !test_bit(REL_X, rel) || !test_bit(REL_Y, rel))
	{
		return false;
	}
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) < 0 || !test_bit(BTN_LEFT, key))
	{
		return false;
	}
	return true;
}

uint32_t map_button(uint16_t code)
{
	switch (code)
	{
		case BTN_LEFT: return MOUSE_BUTTON_LEFT;
		case BTN_RIGHT: return MOUSE_BUTTON_RIGHT;
		case BTN_MIDDLE: return MOUSE_BUTTON_MIDDLE;
		case BTN_SIDE: return MOUSE_BUTTON_SIDE1;
		case BTN_EXTRA: return MOUSE_BUTTON_SIDE2;
		default: return 0;
	}
}

// KYTY_MOUSE_EVDEV=/dev/input/eventA,/dev/input/eventB uses those exact nodes;
// any other value (e.g. "1"/"auto") scans /dev/input for the first mice found.
std::vector<std::string> resolve_device_paths(const char* env)
{
	std::vector<std::string> paths;

	if (std::strchr(env, '/') != nullptr)
	{
		const char* p = env;
		while (*p != '\0' && static_cast<int>(paths.size()) < MAX_MICE)
		{
			const char* comma = std::strchr(p, ',');
			size_t      len   = (comma != nullptr ? static_cast<size_t>(comma - p) : std::strlen(p));
			if (len > 0)
			{
				paths.emplace_back(p, len);
			}
			if (comma == nullptr)
			{
				break;
			}
			p = comma + 1;
		}
		return paths;
	}

	// Auto-enumerate.
	DIR* dir = opendir("/dev/input");
	if (dir == nullptr)
	{
		return paths;
	}
	// event nodes are named eventN; scan in numeric-ish order.
	for (int i = 0; i < 256 && static_cast<int>(paths.size()) < MAX_MICE; i++)
	{
		std::string path = "/dev/input/event" + std::to_string(i);
		int         fd   = open(path.c_str(), O_RDONLY | O_NONBLOCK);
		if (fd < 0)
		{
			continue;
		}
		if (is_mouse(fd))
		{
			paths.push_back(path);
		}
		close(fd);
	}
	closedir(dir);
	return paths;
}

void reader_loop(std::vector<int> fds)
{
	std::vector<int32_t> dx(fds.size(), 0);
	std::vector<int32_t> dy(fds.size(), 0);
	std::vector<int32_t> wheel(fds.size(), 0);

	std::vector<pollfd> pfds(fds.size());
	for (size_t i = 0; i < fds.size(); i++)
	{
		pfds[i].fd     = fds[i];
		pfds[i].events = POLLIN;
	}

	while (true)
	{
		int ready = poll(pfds.data(), pfds.size(), -1);
		if (ready < 0)
		{
			continue;
		}

		for (size_t i = 0; i < pfds.size(); i++)
		{
			if ((pfds[i].revents & POLLIN) == 0)
			{
				continue;
			}

			input_event ev {};
			ssize_t     n = 0;
			while ((n = read(pfds[i].fd, &ev, sizeof(ev))) == static_cast<ssize_t>(sizeof(ev)))
			{
				int index = static_cast<int>(i);

				if (ev.type == EV_REL)
				{
					if (ev.code == REL_X)
					{
						dx[i] += ev.value;
					} else if (ev.code == REL_Y)
					{
						dy[i] += ev.value;
					} else if (ev.code == REL_WHEEL)
					{
						wheel[i] += ev.value;
					}
				} else if (ev.type == EV_KEY)
				{
					uint32_t button = map_button(ev.code);
					if (button != 0)
					{
						// ev.value: 1 = press, 0 = release, 2 = autorepeat.
						InjectButton(index, button, ev.value != 0);
					}
				} else if (ev.type == EV_SYN && ev.code == SYN_REPORT)
				{
					if (dx[i] != 0 || dy[i] != 0)
					{
						InjectMotion(index, dx[i], dy[i]);
					}
					if (wheel[i] != 0)
					{
						InjectWheel(index, wheel[i]);
					}
					dx[i]    = 0;
					dy[i]    = 0;
					wheel[i] = 0;
				}
			}
		}
	}
}

} // namespace

void EvdevStart()
{
	static bool started = false;
	if (started)
	{
		return;
	}

	const char* env = getenv("KYTY_MOUSE_EVDEV");
	if (env == nullptr)
	{
		return;
	}
	started = true;

	std::vector<std::string> paths = resolve_device_paths(env);

	std::vector<int> fds;
	for (const auto& path: paths)
	{
		int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
		if (fd < 0)
		{
			printf("KYTY_MOUSE_EVDEV: cannot open %s (need read access to /dev/input)\n", path.c_str());
			continue;
		}
		// Grab for exclusive use so the desktop pointer does not also move.
		ioctl(fd, EVIOCGRAB, 1);
		fds.push_back(fd);
		printf("KYTY_MOUSE_EVDEV: bound %s -> emulated mouse %d\n", path.c_str(), static_cast<int>(fds.size()) - 1);
	}

	if (fds.empty())
	{
		printf("KYTY_MOUSE_EVDEV: no mice bound\n");
		return;
	}

	std::thread(reader_loop, std::move(fds)).detach();
}

} // namespace Kyty::Libs::Mouse

#else // __linux__

namespace Kyty::Libs::Mouse {
void EvdevStart() {}
} // namespace Kyty::Libs::Mouse

#endif // __linux__

#endif // KYTY_EMU_ENABLED
