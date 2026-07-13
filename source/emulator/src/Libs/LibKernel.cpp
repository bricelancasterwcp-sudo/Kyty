#include "Kyty/Core/Common.h"
#include "Kyty/Core/DbgAssert.h"
#include "Kyty/Core/Singleton.h"
#include "Kyty/Core/String.h"
#include "Kyty/Math/Rand.h"

#include "Emulator/Common.h"
#include "Emulator/Config.h"
#include "Emulator/Kernel/EventFlag.h"
#include "Emulator/Kernel/EventQueue.h"
#include "Emulator/Kernel/FileSystem.h"
#include "Emulator/Kernel/Memory.h"
#include "Emulator/Kernel/Pthread.h"
#include "Emulator/Kernel/Semaphore.h"
#include "Emulator/Libs/Errno.h"
#include "Emulator/Libs/Libs.h"
#include "Emulator/Loader/Nid.h"
#include "Emulator/Loader/RuntimeLinker.h"
#include "Emulator/Loader/SymbolDatabase.h"

#include <cstdlib>
#include <cstring>

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs {

LIB_VERSION("libkernel", 1, "libkernel", 1, 1);

namespace LibKernel {

using KernelModule                   = int32_t;
using get_thread_atexit_count_func_t = KYTY_SYSV_ABI int (*)(KernelModule);
using thread_atexit_report_func_t    = KYTY_SYSV_ABI void (*)(KernelModule);

#pragma pack(1)

struct KernelLoadModuleOpt
{
	size_t size;
};

struct KernelUnloadModuleOpt
{
	size_t size;
};

struct TlsInfo
{
	Loader::Program* program;
	uint64_t         offset;
};

struct MallocReplace
{
	uint64_t size               = sizeof(MallocReplace);
	void*    malloc_initialize  = nullptr;
	void*    malloc_finalize    = nullptr;
	void*    malloc             = nullptr;
	void*    free               = nullptr;
	void*    calloc             = nullptr;
	void*    realloc            = nullptr;
	void*    memalign           = nullptr;
	void*    reallocalign       = nullptr;
	void*    posix_memalign     = nullptr;
	void*    malloc_stats       = nullptr;
	void*    malloc_stats_fast  = nullptr;
	void*    malloc_usable_size = nullptr;
	void*    aligned_alloc      = nullptr;
};

struct NewReplace
{
	uint64_t size                           = sizeof(NewReplace);
	void*    new_p                          = nullptr;
	void*    new_nothrow                    = nullptr;
	void*    new_array                      = nullptr;
	void*    new_array_nothrow              = nullptr;
	void*    delete_p                       = nullptr;
	void*    delete_nothrow                 = nullptr;
	void*    delete_array                   = nullptr;
	void*    delete_array_nothrow           = nullptr;
	void*    delete_with_size               = nullptr;
	void*    delete_with_size_nothrow       = nullptr;
	void*    delete_array_with_size         = nullptr;
	void*    delete_array_with_size_nothrow = nullptr;
};

struct ModuleInfo
{
	uint64_t     size;
	uint64_t     info[32];
	KernelModule handle;
	uint8_t      pad[156];
};

#pragma pack()

constexpr size_t PROGNAME_MAX_SIZE = 511;

static uint64_t    g_stack_chk_guard                     = 0xDeadBeef5533CCAA;
static char        g_progname_buf[PROGNAME_MAX_SIZE + 1] = {0};
static const char* g_progname                            = g_progname_buf;

static get_thread_atexit_count_func_t g_get_thread_atexit_count_func = nullptr;
static thread_atexit_report_func_t    g_thread_atexit_report_func    = nullptr;

void SetProgName(const String& name)
{
	strncpy(g_progname_buf, name.C_Str(), PROGNAME_MAX_SIZE);
}

static KYTY_SYSV_ABI int* get_error_addr()
{
	PRINT_NAME();

	return Posix::GetErrorAddr();
}

static KYTY_SYSV_ABI void stack_chk_fail()
{
	PRINT_NAME();

	EXIT("stack fail!!!");
}

static KYTY_SYSV_ABI int sigprocmask(int /*how*/, const void* /*set*/, void* /*oset*/)
{
	// PRINT_NAME();

	// printf("\t how = %d\n", how);
	// printf("\t set = %016" PRIx64 "\n", reinterpret_cast<uint64_t>(set));
	// printf("\t oset = %016" PRIx64 "\n", reinterpret_cast<uint64_t>(oset));

	return 0;
}

static KYTY_SYSV_ABI int raise(int sig)
{
	PRINT_NAME();

	printf("\t sig = %d\n", sig);

	// Signals are not delivered to the guest; pretend the call succeeded
	return 0;
}

static KYTY_SYSV_ABI int sigaction(int sig, const void* act, void* oldact)
{
	PRINT_NAME();

	printf("\t sig = %d\n", sig);
	printf("\t act = %016" PRIx64 "\n", reinterpret_cast<uint64_t>(act));

	if (oldact != nullptr)
	{
		// All zeroes = SIG_DFL handler, no flags, empty mask (FreeBSD layout, 32 bytes)
		std::memset(oldact, 0, 32);
	}

	return 0;
}

static KYTY_SYSV_ABI int getrusage(int who, void* usage)
{
	PRINT_NAME();

	printf("\t who = %d\n", who);

	if (usage == nullptr)
	{
		*Posix::GetErrorAddr() = Posix::POSIX_EFAULT;
		return -1;
	}

	// No per-process accounting; report zero usage (FreeBSD struct rusage, 144 bytes)
	std::memset(usage, 0, 144);

	return 0;
}

static KYTY_SYSV_ABI int madvise(void* addr, size_t len, int behav)
{
	PRINT_NAME();

	printf("\t addr  = %016" PRIx64 "\n", reinterpret_cast<uint64_t>(addr));
	printf("\t len   = %" PRIu64 "\n", len);
	printf("\t behav = %d\n", behav);

	// Advice only; safe to ignore
	return 0;
}

struct KernelPollfd
{
	int   fd;
	short events;
	short revents;
};

static KYTY_SYSV_ABI int poll(KernelPollfd* fds, unsigned int nfds, int timeout)
{
	PRINT_NAME();

	printf("\t nfds    = %u\n", nfds);
	printf("\t timeout = %d\n", timeout);

	// Guest descriptors are emulator handles, not host fds, so there is
	// nothing to poll natively. Report "no events ready" after honoring the
	// timeout so callers see an ordinary timeout instead of a busy spin.
	if (fds != nullptr)
	{
		for (unsigned int i = 0; i < nfds; i++)
		{
			fds[i].revents = 0;
		}
	}

	if (timeout > 0)
	{
		LibKernel::KernelUsleep(static_cast<KernelUseconds>(timeout) * 1000u);
	}

	return 0;
}

static KYTY_SYSV_ABI int fcntl(int fd, int cmd, void* arg)
{
	PRINT_NAME();

	printf("\t fd  = %d\n", fd);
	printf("\t cmd = %d\n", cmd);
	printf("\t arg = %016" PRIx64 "\n", reinterpret_cast<uint64_t>(arg));

	// File locking / descriptor flags are not modeled; pretend success
	return 0;
}

static KYTY_SYSV_ABI int ioctl(int fd, uint64_t request, void* argp)
{
	PRINT_NAME();

	printf("\t fd      = %d\n", fd);
	printf("\t request = %016" PRIx64 "\n", request);
	printf("\t argp    = %016" PRIx64 "\n", reinterpret_cast<uint64_t>(argp));

	// Device controls are not modeled; pretend success
	return 0;
}

static KYTY_SYSV_ABI int getrlimit(int resource, void* rlp)
{
	PRINT_NAME();

	printf("\t resource = %d\n", resource);

	if (rlp == nullptr)
	{
		*Posix::GetErrorAddr() = Posix::POSIX_EFAULT;
		return -1;
	}

	// No limits are enforced; report unlimited (rlim_cur, rlim_max)
	auto* limits = static_cast<int64_t*>(rlp);
	limits[0]    = INT64_MAX;
	limits[1]    = INT64_MAX;

	return 0;
}

static KYTY_SYSV_ABI int cpuset_getaffinity(int level, int which, int64_t id, size_t setsize, void* mask)
{
	PRINT_NAME();

	printf("\t level   = %d\n", level);
	printf("\t which   = %d\n", which);
	printf("\t id      = %" PRId64 "\n", id);
	printf("\t setsize = %" PRIu64 "\n", setsize);

	if (mask == nullptr || setsize == 0)
	{
		*Posix::GetErrorAddr() = Posix::POSIX_EFAULT;
		return -1;
	}

	// 7 cores are available to a title
	std::memset(mask, 0, setsize);
	*static_cast<uint8_t*>(mask) = 0x7F;

	return 0;
}

static KYTY_SYSV_ABI int64_t sysconf(int name)
{
	PRINT_NAME();

	printf("\t name = %d\n", name);

	// FreeBSD name values
	constexpr int SC_PAGESIZE         = 47;
	constexpr int SC_NPROCESSORS_CONF = 57;
	constexpr int SC_NPROCESSORS_ONLN = 58;

	switch (name)
	{
		case SC_PAGESIZE: return 0x4000; // 16 KiB
		case SC_NPROCESSORS_CONF:
		case SC_NPROCESSORS_ONLN: return 7; // cores available to a title
		default: break;
	}

	*Posix::GetErrorAddr() = Posix::POSIX_EINVAL;
	return -1;
}

static KYTY_SYSV_ABI KernelModule KernelLoadStartModule(const char* module_file_name, size_t args, const void* argp, uint32_t flags,
                                                        const KernelLoadModuleOpt* opt, int* res)
{
	PRINT_NAME();

	// EXIT_NOT_IMPLEMENTED(!Core::Thread::IsMainThread());

	printf("\tmodule_file_name = %s\n", module_file_name);

	EXIT_NOT_IMPLEMENTED(flags != 0);
	EXIT_NOT_IMPLEMENTED(opt != nullptr);

	auto* rt = Core::Singleton<Loader::RuntimeLinker>::Instance();

	auto* program = rt->LoadProgram(FileSystem::GetRealFilename(String::FromUtf8(module_file_name)));

	auto handle = program->unique_id;

	program->dbg_print_reloc = true;

	rt->RelocateAll();

	int result = rt->StartModule(program, args, argp, nullptr);

	printf("\tmodule_start() result = %d\n", result);

	EXIT_NOT_IMPLEMENTED(result < 0);

	if (res != nullptr)
	{
		*res = result;
	}

	return static_cast<KernelModule>(handle);
}

static KYTY_SYSV_ABI int KernelDlsym(KernelModule handle, const char* symbol, void** addrp)
{
	PRINT_NAME();

	if (symbol == nullptr || addrp == nullptr)
	{
		return KERNEL_ERROR_EFAULT;
	}

	printf("\t handle = %d\n", handle);
	printf("\t symbol = %s\n", symbol);

	auto* rt = Core::Singleton<Loader::RuntimeLinker>::Instance();

	auto* program = rt->FindProgramById(handle);

	if (program == nullptr || program->export_symbols == nullptr)
	{
		return KERNEL_ERROR_ESRCH;
	}

	// Sce dynamic symbols are stored by nid, not by name
	const auto* record = program->export_symbols->FindByNidName(Loader::NidHash(symbol));

	if (record == nullptr || record->vaddr == 0)
	{
		return KERNEL_ERROR_ESRCH;
	}

	*addrp = reinterpret_cast<void*>(record->vaddr);

	printf("\t addr   = %016" PRIx64 "\n", record->vaddr);

	return OK;
}

static int KYTY_SYSV_ABI KernelStopUnloadModule(KernelModule handle, size_t args, const void* argp, uint32_t flags,
                                                const KernelUnloadModuleOpt* opt, int* res)
{
	PRINT_NAME();

	// EXIT_NOT_IMPLEMENTED(!Core::Thread::IsMainThread());

	auto* rt = Core::Singleton<Loader::RuntimeLinker>::Instance();

	EXIT_NOT_IMPLEMENTED(flags != 0);
	EXIT_NOT_IMPLEMENTED(opt != nullptr);

	auto* program = rt->FindProgramById(handle);

	EXIT_NOT_IMPLEMENTED(program == nullptr);

	if (g_get_thread_atexit_count_func != nullptr && g_get_thread_atexit_count_func(program->unique_id) > 0)
	{
		printf("KernelStopUnloadModule: cannot unload %s\n", program->file_name.C_Str());
		if (g_thread_atexit_report_func != nullptr)
		{
			g_thread_atexit_report_func(program->unique_id);
		}
		return KERNEL_ERROR_EBUSY;
	}

	int result = rt->StopModule(program, args, argp, nullptr);

	printf("\tmodule_stop() result = %d\n", result);

	EXIT_NOT_IMPLEMENTED(result < 0);

	if (res != nullptr)
	{
		*res = result;
	}

	rt->UnloadProgram(program);

	return OK;
}

static void* KYTY_SYSV_ABI tls_get_addr(TlsInfo* info)
{
	PRINT_NAME();

	// EXIT_NOT_IMPLEMENTED(!Core::Thread::IsMainThread());

	return Loader::RuntimeLinker::TlsGetAddr(info->program) + info->offset;
}

static void* KYTY_SYSV_ABI KernelGetProcParam()
{
	PRINT_NAME();

	auto* rt = Core::Singleton<Loader::RuntimeLinker>::Instance(); // NOLINT

	return reinterpret_cast<void*>(rt->GetProcParam());
}

static void KYTY_SYSV_ABI KernelRtldSetApplicationHeapAPI(void* api[])
{
	PRINT_NAME();

	for (int i = 0; i < 10; i++)
	{
		printf("\tapi[%d] = 0x%016" PRIx64 "\n", i, reinterpret_cast<uint64_t>(api[i]));
	}

	[[maybe_unused]] auto* heap_malloc         = api[0];
	[[maybe_unused]] auto* heap_free           = api[1];
	[[maybe_unused]] auto* heap_posix_memalign = api[6];
}

static int64_t KYTY_SYSV_ABI write(int d, const char* str, int64_t size)
{
	// PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(d < 0);

	if (d > 2)
	{
		return POSIX_N_CALL(FileSystem::KernelWrite(d, str, size));
	}

	int size_int = static_cast<int>(size);

	emu_printf(FG_BRIGHT_MAGENTA "%.*s" DEFAULT, size_int, str);

	return size_int;
}

static int KYTY_SYSV_ABI open(const char* path, int flags, int mode)
{
	return POSIX_N_CALL(FileSystem::KernelOpen(path, flags, mode));
}

static int KYTY_SYSV_ABI close(int d)
{
	EXIT_NOT_IMPLEMENTED(d <= 2);

	return POSIX_CALL(FileSystem::KernelClose(d));
}

static int64_t KYTY_SYSV_ABI read(int d, void* buf, uint64_t nbytes)
{
	// PRINT_NAME();

	if (d != 0)
	{
		return POSIX_N_CALL(FileSystem::KernelRead(d, buf, nbytes));
	}

	return static_cast<int64_t>(strlen(std::fgets(static_cast<char*>(buf), static_cast<int>(nbytes), stdin)));
}

static int KYTY_SYSV_ABI KernelGetModuleInfoFromAddr(uint64_t addr, int n, ModuleInfo* r)
{
	PRINT_NAME();

	printf("\taddr = %016" PRIx64 "\n", addr);
	printf("\tn = %d\n", n);

	EXIT_NOT_IMPLEMENTED(n != 2);
	EXIT_NOT_IMPLEMENTED(r == nullptr);

	auto* rt = Core::Singleton<Loader::RuntimeLinker>::Instance();

	auto* p = rt->FindProgramByAddr(addr);

	if (p == nullptr)
	{
		printf("\thandle: not found\n");
		r->handle = 0;
		return -1;
	}

	r->handle = p->unique_id;

	printf("\thandle: %d\n", r->handle);

	return 0;
}

static void KYTY_SYSV_ABI KernelDebugRaiseExceptionOnReleaseMode(int /*c1*/, int /*c2*/)
{
	PRINT_NAME();
}

static void KYTY_SYSV_ABI KernelDebugRaiseException(int /*c1*/, int /*c2*/)
{
	PRINT_NAME();
}

static void KYTY_SYSV_ABI exit(int code)
{
	PRINT_NAME();

	::exit(code);
}

static KYTY_SYSV_ABI MallocReplace* KernelGetSanitizerMallocReplaceExternal()
{
	PRINT_NAME();

	static MallocReplace ret;

	return &ret;
}

static KYTY_SYSV_ABI NewReplace* KernelGetSanitizerNewReplaceExternal()
{
	PRINT_NAME();

	static NewReplace ret;

	return &ret;
}

static KYTY_SYSV_ABI int elf_phdr_match_addr(ModuleInfo* m, uint64_t dtor_vaddr)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(m == nullptr);

	auto* rt     = Core::Singleton<Loader::RuntimeLinker>::Instance();
	auto* p      = rt->FindProgramByAddr(dtor_vaddr);
	int   result = (p != nullptr && p->unique_id == m->handle) ? 1 : 0;

	printf("\thandle     = %" PRId32 "\n", m->handle);
	printf("\tdtor_vaddr = %016" PRIx64 "\n", dtor_vaddr);
	printf("\tmatch      = %s\n", result == 1 ? "true" : "false");

	return result;
}

int KYTY_SYSV_ABI KernelUuidCreate(uint32_t* uuid)
{
	PRINT_NAME();

	if (uuid == nullptr)
	{
		return KERNEL_ERROR_EINVAL;
	}

	uuid[0] = Kyty::Math::Rand::Uint();
	uuid[1] = Kyty::Math::Rand::Uint();
	uuid[2] = Kyty::Math::Rand::Uint();
	uuid[3] = Kyty::Math::Rand::Uint();

	return OK;
}

static KYTY_SYSV_ABI void pthread_cxa_finalize(void* /*p*/)
{
	PRINT_NAME();
}

void KYTY_SYSV_ABI KernelSetThreadAtexitCount(get_thread_atexit_count_func_t func)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(g_get_thread_atexit_count_func != nullptr);

	g_get_thread_atexit_count_func = func;
}

