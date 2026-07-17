# ClassiCube Audio: sceAudioOut Backend + Sound Assets

**Date:** 2026-07-17
**Status:** Approved (continuation of the textures/threading/net arc — "audio" follow-up)
**Goal:** Dig/step sounds and music playing from ClassiCube inside Kyty, via a new
guest `Audio_PS4.c` backend over sceAudioOut.

## Ground truth (from code exploration, verified)

- **Kyty's AudioOut HLE is real and sufficient**: `libAudio_1` symbols; AudioOut
  Init/Open/Output(+s)/SetVolume/Close/GetPortState all implemented over SDL2
  host audio with 3-block backpressure pacing; up to 32 ports (index 0 only,
  types {0,1,3,4}, user_id 1 or 255); **arbitrary sample rates accepted**
  (passed to SDL, which converts); volume applied at queue time.
- **ClassiCube backend contract** (`src/Audio.h` + `_AudioBase.h`): poll-model
  (no callbacks): `AudioBackend_Init/Tick/Free`, `Audio_Init/Close/SetVolume/
  DescribeError/AllocChunks/FreeChunks`, `StreamContext_SetFormat/Enqueue/Play/
  Pause/Update`, `SoundContext_FastPlay/PlayData/PollBusy`. Up to 9 concurrent
  contexts (8-sound pool + 1 music). All PCM is 16-bit signed; sounds are mono
  whole-file single chunks, music is 4 rotating ~1s chunks decoded from Vorbis
  on a dedicated music thread. Pitch variation = play at scaled output rate
  (`Audio_AdjustSampleRate`, e.g. dig = 80%), NOT resampling.
- **Assets are not in default.zip**: 59 dig/step sounds + 7 music tracks,
  downloaded by SHA1 hash from `https://resources.download.minecraft.net/`
  (URLs in `Resources.c`); runtime expects `audio/default.zip` (16-bit WAVs)
  and loose `audio/*.ogg`. The in-game downloader needs HTTPS (no SSL backend
  on PS4) — so assets are fetched host-side.

## Design

### Guest backend `src/ps4/Audio_PS4.c` (new, ~250 lines)

Follow the OS2 precedent for wiring: `#define CC_AUD_BACKEND_PS4` in `Core.h`,
PS4 block gets `DEFAULT_AUD_BACKEND CC_AUD_BACKEND_PS4` (auto-disables
`Audio_Null.c` via its guard), `-lSceAudioOut` added to the Makefile libs.

Model: **one sceAudioOut port per active context, opened at the adjusted rate**
(Kyty accepts any rate → no software resampling), grain 256 samples, S16
mono/stereo (format param 0/1), user_id 255, type MAIN(0) for sounds / BGM(1)
for music.

Because `sceAudioOutOutput` blocks (backpressure) and sounds play from the MAIN
thread, each context owns a **feeder thread** (pthreads — enabled by the
threading work): ring of queued chunks with in-use flags, mutex+cond handoff;
feeder sub-chunks each queued chunk into 256-sample blocks, pads the tail with
silence, and loops blocking `sceAudioOutOutput` calls. `PollBusy`/`Update`
read the in-use flags. Port is (re)opened inside the feeder when the requested
(rate, channels) differ from the current port — dig-at-80% vs step-at-100% is a
cheap reopen in the emulator. `Audio_SetVolume` → `sceAudioOutSetVolume`
(0..100 → 0..32768, both-channel mask). `AudioBackend_Tick` = no-op;
`AudioBackend_Init` = idempotent `sceAudioOutInit()`; `Audio_Close` signals
shutdown, joins the feeder, closes the port. Chunk allocation: default
`Mem_TryAlloc` path (no alignment requirement in the emulator; note if real-hw
fidelity is wanted later, add AUDIO_OVERRIDE_ALLOC + grain-sized staging).

### Assets (host-side pipeline, delegated)

Script parses `Resources.c` for names+hashes, downloads from the Mojang CDN,
`ffmpeg`-transcodes sounds OGG→16-bit PCM WAV (mono, source rate), zips them as
`audio/default.zip` entries (`dig_*.wav`/`step_*.wav`), downloads the 7 music
OGGs verbatim, installs everything under `app0/audio/`. Script saved to the
durable harness. (Personal-use fetch of the exact files the game itself
downloads on first run.)

### Kyty: evidence hook

New `KYTY_DUMP_AUDIO=<prefix>` env: `QueueBlock` appends the post-volume PCM of
each port to `<prefix>.port<N>.raw` and logs an occasional summary line. This
is the audio analog of `KYTY_DUMP_FRAME` — headless proof of nonzero samples at
the right rate. No behavior change when unset.

### Verification

1. Sounds: boot with `KYTY_PAD_FPS_DEMO` (digs at flip ~280) + `KYTY_DUMP_AUDIO`;
   assert AudioOutOpen at the expected rates and nonzero-RMS PCM dumps.
2. Music: set `music-mindelay=1` in `/app0/options.txt` for the test run; expect
   a BGM-type port at the Vorbis rate + long-running PCM stream.
3. Regressions: singleplayer boot, multiplayer join, FPS demo, Doom.

Delegation per the standing protocol: asset pipeline + verification run loops +
review pass go to Opus subagents; backend implementation, ABI/threading
decisions, gap debugging, and commits stay in the main session.

## Out of scope

- Real-hardware audio fidelity (fixed 48kHz grains, aligned mempools) — the
  emulator accepts arbitrary rates; noted as a future-hardening TODO in the
  backend header comment.
- In-game resource downloading (needs an SSL backend).
