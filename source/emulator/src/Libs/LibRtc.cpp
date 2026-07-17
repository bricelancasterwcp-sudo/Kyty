#include "Emulator/Common.h"
#include "Emulator/Libs/Errno.h"
#include "Emulator/Libs/Libs.h"
#include "Emulator/Loader/SymbolDatabase.h"

#include <ctime>

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs {

namespace Rtc {

LIB_NAME("Rtc", "Rtc");

// Matches the OpenOrbis TimeTable layout (7 x uint16). The official SDK uses a
// 32-bit microsecond field, but the guests we run are built against OpenOrbis.
struct RtcDateTime
{
	uint16_t year;
	uint16_t month;
	uint16_t day;
	uint16_t hour;
	uint16_t minute;
	uint16_t second;
	uint16_t microsecond;
};

int KYTY_SYSV_ABI RtcGetCurrentClockLocalTime(RtcDateTime* dt)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(dt == nullptr);

	time_t now = time(nullptr);
	tm     local {};
	localtime_r(&now, &local);

	dt->year        = static_cast<uint16_t>(local.tm_year + 1900);
	dt->month       = static_cast<uint16_t>(local.tm_mon + 1);
	dt->day         = static_cast<uint16_t>(local.tm_mday);
	dt->hour        = static_cast<uint16_t>(local.tm_hour);
	dt->minute      = static_cast<uint16_t>(local.tm_min);
	dt->second      = static_cast<uint16_t>(local.tm_sec);
	dt->microsecond = 0;

	return OK;
}

int KYTY_SYSV_ABI RtcGetCurrentTick(uint64_t* tick)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(tick == nullptr);

	// sceRtc ticks are microseconds since 0001-01-01; the offset below is the
	// Unix epoch in those units
	timespec ts {};
	clock_gettime(CLOCK_REALTIME, &ts);
	*tick = 62135596800000000ULL + static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;

	return OK;
}

} // namespace Rtc

LIB_VERSION("Rtc", 1, "Rtc", 1, 1);

LIB_DEFINE(InitRtc_1)
{
	PRINT_NAME_ENABLE(true);

	LIB_FUNC("ZPD1YOKI+Kw", Rtc::RtcGetCurrentClockLocalTime);
	LIB_FUNC("18B2NS1y9UU", Rtc::RtcGetCurrentTick);
}

} // namespace Kyty::Libs

#endif // KYTY_EMU_ENABLED