void KYTY_SYSV_ABI KernelSetThreadAtexitReport(thread_atexit_report_func_t func)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(g_thread_atexit_report_func != nullptr);

	g_thread_atexit_report_func = func;
}

int KYTY_SYSV_ABI KernelRtldThreadAtexitIncrement(uint64_t* /*c*/)
{
	PRINT_NAME();

	//__sync_fetch_and_add(c, 1);

	return 0;
}

int KYTY_SYSV_ABI KernelRtldThreadAtexitDecrement(uint64_t* /*c*/)
{
	PRINT_NAME();

	//__sync_fetch_and_sub(c, 1);

	return 0;
}

int KYTY_SYSV_ABI KernelIsNeoMode()
{
	PRINT_NAME();

	return (Config::IsNeo() ? 1 : 0);
}

int KYTY_SYSV_ABI clock_gettime(int clock_id, LibKernel::KernelTimespec* time)
{
	PRINT_NAME();

	return POSIX_CALL(LibKernel::KernelClockGettime(clock_id, time));
}

void KYTY_SYSV_ABI KernelSetGPO(uint32_t bits)
{
	PRINT_NAME();

	printf("\t bits = %08" PRIx32 "\n", bits);
}

} // namespace LibKernel

namespace Posix {

LIB_VERSION("Posix", 1, "libkernel", 1, 1);

int KYTY_SYSV_ABI clock_gettime(int clock_id, LibKernel::KernelTimespec* time)
{
	PRINT_NAME();

	return POSIX_CALL(LibKernel::KernelClockGettime(clock_id, time));
}

int KYTY_SYSV_ABI nanosleep(const LibKernel::KernelTimespec* rqtp, LibKernel::KernelTimespec* rmtp)
{
	PRINT_NAME();

	return POSIX_CALL(LibKernel::KernelNanosleep(rqtp, rmtp));
}

int KYTY_SYSV_ABI stat(const char* path, LibKernel::FileSystem::FileStat* sb)
{
	PRINT_NAME();

	return POSIX_CALL(LibKernel::FileSystem::KernelStat(path, sb));
}

void* KYTY_SYSV_ABI mmap(void* addr, size_t len, int prot, int flags, int fd, int64_t offset)
{
	PRINT_NAME();

	printf("\t addr   = 0x%016" PRIx64 "\n", reinterpret_cast<uint64_t>(addr));
	printf("\t len    = %" PRIu64 "\n", len);
	printf("\t prot   = %d\n", prot);
	printf("\t flags  = %d\n", flags);
	printf("\t fd     = %d\n", fd);
	printf("\t offset = %" PRId64 "\n", offset);

	// Only anonymous mappings are supported (FreeBSD MAP_ANON = 0x1000)
	EXIT_NOT_IMPLEMENTED((static_cast<uint32_t>(flags) & 0x1000u) == 0);
	EXIT_NOT_IMPLEMENTED(fd != -1);
	EXIT_NOT_IMPLEMENTED(offset != 0);

	void* out    = addr;
	int   result = LibKernel::Memory::KernelMapFlexibleMemory(&out, len, prot & 0x7, 0);

	if (result != OK)
	{
		*GetErrorAddr() = LibKernel::KernelToPosix(result);
		return reinterpret_cast<void*>(-1); // MAP_FAILED
	}

	return out;
}

int KYTY_SYSV_ABI munmap(void* addr, size_t len)
{
	PRINT_NAME();

	return POSIX_CALL(LibKernel::Memory::KernelMunmap(reinterpret_cast<uint64_t>(addr), len));
}

int64_t KYTY_SYSV_ABI lseek(int d, int64_t offset, int whence)
{
	PRINT_NAME();

	// posix lseek returns the resulting offset (>= 0), or -1 with errno set
	return POSIX_N_CALL(LibKernel::FileSystem::KernelLseek(d, offset, whence));
}

int KYTY_SYSV_ABI open(const char* path, int flags, uint16_t mode)
{
	PRINT_NAME();

	return POSIX_N_CALL(LibKernel::FileSystem::KernelOpen(path, flags, mode));
}

int KYTY_SYSV_ABI close(int d)
{
	PRINT_NAME();

	return POSIX_CALL(LibKernel::FileSystem::KernelClose(d));
}

int64_t KYTY_SYSV_ABI read(int d, void* buf, size_t nbytes)
{
	PRINT_NAME();

	return POSIX_N_CALL(LibKernel::FileSystem::KernelRead(d, buf, nbytes));
}

int64_t KYTY_SYSV_ABI write(int d, const void* buf, size_t nbytes)
{
	PRINT_NAME();

	return POSIX_N_CALL(LibKernel::FileSystem::KernelWrite(d, buf, nbytes));
}

int KYTY_SYSV_ABI gettimeofday(LibKernel::KernelTimeval* tp, void* /*tz*/)
{
	PRINT_NAME();

	// The timezone argument is obsolete and ignored
	return POSIX_CALL(LibKernel::KernelGettimeofday(tp));
}

int KYTY_SYSV_ABI fstat(int d, LibKernel::FileSystem::FileStat* sb)
{
	PRINT_NAME();

	return POSIX_CALL(LibKernel::FileSystem::KernelFstat(d, sb));
}

LIB_DEFINE(InitLibKernel_1_Posix)
{
	LIB_FUNC("lLMT9vJAck0", clock_gettime);
	LIB_FUNC("yS8U2TGCe1A", nanosleep);
	LIB_FUNC("E6ao34wPw+U", stat);
	LIB_FUNC("BPE9s9vQQXo", mmap);
	LIB_FUNC("UqDGjXA5yUM", munmap);

	LIB_FUNC("OxhIB8LB-PQ", Posix::pthread_create);
	LIB_FUNC("h9CcP3J0oVM", Posix::pthread_join);
	LIB_FUNC("7H0iTOciTLo", Posix::pthread_mutex_lock);
	LIB_FUNC("K-jXhbt2gn4", Posix::pthread_mutex_trylock);
	LIB_FUNC("2Z+PpY6CaJg", Posix::pthread_mutex_unlock);
	LIB_FUNC("ttHNfU+qDBU", Posix::pthread_mutex_init);
	LIB_FUNC("dQHWEsJtoE4", Posix::pthread_mutexattr_init);
	LIB_FUNC("mDmgMOGVUqg", Posix::pthread_mutexattr_settype);
	LIB_FUNC("HF7lK46xzjY", Posix::pthread_mutexattr_destroy);
	LIB_FUNC("ltCfaGr2JGE", Posix::pthread_mutex_destroy);
	LIB_FUNC("mkx2fVhNMsg", Posix::pthread_cond_broadcast);
	LIB_FUNC("Op8TBGY5KHg", Posix::pthread_cond_wait);
	LIB_FUNC("mqULNdimTn0", Posix::pthread_key_create);
	LIB_FUNC("6BpEZuDT7YI", Posix::pthread_key_delete);
	LIB_FUNC("WrOLvHU0yQM", Posix::pthread_setspecific);
	LIB_FUNC("0-KXaS70xy4", Posix::pthread_getspecific);
}

} // namespace Posix

