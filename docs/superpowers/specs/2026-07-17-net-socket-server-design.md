# POSIX Socket-Server Bridge (networking sample) — Design

**Date:** 2026-07-17
**Target:** OpenOrbis `samples/networking` (a TCP server on `0.0.0.0:9025` that sends `"hello\n"` to each client) runs in Kyty, establishing the POSIX socket **server** surface (bind/listen/accept) the sceNet bridge never had.

## What the sample needs

Unlike net_http (which uses the sceNet* HLE by name), this sample uses the **raw POSIX socket API**:

- `socket()` — statically linked from libc, calls `__sys_socketex(name, domain, type, protocol)` imported from **libkernel**.
- `bind` / `listen` / `accept` — direct dynamic imports from **libkernel** (libc's own bind.lo/listen.lo/accept.lo are empty stubs).
- `write` / `read` / `close` — already registered as `Posix::write/read/close`, routing to `FileSystem::KernelWrite/Read/Close`.

NIDs (Sony hash, verified by a non-permissive boot binding all four):

| Symbol | NID | Maps to |
|---|---|---|
| `__sys_socketex` | `pG70GT5yRo4` | `Net::NetSysSocket` |
| `bind` | `KuOmgKoqCdY` | `Net::NetBind` |
| `listen` | `pxnCmagrtao` | `Net::NetListen` |
| `accept` | `3e+4Iv7IJ8U` | `Net::NetAccept` |

Registered in `InitLibKernel_1_FS` (LIB_VERSION "libkernel") — that is the library the guest imports them from, same block as `Posix::write/read/close`.

## Implementation

**POSIX semantics, not SCE.** These four functions are POSIX libkernel syscalls: return fd/0 on success, `-1` with the thread errno cell (`*Posix::GetErrorAddr()`) set on failure — distinct from the sibling sceNet* functions that return SCE error codes. `HostErrnoToPosix()` maps host errno → the FreeBSD-numbered POSIX_* constants. Server side (bind/listen/accept) had no sceNet equivalent; ids are raw host fds, shared with the existing sceNet client bridge. `NetAccept` translates the host peer `sockaddr_in` back into the guest FreeBSD `NetSockaddrIn` layout (sin_len/sin_family bytes, network-order port/addr), bounded by `*addrlen`.

**Constant equivalence:** `AF_INET == 2`, `SOCK_STREAM == 1`, `SOCK_DGRAM == 2` on both the guest FreeBSD ABI and the Linux host, so domain/type pass through directly.

## The fd id-space problem (the load-bearing fix)

Kyty splits descriptors into two spaces:
- **Files:** small `g_files` indices (`index + 3`: 3, 4, 5, …), returned by `KernelOpen`.
- **Sockets:** raw host fds, returned by `socket()`/`accept()`.

`write()`/`read()`/`close()` funnel through `KernelWrite/Read/Close`, which only knew the file space. A socket fd there missed `g_files` — and `GetFile()` **aborts** (`EXIT_IF(!IndexValid)`) on an out-of-range index rather than returning null.

Fix: `KernelWrite/Read/Close` route to host `::write/::read/::close` for socket fds, gated so a **file** fd is never probed as a socket:

```cpp
if (!g_files->IsValidDescriptor(d) && Network::Net::IsHostSocket(d)) { /* host op */ }
```

- `IsValidDescriptor(d)` — new **non-aborting** check (is d a live guest file fd?). Checked FIRST and short-circuits, so valid file fds take the file path with **no** getsockopt on the hot path.
- `IsHostSocket(d)` — `getsockopt(d, SOL_SOCKET, SO_TYPE)` probe.
- Host errors return `KERNEL_ERROR_UNKNOWN + HostErrnoToPosix(errno)` (SCE kernel space = `0x80020000 | posix`), which round-trips back to the right posix errno through `KernelToPosix` in the `POSIX_N_CALL` wrapper.

**Why file-first ordering matters:** a guest file fd is a small index (e.g. 3) that can numerically alias a low host fd the emulator itself holds for an X11/Vulkan/SDL socket. Probing that with getsockopt would misroute the guest's file I/O to an emulator-internal socket. Checking file validity first eliminates that.

**Residual limitation (pre-existing, documented):** if the guest simultaneously holds a file fd at index N and a socket whose raw host fd equals `N + 3`, the two are genuinely indistinguishable by number — the guest itself can't tell them apart. This is inherent to the split-fd design the sceNet client bridge already shipped (raw host fds for sockets); the real fix would be to allocate socket fds out of a reserved high range or through `g_files`, which touches all of sceNet and is out of scope here. In practice guest file fds stay small and socket fds are high, so they don't collide.

## Verification

Guest server binds+listens on `0.0.0.0:9025` (confirmed a real host LISTEN socket via `ss`), `accept()` blocks, two host TCP clients each receive the exact 7-byte `"hello\n\0"`, connections close cleanly, accept loop continues, no crash. net_http's file-write path (same KernelWrite) confirmed byte-identical afterward. Boot lua: `kyty-test-harness/boot_networking.lua`.
