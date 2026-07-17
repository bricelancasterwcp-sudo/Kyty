# ClassiCube: Authentic Textures, Real Threading, Multiplayer Networking

**Date:** 2026-07-17
**Status:** Approved (scope confirmed: full multiplayer)
**Goal:** Take ClassiCube-in-Kyty from "playable singleplayer with placeholder
textures" to: authentic look, real guest threads, and joined-server multiplayer
against a locally hosted classic server.

## Background

ClassiCube runs and is playable in Kyty (`KYTY_PAD_FPS=1`, see
2026-07-16-pad-fps-input-design.md). Remaining gaps, all measured:

- Textures are a generated placeholder `default.zip` (distinct block colors).
  The real pack lives at `http://static.classicube.net/default.zip` — the exact
  file console ports download at first boot (Resources.c:719).
- The PS4 port's thread layer is stubbed: 13 functions
  (`Thread_Run/Detach/Join`, `Mutex_*`, `Waitable_*`) are no-ops, and
  `Core.h` marks the port `CC_BUILD_COOPTHREADED` ("TODO rid of").
  `Platform_Posix.c` has the reference pthread implementation; the OpenOrbis
  toolchain ships `pthread.h` (scePthread-backed), and Kyty's Pthread HLE is
  proven by prior titles.
- Networking is doubly stubbed: every guest `Socket_*`/`ParseHost` returns 100,
  AND Kyty's LibNet has no socket functions at all (only
  Init/PoolCreate/InetPton/EtherNtostr/MacAddress + Http/Ssl print-stubs).

## Design

### Part 1 — Authentic textures (asset-only)

Fetch `http://static.classicube.net/default.zip` host-side (identical to what
the game itself downloads on consoles), validate it contains the 21 entries the
engine requires (list = `gen_default_zip.py`), install to
`app0/texpacks/default.zip`. Keep the placeholder as
`default_placeholder.zip.bak` beside it. Verify: boot + frame dump shows
authentic terrain/HUD.

### Part 2 — Real guest threading

In `src/ps4/Platform_PS4.c`: replace the 13 stubs with the `Platform_Posix.c`
implementations (pthread_create/join/detach, `pthread_mutex_*`, waitable =
cond + mutex + signalled flag, `pthread_cond_timedwait` for `WaitFor`).
In `src/Core.h` PS4 block: remove `CC_BUILD_COOPTHREADED`.

Verify: boot clean (any missing pthread NID surfaces as "Unpatched function" —
known 1-line fix pattern in Kyty's LibKernel registration), Http worker thread
starts, singleplayer still playable (`KYTY_PAD_FPS_DEMO` regression), Doom
regression.

### Part 3 — Multiplayer networking

**Kyty side (new HLE surface in LibNet.cpp):** BSD-socket bridge to host
sockets, IPv4/TCP first:

| sceNet fn | host impl |
|---|---|
| `sceNetSocket` | `socket(AF_INET, SOCK_STREAM/DGRAM, 0)` |
| `sceNetSocketClose` | `close` |
| `sceNetConnect` | `connect` (translate Orbis `sockaddr_in` — leading `sin_len` byte — to Linux) |
| `sceNetSend` / `sceNetRecv` | `send`/`recv`, map EAGAIN/EWOULDBLOCK to `SCE_NET_ERROR_EAGAIN` |
| `sceNetSetsockOpt` | `SO_NBIO` → `fcntl(O_NONBLOCK)`; others best-effort |
| `sceNetEpollCreate/Control/Wait/Destroy` | host `poll()` over a small tracked set |
| `sceNetErrnoLoc` (if imported) | per-thread errno cell |

Orbis socket error returns use the `0x804101xx` (SCE_NET_ERROR_*) convention;
translate the handful ClassiCube's builtin backend actually checks
(EAGAIN/EINPROGRESS/ECONNREFUSED/ETIMEDOUT).

**Guest side (`Platform_PS4.c`):** implement `Socket_Create/Connect/Read/
Write/Close/SetNonBlocking/Poll` over those sceNet calls, mirroring the other
console ports' shape. `ParseHost` = `sceNetInetPton` dotted-quad only (no DNS
this pass — direct-connect by IP; Kyty already HLEs `sceNetInetPton`).

**Join flow:** `ProcessProgramArgs` (main_impl.h, PLAT_PS4 block) — if
`/app0/mpconnect.txt` exists (one line: `user ip port`), return the 4-arg
direct-connect form (`user`, empty mppass, `ip`, `port`); otherwise
auto-singleplayer as today. Toggling multiplayer = add/remove a file, no
rebuild.

**Server:** local classic-protocol server on the host. Preference order:
MCGalaxy if dotnet/mono is available, else a minimal Python classic server
(protocol 7: handshake, gzip level send, spawn, position echo — enough to
join, see the world, and see another entity). Server must run with
name-verification off (empty mppass).

**Verify:** guest connects to `127.0.0.1:<port>` from inside Kyty, receives the
level, spawns — frame dump shows the server's world (and a second entity if the
bot/server sends one). Plus: send/recv byte counts in the server log prove
guest→host TCP.

## Out of scope

- Audio: the port's backend is `CC_AUD_BACKEND_NULL`; real audio needs a new
  `Audio_Orbis` guest backend over Kyty's LibAudio — separate follow-up.
- DNS resolution, IPv6, UDP fast-path, TLS (classic protocol is plain TCP).
- classicube.net auth (mppass) — local unverified server only.

## Risks / fallbacks

- Missing pthread/sceNet NIDs in Kyty registration → surface loudly as
  "Unpatched function", fixed by the established LIB_FUNC pattern.
- If real-thread mode trips a latent port bug, threading can ship with
  `CC_BUILD_COOPTHREADED` still set (threads exist for Http) — decided by test.
- If server hosting fights back, deliver verified-TCP (handshake bytes at a
  host listener) and document the remaining step.

## Test plan

1. Textures: boot + dump → authentic look; placeholder kept as fallback.
2. Threading: boot clean, worker thread live, `KYTY_PAD_FPS_DEMO` regression
   still walks/turns/digs; Doom boot regression.
3. Net: join local server, dump in-world frame; server log shows the session.
4. Full regression: singleplayer boot with no env vars unchanged.