namespace FileSystem = LibKernel::FileSystem;
namespace Memory     = LibKernel::Memory;
namespace EventQueue = LibKernel::EventQueue;
namespace EventFlag  = LibKernel::EventFlag;
namespace Semaphore  = LibKernel::Semaphore;

LIB_DEFINE(InitLibKernel_1_FS)
{
	LIB_FUNC("1G3lF1Gg1k8", FileSystem::KernelOpen);
	LIB_FUNC("UK2Tl2DWUns", FileSystem::KernelClose);
	LIB_FUNC("Cg4srZ6TKbU", FileSystem::KernelRead);
	LIB_FUNC("+WRlkKjZvag", FileSystem::KernelReadv);
	LIB_FUNC("4wSze92BhLI", FileSystem::KernelWrite);
	LIB_FUNC("YSHRBRLn2pI", FileSystem::KernelWritev);
	LIB_FUNC("+r3rMFwItV4", FileSystem::KernelPread);
	LIB_FUNC("nKWi-N2HBV4", FileSystem::KernelPwrite);
	LIB_FUNC("eV9wAD2riIA", FileSystem::KernelStat);
	LIB_FUNC("kBwCPsYX-m4", FileSystem::KernelFstat);
	LIB_FUNC("AUXVxWeJU-A", FileSystem::KernelUnlink);
	LIB_FUNC("taRWhTJFTgE", FileSystem::KernelGetdirentries);
	LIB_FUNC("oib76F-12fk", FileSystem::KernelLseek);
	LIB_FUNC("Oy6IpwgtYOk", Posix::lseek);
	LIB_FUNC("wuCroIGjt2g", Posix::open);
	LIB_FUNC("bY-PO6JhzhQ", Posix::close);
	LIB_FUNC("AqBioC2vF3I", Posix::read);
	LIB_FUNC("FN4gaPmuFV8", Posix::write);
	LIB_FUNC("j2AIqSqJP0w", FileSystem::KernelGetdents);
	LIB_FUNC("1-LFLmRFxxM", FileSystem::KernelMkdir);
	LIB_FUNC("mqQMh1zPPT8", Posix::fstat);
}

