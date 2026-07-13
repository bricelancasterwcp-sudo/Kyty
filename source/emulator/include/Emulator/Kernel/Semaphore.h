#ifndef EMULATOR_INCLUDE_EMULATOR_KERNEL_SEMAPHORE_H_
#define EMULATOR_INCLUDE_EMULATOR_KERNEL_SEMAPHORE_H_

#include "Kyty/Core/Common.h"

#include "Emulator/Common.h"
#include "Emulator/Kernel/Pthread.h"

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs::LibKernel::Semaphore {

class KernelSemaPrivate;

using KernelSema = KernelSemaPrivate*;

int KYTY_SYSV_ABI KernelCreateSema(KernelSema* sem, const char* name, uint32_t attr, int init, int max, void* opt);
int KYTY_SYSV_ABI KernelDeleteSema(KernelSema sem);
int KYTY_SYSV_ABI KernelWaitSema(KernelSema sem, int need, KernelUseconds* time);
int KYTY_SYSV_ABI KernelPollSema(KernelSema sem, int need);
int KYTY_SYSV_ABI KernelSignalSema(KernelSema sem, int count);
int KYTY_SYSV_ABI KernelCancelSema(KernelSema sem, int count, int* threads);

} // namespace Kyty::Libs::LibKernel::Semaphore

namespace Kyty::Libs::Posix {

// Unnamed posix semaphores (sem_t). The guest sem_t is treated as a slot
// holding a pointer to the host-side semaphore object (the FreeBSD sem_t is
// 16 bytes, so an 8-byte pointer always fits).
struct PosixSemPrivate;

using PosixSem = PosixSemPrivate*;

int KYTY_SYSV_ABI sem_init(PosixSem* sem, int pshared, unsigned int value);
int KYTY_SYSV_ABI sem_destroy(PosixSem* sem);
int KYTY_SYSV_ABI sem_wait(PosixSem* sem);
int KYTY_SYSV_ABI sem_trywait(PosixSem* sem);
int KYTY_SYSV_ABI sem_post(PosixSem* sem);
int KYTY_SYSV_ABI sem_getvalue(PosixSem* sem, int* sval);

} // namespace Kyty::Libs::Posix

#endif // KYTY_EMU_ENABLED

#endif /* EMULATOR_INCLUDE_EMULATOR_KERNEL_SEMAPHORE_H_ */
