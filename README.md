[![Linux CI](https://github.com/bricelancasterwcp-sudo/Kyty/actions/workflows/linux.yml/badge.svg)](https://github.com/bricelancasterwcp-sudo/Kyty/actions/workflows/linux.yml)

# Kyty — Linux port
## PS4 & PS5 emulator

This fork ports [InoriRus/Kyty](https://github.com/InoriRus/Kyty) to Linux and continues development from where upstream stopped (October 2022). The original emulator, by [Vladimir M](mailto:inorirus@gmail.com), is licensed under the MIT license; so is this fork.

---
### Linux port status

Verified on Ubuntu (gcc 13+, X11/XWayland, Vulkan on NVIDIA), using homebrew built with the open-source [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain):

| Smoke tier | Exercises | Result |
| --- | --- | --- |
| `hello_world` | ELF load, dynamic linking, native execution, HLE kernel | runs |
| `graphics` | VideoOut, Vulkan present, linear framebuffers | runs (~60 fps) |
| `input` | controller, PNG asset loading, `readv` | runs |
| `SDL2` | threads, TTF text via FreeType, textures, full game loop | runs (~60 fps)¹ |
| `cube`² | GNM command processor, GCN→SPIR-V recompiler, resource-bound shaders (vertex fetch + constant buffers + 2D texture), depth test, double buffering | renders |
| `instcube`² | instanced draw: one `DrawIndexAuto` with `SetNumInstances(N)` → N cubes, each stepped by a per-instance vertex attribute (`InstanceID` fetch) | renders |
| `gltf`² | GNM command-processor breadth on real content — a 321-instruction PBR pixel shader, 300K-index indexed draws, point lighting, real model textures | renders |
| `tex2dthin`² | 2D-thin (tile mode 14) macro-tiled texture detiling — the layout retail PS4 games use, base + neo, unorm + sRGB | renders |
| `doom`³ | FreeDoom (a full Doom port) — engine init, keyboard input, windowed present | playable |

¹ Currently needs `KYTY_PERMISSIVE=1` (below) to stub the remaining unregistered POSIX libc calls; core rendering, threading and fonts are real.

² Built from the open [freegnm](https://gitgud.io/gluesniffer/freegnm-examples) toolchain (OpenOrbis has no PS4 shader compiler); exercises the resource-bound shader and texture paths end-to-end. See [`_smoke/`](_smoke) for the graphics smoke configs.

³ FreeDoom (BSD) built with the OpenOrbis toolchain via [doomgeneric](https://github.com/ozkl/doomgeneric) (GPL, kept out of the MIT emulator tree); confirmed interactive — movement, firing, and menus — in a window.

Audio (`sceAudioOut`) streams to the host device (SDL → PipeWire/ALSA). MP4 video and networking are not implemented on this fork yet.

### Notable fixes in this fork

- **GNM graphics pipeline & resource-bound shaders** — the GNM command processor and GCN→SPIR-V shader recompiler drive real homebrew graphics end-to-end on Vulkan. **Resource-bound shaders now work:** external vertex fetch, constant buffers in both the vertex and pixel stage (via the `0x1c` indirect resource table), and 2D textures + samplers — so a textured, MVP-transformed, depth-tested cube renders. Along the way: derive a shader's `0x1c` table register from its code (works around a psbc/ACO off-by-one), classify indirect-table entries as buffer/texture/sampler by their consumer instruction, declare direct SGPRs so unused `base_vertex`/`start_instance` don't break SPIR-V, accept partial `exp param` varying masks (vec1/2/3), `s_setprio` as a no-op, and looser depth-tiling / double-buffered-scanout handling.
- **Texture tiling** — textures are detiled to linear on upload for the layouts real content uses: `LINEAR_GENERAL` (row-major), 1D-thin, and **2D-thin macro-tiled (tile mode 14) — the format retail PS4 games ship textures in**. The 2D-thin address swizzle (micro-tile order + pipe/bank/tile-split assembly) is cross-verified byte-for-byte against a reference GPU address library for both base and neo GPU configs, covering unorm and sRGB RGBA8; plus `R32G32B32A32_FLOAT` support.
- **Instanced rendering** — `SetNumInstances(N)` + `DrawIndex`/`DrawIndexAuto` now issue a real Vulkan instanced draw (`instanceCount = N`). Per-instance vertex attributes are recognized by their fetch-address VGPR (`v1`–`v3` = `InstanceID`, `v0` = `VertexID`) and mapped to `VK_VERTEX_INPUT_RATE_INSTANCE`, so a single draw call renders N copies each stepped by its own attribute data.
- **Structured loops in the shader recompiler** — a GCN backward `s_branch` (which the linear, forward-only block emitter used to turn into an invalid SPIR-V back-edge) is now reconstructed into a proper `OpLoopMerge` loop: the header carries the loop merge, the back-edge routes through a synthetic continue block, and the loop-condition branch becomes a structured break. Natural single-level loops render correctly, and a follow-on **state-machine relooper** extends this to convergent and multi-exit *scalar* control flow — the shapes a real PBR pixel shader produces — by lowering the CFG to a switch-driven state machine that is always valid SPIR-V (forward-only and natural-loop shaders keep the original emitter byte-for-byte). Divergent per-lane control flow (EXEC-masked) still falls back to a loud "not implemented" rather than emitting invalid SPIR-V.
- **Builds on Ubuntu** — the Qt launcher and warnings-as-errors are now optional CMake flags, so the emulator core compiles cleanly with modern gcc.
- **TLS instruction patcher** handles the `data16` (`0x66`) prefixes clang emits; without this, any guest thread-local access corrupted the host thread and crashed. This unblocks essentially all real (TLS-using) code.
- **FreeType bridge** — guest FreeType calls are forwarded to the host `libfreetype`, enabling TTF text rendering.
- **`gettimeofday`** now has real sub-second precision (was whole-second, which froze `SDL_GetTicks` and crashed frame-rate math).
- Guest allocations are kept below 2^44 to match the GPU memory tracker; the crash reporter's stack walker is bounded to the real thread stack; `posix` mmap/readv/writev/open/close/lseek and `scePad` handle functions implemented.

### Building on Linux

```bash
sudo apt-get install -y cmake gcc g++ make libfreetype-dev libvulkan-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev

cmake -B _Build/gcc -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DKYTY_BUILD_LAUNCHER=OFF \
  -DKYTY_WARNINGS_ARE_ERRORS=OFF \
  source
cmake --build _Build/gcc -j"$(nproc)" --target fc_script
```

### Running

`fc_script` is the emulator, driven by a Lua config that mounts a game directory and loads its modules. Ready-made smoke configs live in [`_smoke/`](_smoke).

```bash
# Point the mount paths in _smoke/*.lua at your built OpenOrbis sample first.
KYTY_PERMISSIVE=1 ./_Build/gcc/fc_script _smoke/smoke_sdl2.lua
```

`KYTY_PERMISSIVE=1` resolves not-yet-implemented imports to a no-op returning 0, so a title can boot past non-critical calls while the gaps are filled in. This fork does **not** include or require any copyrighted PS4/PS5 firmware or games — all testing uses homebrew built from the open OpenOrbis toolchain.

The original project's Windows build and documentation follow below, unchanged.

---
### Screenshots
#### PS4
<img src="https://user-images.githubusercontent.com/7149418/169674296-4185e2da-99f9-4073-8ca9-19dc124c7459.png" width="400"> <img src="https://user-images.githubusercontent.com/7149418/169674298-df817d95-7288-46fe-a040-3c0a40c29a6b.png" width="400"> <img src="https://user-images.githubusercontent.com/7149418/169674301-37a3f947-76cd-4a9b-8c81-adec3d5d9c59.png" width="400"> <img src="https://user-images.githubusercontent.com/7149418/169674303-13edae7d-24d3-4ec6-ba94-586e13c69df5.png" width="400">
#### PS5
<img src="https://user-images.githubusercontent.com/7149418/185373811-3c12178d-d924-4da1-be7a-06ff6cb733b7.png" width="800">

---
### Building
Supported platforms:
- Windows 10 x64

Toolchains:
- Visual Studio + clang-cl + ninja
- Eclipse CDT + mingw-w64 + gcc/clang + ninja/mingw32-make

Supported versions:
Tool                            | Version
:------------                   | :------------
cmake                           |3.12
Visual Studio 2019              |16.10.3
clang                           |12.0.1
clang-cl                        |11.0.0
gcc (MinGW-W64 x86_64-posix-seh)|10.2.0
ninja                           |1.10.1
MinGW-w64                       |8.0.0
Eclipse CDT                     |10.3.0
Qt                              |5.15.0

Define environment variable named Qt5_DIR pointing to the proper version of Qt

MSVC compiler (cl.exe) is not supported!

External dependencies:
* Vulkan SDK 1.2.198.1
* Qt 5.15.0