LIB_DEFINE(InitLibKernel_1_Mem)
{
	LIB_FUNC("mL8NDH86iQI", Memory::KernelMapNamedFlexibleMemory);
	LIB_FUNC("IWIBBdTHit4", Memory::KernelMapFlexibleMemory);
	LIB_FUNC("cQke9UuBQOk", Memory::KernelMunmap);
	LIB_FUNC("pO96TwzOm5E", Memory::KernelGetDirectMemorySize);
	LIB_FUNC("rTXw65xmLIA", Memory::KernelAllocateDirectMemory);
	LIB_FUNC("B+vc2AO2Zrc", Memory::KernelAllocateMainDirectMemory);
	LIB_FUNC("L-Q3LEjIbgA", Memory::KernelMapDirectMemory);
	LIB_FUNC("NcaWUxfMNIQ", Memory::KernelMapNamedDirectMemory);
	LIB_FUNC("MBuItvba6z8", Memory::KernelReleaseDirectMemory);
	LIB_FUNC("WFcfL2lzido", Memory::KernelQueryMemoryProtection);
	LIB_FUNC("BHouLQzh0X0", Memory::KernelDirectMemoryQuery);
	LIB_FUNC("aNz11fnnzi4", Memory::KernelAvailableFlexibleMemorySize);
	LIB_FUNC("vSMAm3cxYTY", Memory::KernelMprotect);
	LIB_FUNC("BPE9s9vQQXo", Posix::mmap);
	LIB_FUNC("UqDGjXA5yUM", Posix::munmap);
}

LIB_DEFINE(InitLibKernel_1_Equeue)
{
	LIB_FUNC("D0OdFMjp46I", EventQueue::KernelCreateEqueue);
	LIB_FUNC("jpFjmgAC5AE", EventQueue::KernelDeleteEqueue);
	LIB_FUNC("fzyMKs9kim0", EventQueue::KernelWaitEqueue);
	LIB_FUNC("vz+pg2zdopI", EventQueue::KernelGetEventUserData);
}

LIB_DEFINE(InitLibKernel_1_EventFlag)
{
	LIB_FUNC("BpFoboUJoZU", EventFlag::KernelCreateEventFlag);
	LIB_FUNC("JTvBflhYazQ", EventFlag::KernelWaitEventFlag);
	LIB_FUNC("IOnSvHzqu6A", EventFlag::KernelSetEventFlag);
}

LIB_DEFINE(InitLibKernel_1_Semaphore)
{
	LIB_FUNC("188x57JYp0g", Semaphore::KernelCreateSema);
	LIB_FUNC("R1Jvn8bSCW8", Semaphore::KernelDeleteSema);
	LIB_FUNC("Zxa0VhQVTsk", Semaphore::KernelWaitSema);
	LIB_FUNC("4czppHBiriw", Semaphore::KernelSignalSema);

	// Unnamed posix semaphores, imported under libkernel (used by SDL)
	LIB_FUNC("pDuPEf3m4fI", Posix::sem_init);
	LIB_FUNC("cDW233RAwWo", Posix::sem_destroy);
	LIB_FUNC("YCV5dGGBcCo", Posix::sem_wait);
	LIB_FUNC("WBWzsRifCEA", Posix::sem_trywait);
	LIB_FUNC("IKP8typ0QUk", Posix::sem_post);
	LIB_FUNC("Bq+LRV-N6Hk", Posix::sem_getvalue);
}

LIB_DEFINE(InitLibKernel_1_Pthread)
{
	LIB_FUNC("9UK1vLZQft4", LibKernel::PthreadMutexLock);
	LIB_FUNC("tn3VlD0hG60", LibKernel::PthreadMutexUnlock);
	LIB_FUNC("2Of0f+3mhhE", LibKernel::PthreadMutexDestroy);
	LIB_FUNC("cmo1RIYva9o", LibKernel::PthreadMutexInit);
	LIB_FUNC("upoVrzMHFeE", LibKernel::PthreadMutexTrylock);
	LIB_FUNC("smWEktiyyG0", LibKernel::PthreadMutexattrDestroy);
	LIB_FUNC("F8bUHwAG284", LibKernel::PthreadMutexattrInit);
	LIB_FUNC("iMp8QpE+XO4", LibKernel::PthreadMutexattrSettype);
	LIB_FUNC("1FGvU0i9saQ", LibKernel::PthreadMutexattrSetprotocol);

	LIB_FUNC("aI+OeCz8xrQ", LibKernel::PthreadSelf);
	LIB_FUNC("6UgtwV+0zb4", LibKernel::PthreadCreate);
	LIB_FUNC("3PtV6p3QNX4", LibKernel::PthreadEqual);
	LIB_FUNC("onNY9Byn-W8", LibKernel::PthreadJoin);
	LIB_FUNC("4qGrR6eoP9Y", LibKernel::PthreadDetach);
	LIB_FUNC("How7B8Oet6k", LibKernel::PthreadGetname);
	LIB_FUNC("bt3CTBKmGyI", LibKernel::PthreadSetaffinity);
	LIB_FUNC("1tKyG7RlMJo", LibKernel::PthreadGetprio);
	LIB_FUNC("W0Hpm2X0uPE", LibKernel::PthreadSetprio);
	LIB_FUNC("T72hz6ffq08", LibKernel::PthreadYield);
	LIB_FUNC("EI-5-jlq2dE", LibKernel::PthreadGetthreadid);

	LIB_FUNC("62KCwEMmzcM", LibKernel::PthreadAttrDestroy);
	LIB_FUNC("x1X76arYMxU", LibKernel::PthreadAttrGet);
	LIB_FUNC("8+s5BzZjxSg", LibKernel::PthreadAttrGetaffinity);
	LIB_FUNC("nsYoNRywwNg", LibKernel::PthreadAttrInit);
	LIB_FUNC("JaRMy+QcpeU", LibKernel::PthreadAttrGetdetachstate);
	LIB_FUNC("Ru36fiTtJzA", LibKernel::PthreadAttrGetstackaddr);
	LIB_FUNC("-fA+7ZlGDQs", LibKernel::PthreadAttrGetstacksize);
	LIB_FUNC("txHtngJ+eyc", LibKernel::PthreadAttrGetguardsize);
	LIB_FUNC("UTXzJbWhhTE", LibKernel::PthreadAttrSetstacksize);
	LIB_FUNC("-Wreprtu0Qs", LibKernel::PthreadAttrSetdetachstate);
	LIB_FUNC("El+cQ20DynU", LibKernel::PthreadAttrSetguardsize);
	LIB_FUNC("eXbUSpEaTsA", LibKernel::PthreadAttrSetinheritsched);
	LIB_FUNC("DzES9hQF4f4", LibKernel::PthreadAttrSetschedparam);
	LIB_FUNC("4+h9EzwKF4I", LibKernel::PthreadAttrSetschedpolicy);
	LIB_FUNC("3qxgM4ezETA", LibKernel::PthreadAttrSetaffinity);

	LIB_FUNC("6ULAa0fq4jA", LibKernel::PthreadRwlockInit);
	LIB_FUNC("BB+kb08Tl9A", LibKernel::PthreadRwlockDestroy);
	LIB_FUNC("Ox9i0c7L5w0", LibKernel::PthreadRwlockRdlock);
	LIB_FUNC("+L98PIbGttk", LibKernel::PthreadRwlockUnlock);
	LIB_FUNC("mqdNorrB+gI", LibKernel::PthreadRwlockWrlock);

	LIB_FUNC("2Tb92quprl0", LibKernel::PthreadCondInit);
	LIB_FUNC("g+PZd2hiacg", LibKernel::PthreadCondDestroy);
	LIB_FUNC("WKAXJ4XBPQ4", LibKernel::PthreadCondWait);
	LIB_FUNC("JGgj7Uvrl+A", LibKernel::PthreadCondBroadcast);
	LIB_FUNC("kDh-NfxgMtE", LibKernel::PthreadCondSignal);
	LIB_FUNC("BmMjYxmew1w", LibKernel::PthreadCondTimedwait);
	LIB_FUNC("m5-2bsNfv7s", LibKernel::PthreadCondattrInit);
	LIB_FUNC("waPcxYiR3WA", LibKernel::PthreadCondattrDestroy);

	LIB_FUNC("QBi7HCK03hw", LibKernel::KernelClockGettime);
	LIB_FUNC("ejekcaNQNq0", LibKernel::KernelGettimeofday);
	LIB_FUNC("-2IRUCO--PM", LibKernel::KernelReadTsc);
	LIB_FUNC("1j3S3n-tTW4", LibKernel::KernelGetTscFrequency);
	LIB_FUNC("4J2sUJmuHZQ", LibKernel::KernelGetProcessTime);
	LIB_FUNC("BNowx2l588E", LibKernel::KernelGetProcessTimeCounterFrequency);
	LIB_FUNC("fgxnMeTNUtY", LibKernel::KernelGetProcessTimeCounter);

	LIB_FUNC("7H0iTOciTLo", Posix::pthread_mutex_lock);
	LIB_FUNC("2Z+PpY6CaJg", Posix::pthread_mutex_unlock);
	LIB_FUNC("mkx2fVhNMsg", Posix::pthread_cond_broadcast);
	LIB_FUNC("Op8TBGY5KHg", Posix::pthread_cond_wait);

	// Remaining posix pthread entry points, also imported under libkernel
	// (the OpenOrbis libc / SDL2 resolve these NIDs from libkernel, not Posix)
	LIB_FUNC("OxhIB8LB-PQ", Posix::pthread_create);
	LIB_FUNC("h9CcP3J0oVM", Posix::pthread_join);
	LIB_FUNC("K-jXhbt2gn4", Posix::pthread_mutex_trylock);
	LIB_FUNC("ttHNfU+qDBU", Posix::pthread_mutex_init);
	LIB_FUNC("dQHWEsJtoE4", Posix::pthread_mutexattr_init);
	LIB_FUNC("mDmgMOGVUqg", Posix::pthread_mutexattr_settype);
	LIB_FUNC("HF7lK46xzjY", Posix::pthread_mutexattr_destroy);
	LIB_FUNC("ltCfaGr2JGE", Posix::pthread_mutex_destroy);
	LIB_FUNC("mqULNdimTn0", Posix::pthread_key_create);
	LIB_FUNC("6BpEZuDT7YI", Posix::pthread_key_delete);
	LIB_FUNC("WrOLvHU0yQM", Posix::pthread_setspecific);
	LIB_FUNC("0-KXaS70xy4", Posix::pthread_getspecific);
	LIB_FUNC("n88vx3C5nW8", Posix::gettimeofday);

	LIB_FUNC("+U1R4WtXvoc", Posix::pthread_detach);
	LIB_FUNC("7Xl257M4VNI", Posix::pthread_equal);
	LIB_FUNC("EotR8a3ASf4", Posix::pthread_self);
	LIB_FUNC("Z4QosVuAsA0", Posix::pthread_once);
	LIB_FUNC("2dEhvvjlq30", Posix::pthread_setcanceltype);
	LIB_FUNC("2MOy+rUfuhQ", Posix::pthread_cond_signal);
	LIB_FUNC("27bAgiJmOh0", Posix::pthread_cond_timedwait);
	LIB_FUNC("RXXqi4CtF8w", Posix::pthread_cond_destroy);
	LIB_FUNC("wtkt-teR1so", Posix::pthread_attr_init);
	LIB_FUNC("2Q0z6rnBrTE", Posix::pthread_attr_setstacksize);
	LIB_FUNC("E+tyo3lp5Lw", Posix::pthread_attr_setdetachstate);
	LIB_FUNC("FIs3-UQT9sg", Posix::pthread_getschedparam);
	LIB_FUNC("Xs9hdiD7sAA", Posix::pthread_setschedparam);
	LIB_FUNC("iGjsr1WAtI0", Posix::pthread_rwlock_rdlock);
	LIB_FUNC("EgmLo6EWgso", Posix::pthread_rwlock_unlock);
	LIB_FUNC("sIlRvQqsN2Y", Posix::pthread_rwlock_wrlock);
	LIB_FUNC("6XG4B33N09g", Posix::sched_yield);
	LIB_FUNC("CBNtXOoef-E", Posix::sched_get_priority_max);
	LIB_FUNC("m0iS6jNsXds", Posix::sched_get_priority_min);
	LIB_FUNC("yS8U2TGCe1A", Posix::nanosleep);
}

LIB_DEFINE(InitLibKernel_1)
{
	InitLibKernel_1_FS(s);
	InitLibKernel_1_Mem(s);
	InitLibKernel_1_Equeue(s);
	InitLibKernel_1_EventFlag(s);
	InitLibKernel_1_Semaphore(s);
	InitLibKernel_1_Pthread(s);
	Posix::InitLibKernel_1_Posix(s);

	LIB_OBJECT("f7uOxY9mM1U", &LibKernel::g_stack_chk_guard);
	LIB_OBJECT("djxxOmW6-aw", &LibKernel::g_progname);

	LIB_FUNC("1jfXLRVzisc", LibKernel::KernelUsleep);
	LIB_FUNC("6c3rCVE-fTU", LibKernel::open);
	LIB_FUNC("6xVpy0Fdq+I", LibKernel::sigprocmask);
	LIB_FUNC("aPcyptbOiZs", LibKernel::sigprocmask);
	LIB_FUNC("JZKw5+Wrnaw", LibKernel::sigprocmask);
	LIB_FUNC("A0O5kF5x4LQ", Posix::fstat);
	LIB_FUNC("NhpspxdjEKU", Posix::nanosleep);
	LIB_FUNC("Wh7HbV7JFqc", LibKernel::getrlimit);
	LIB_FUNC("Pdgml4rbxYk", LibKernel::cpuset_getaffinity);
	LIB_FUNC("0t0-MxQNwK4", LibKernel::raise);
	LIB_FUNC("KiJEPEWRyUY", LibKernel::sigaction);
	LIB_FUNC("hHlZQUnlxSM", LibKernel::getrusage);
	LIB_FUNC("Jahsnh4KKkg", LibKernel::madvise);
	LIB_FUNC("ku7D4q1Y9PI", LibKernel::poll);
	LIB_FUNC("t0fXUzq61Z4", LibKernel::fcntl);
	LIB_FUNC("wW+k21cmbwQ", LibKernel::ioctl);
	LIB_FUNC("mkawd0NA9ts", LibKernel::sysconf);
	LIB_FUNC("6Z83sYWFlA8", LibKernel::exit);
	LIB_FUNC("8OnWXlgQlvo", LibKernel::KernelRtldThreadAtexitDecrement);
	LIB_FUNC("959qrazPIrg", LibKernel::KernelGetProcParam);
	LIB_FUNC("9BcDykPmo1I", LibKernel::get_error_addr);
	LIB_FUNC("bnZxYgAFeA0", LibKernel::KernelGetSanitizerNewReplaceExternal);
	LIB_FUNC("ca7v6Cxulzs", LibKernel::KernelSetGPO);
	LIB_FUNC("DRuBt2pvICk", LibKernel::read);
	LIB_FUNC("f7KBOafysXo", LibKernel::KernelGetModuleInfoFromAddr);
	LIB_FUNC("Fjc4-n1+y2g", LibKernel::elf_phdr_match_addr);
	LIB_FUNC("FxVZqBAA7ks", LibKernel::write);
	LIB_FUNC("kbw4UHHSYy0", LibKernel::pthread_cxa_finalize);
	LIB_FUNC("lLMT9vJAck0", LibKernel::clock_gettime);
	LIB_FUNC("NNtFaKJbPt0", LibKernel::close);
	LIB_FUNC("OMDRKKAZ8I4", LibKernel::KernelDebugRaiseException);
	LIB_FUNC("Ou3iL1abvng", LibKernel::stack_chk_fail);
	LIB_FUNC("p5EcQeEeJAE", LibKernel::KernelRtldSetApplicationHeapAPI);
	LIB_FUNC("pB-yGZ2nQ9o", LibKernel::KernelSetThreadAtexitCount);
	LIB_FUNC("py6L8jiVAN8", LibKernel::KernelGetSanitizerMallocReplaceExternal);
	LIB_FUNC("QKd0qM58Qes", LibKernel::KernelStopUnloadModule);
	LIB_FUNC("rNhWz+lvOMU", LibKernel::KernelSetThreadDtors);
	LIB_FUNC("Tz4RNUCBbGI", LibKernel::KernelRtldThreadAtexitIncrement);
	LIB_FUNC("vNe1w4diLCs", LibKernel::tls_get_addr);
	LIB_FUNC("WhCc1w3EhSI", LibKernel::KernelSetThreadAtexitReport);
	LIB_FUNC("WslcK1FQcGI", LibKernel::KernelIsNeoMode);
	LIB_FUNC("wzvqT4UqKX8", LibKernel::KernelLoadStartModule);
	LIB_FUNC("LwG8g3niqwA", LibKernel::KernelDlsym);
	LIB_FUNC("Xjoosiw+XPI", LibKernel::KernelUuidCreate);
	LIB_FUNC("zE-wXIZjLoM", LibKernel::KernelDebugRaiseExceptionOnReleaseMode);
}

} // namespace Kyty::Libs

#endif // KYTY_EMU_ENABLED
