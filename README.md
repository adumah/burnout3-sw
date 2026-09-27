# Burnout 3: Takedown - Static Recompilation for Windows 11

> **The first known static recompilation of an original Xbox game into a native PC executable.**

This project takes the original Xbox binary of **Burnout 3: Takedown** (2004, Criterion Games / EA) — all 2.73 MB of raw x86 machine code — and translates every single function into native C code that compiles and runs on modern Windows. No emulation. No interpreter. Just 22,097 recompiled functions executing as a native x86-64 binary.

> **Status (branch `feat/xboxrecomp-boot`): alpha.** The build now runs on the
> shared [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) runtime, the way
> `hl2` does. It has no hand-written menus, renderers or game-state overrides.
> From its real entry point the title plays the EA and Criterion intros and
> reaches its own "Press START" screen with the attract video behind it. See
> [docs/boot.md](docs/boot.md) for what it took and how to run it. Everything
> below this note describes the earlier in-tree fork. It is kept for history
> and is not what this branch builds.

![Burnout 3's own title screen, reached from the XBE entry point](docs/screenshots/boot_title_press_start.png)

![Title intro video playing through the boot sequence](docs/screenshots/title.png)
![Intro video - racing footage from the original XMV videos](docs/screenshots/introvideo.png)
![Main menu with full Burnout 3 logo, all options, and scrolling chyron](docs/screenshots/menu_main.png)

## Why This Is Interesting

Static recompilation has seen incredible progress on other platforms — [N64: Recompiled](https://github.com/N64Recomp/N64Recomp) proved MIPS→C was viable, and [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) brought the technique to Xbox 360's PowerPC architecture with projects like Sonic Unleashed getting full native PC ports. But the **original Xbox** has been overlooked. Its emulation scene relies on dynamic recompilation (Cxbx-Reloaded, xemu), and there's a [Halo CE decompilation research project](https://github.com/halo-re/halo), but nobody has attempted a full static recompilation of a retail OG Xbox game — until now.

What makes OG Xbox uniquely interesting for static recomp: it's x86, so the source and target architectures are closely related (x86 → x86-64). But it also means dealing with all of x86's quirks: variable-length instructions, complex addressing modes, flags register, FPU stack, and segment prefixes. Plus the Xbox SDK has 11 statically-linked libraries baked into every game binary that you have to identify and handle.

This project proves it can be done:
- **22,097 functions** lifted from machine code to C, compiling to a native `.exe`
- **The game boots, loads its assets, and enters gameplay** — the original state machine, physics, and game logic all running through recompiled code
- **Real game assets rendered** — actual track geometry with textures loaded from the original game files
- **Every Xbox kernel call replaced** — 147 kernel imports mapped to Win32 equivalents
- **64 MB of Xbox memory faithfully reproduced** using `CreateFileMapping` with mirror views

The technical challenges are fascinating: translating x86 to x86-64 with a global register model, handling indirect calls through a 22K-entry dispatch table, reproducing Xbox memory layout at the right virtual addresses, and implementing a D3D8-to-D3D11 translation layer for the GPU.

## Current Status

**Phase 5: Integration** — The game boots, loads all resources, transitions through state 4 (crash mode) → state 5 (menus/frontend), and renders the main menu with all UI elements. The D3D8LTCG rendering pipeline has been un-stubbed with a live NV2A push buffer translation pipeline proven end-to-end.

### What's Working
- **Full main menu rendering** — all 5 menu options (World Tour, Single Event, Multiplayer, Xbox Live, Driver Details) rendered via NV2A push buffer → D3D11 translation
  - Burnout 3 logo, "Select Option" header, button prompts, scrolling chyron ticker
  - 32 draw calls/frame, 3078 vertices, 11 texture bindings from Global.txd + captured font atlas
  - Time-based smooth chyron scroll animation at 50px/sec
  - Per-draw texture mapping via NV2A VRAM offset → Global.txd name lookup
- **Live NV2A push buffer pipeline** — the original D3D8LTCG rendering function (sub_0034D530, 79KB / 20K lines of generated C) has been un-stubbed and runs cleanly, writing NV2A viewport/transform commands to a 4MB push buffer each frame. Device cursor sync, per-frame buffer reset, and live parser all operational. D3D8 device context populated with surface descriptors, viewport, and push buffer pointers.
- **Boot video sequence** — Criterion logo, EA logo, and title intro videos play from pre-converted XMV→MP4 files via Media Foundation
- Full game boot sequence through the original RenderWare engine init
- **Game state machine**: loading → init → state 4 (crash) → state 5 (menus/frontend) — running continuously at ~32 FPS
- **RW→D3D11 rendering bridge** — the original RenderWare display driver pipeline (sub_001DDAF0 → sub_00351090) routes through our D3D8→D3D11 layer
- **Im2d 2D rendering pipeline** — RwIm2DRenderPrimitive (sub_001DE900) overridden to route pre-transformed 2D vertices through D3D8→D3D11
- **Game audio events** — AWD sounds triggered on state transitions (Zoom on state 7→4, MenuIn on state 4→5, GlobeHigh startup chime)
- **37 playable tracks** loaded from game files with fly camera and drive mode
- **160 DXT textures per track** loaded from `static.dat` and mapped to geometry
- **67 vehicle models** across 7 classes (Compact, Coupe, Heavy, etc.)
- **AWD audio playback** — Fe.awd (50 sounds) and Generic.awd loaded, software mixer with 64 voices
- **MCPX APU audio emulation** — xemu's Voice Processor extracted and running standalone (256 voices, ADPCM/PCM, envelopes, HRTF, filters)
- **NV2A GPU translation** — push buffer method handler translates NV2A Kelvin methods (BEGIN_END, INLINE_ARRAY, texture state, viewport, blend/depth/cull) to D3D8→D3D11 calls
- **Xbox texture unswizzle** — Morton/Z-order decode for non-DXT textures including non-square dimensions (A1R5G5B5, R5G6B5, A8R8G8B8, etc.)
- **VEH fault handling** — divide-by-zero, access violations, and 32-bit overflow all handled gracefully
- **Native pointer resolution** — RW allocator stores native heap pointers in Xbox memory; transparent native↔Xbox VA conversion
- File loading pipeline reads game resources into Xbox memory space
- Keyboard + gamepad input (XInput)
- D3D11 rendering through a D3D8 compatibility layer

### What's Left
- [ ] Regenerate sub_00351090 (RW scene traversal) — disassembler found 0 functions in D3D section, needs function boundary detection for XDK library code
- [ ] Sub-menu navigation (New Profile, Save/Load, World Tour country/map selection)
- [ ] NV2A register combiner emulation (currently hardcoded MODULATE blend mode)
- [ ] Connect RW scene traversal to live push buffer pipeline (render list population)
- [ ] Connect game's DirectSound init (sub_00135040) to APU voice processor
- [ ] Vehicle textures (`.btv` paint variant format)
- [ ] Full collision / physics world initialization
- [ ] Performance optimization

## How It Works

### The Recompilation Pipeline

```
default.xbe (Xbox executable)
    ↓
XBE Parser → extracts 17 sections, 147 kernel imports
    ↓
Disassembler → finds 20,816 functions, 920K instructions
    ↓
x86→C Lifter → generates 4.43 million lines of C code
    ↓
MSVC Compiler → native x86-64 .exe
    ↓
Runtime: Xbox kernel shim + D3D8→D3D11 + memory layout → Game runs!
```

### Register Model

The Xbox uses x86 (32-bit Pentium III). We translate to x86-64 C code with a simulated register model:

```c
// Global registers (shared across all 22,097 functions)
uint32_t g_eax, g_ecx, g_edx, g_esp;  // volatile
uint32_t g_ebx, g_esi, g_edi;          // callee-saved

// Each recompiled function looks like:
void sub_00012345(void) {
    uint32_t ebp;           // local frame pointer
    PUSH32(g_ebp);          // push to simulated stack
    ebp = g_esp;
    g_eax = MEM32(ebp + 8); // read argument from Xbox stack
    // ... translated instructions ...
    POP32(g_ebp);
    RECOMP_RET();
}
```

### Memory Architecture

The Xbox has 64 MB of unified RAM starting at address 0. We reproduce this layout exactly:

- **CreateFileMapping** allocates a 64 MB region as a shared memory object
- **28 MapViewOfFileEx** calls create mirror views at specific addresses
- Xbox code reads/writes memory at the same addresses it would on real hardware
- Special regions: NV2A GPU registers at 0xFD000000+, kernel at 0x80010000+

### D3D8 → D3D11 Translation

The Xbox uses a modified Direct3D 8 API. We implement a full D3D8 COM interface that translates to D3D11:

- Vertex/index buffer creation, Lock/Unlock with staging buffers
- Fixed-function pipeline emulated via HLSL shaders (world/view/proj transforms, lighting, texture stages)
- Render state translation (200+ D3D8 states mapped to D3D11 equivalents)
- DXT1/DXT3/DXT5 compressed textures loaded directly (D3D11 supports these natively)

### Indirect Call Dispatch

The biggest challenge: the game makes thousands of indirect calls (virtual methods, function pointers) that we can't resolve at compile time. Our RECOMP_ICALL macro handles this with a 3-tier lookup:

1. **Manual overrides** — 32 hand-written function replacements
2. **Auto-generated dispatch table** — 22,097 entries mapping Xbox addresses to C functions
3. **Kernel bridge** — Xbox kernel thunks at 0xFE000000+ routed to Win32 implementations

## Discovered File Formats

Through reverse engineering during this project, we've documented several previously-undocumented Criterion game formats:

| Format | File | Description | Documentation |
|--------|------|-------------|---------------|
| **BGV** | `*.bgv` | Vehicle geometry (vertices, normals, UVs, triangle strips) | [docs/formats/bgv.md](docs/formats/bgv.md) |
| **streamed.dat** | Track geometry sections with vertex/index buffers | [docs/formats/streamed-dat.md](docs/formats/streamed-dat.md) |
| **static.dat** | Per-track DXT texture dictionary | [docs/formats/static-dat.md](docs/formats/static-dat.md) |
| **PrgData.bin** | 3-level nested pointer structure (game config) | [docs/formats/prgdata.md](docs/formats/prgdata.md) |
| **Global.txd** | RenderWare texture dictionary (191 HUD/FX textures) | [docs/formats/global-txd.md](docs/formats/global-txd.md) |

## Technical Deep Dives

### Burnout 3-Specific Documentation

| Topic | Description | Doc |
|-------|-------------|-----|
| **RenderWare Engine** | Criterion's custom RW 3.7 fork, binary stream format, world/texture pipeline | [docs/technical/renderware.md](docs/technical/renderware.md) |
| **Track Geometry Pipeline** | streamed.dat parsing, triangle strip conversion, texture mapping | [docs/technical/track-geometry.md](docs/technical/track-geometry.md) |
| **Vehicle Model Pipeline** | BGV format, packed normals, LOD system, draw call extraction | [docs/technical/vehicle-models.md](docs/technical/vehicle-models.md) |
| **Game State Machine** | Boot sequence, state transitions, load queue, gameplay tick | [docs/technical/game-states.md](docs/technical/game-states.md) |

### Generic Xbox Static Recomp Documentation

For the recompilation pipeline, register model, memory layout, ICALL dispatch, D3D translation, kernel replacement, and lessons learned, see the **[xboxrecomp](https://github.com/sp00nznet/xboxrecomp)** toolkit repository — the generic tools and documentation that power this project.

## Target Game

| Field | Value |
|-------|-------|
| **Title** | Burnout 3: Takedown |
| **Developer** | Criterion Games |
| **Publisher** | Electronic Arts |
| **Platform** | Xbox (Original) |
| **Engine** | RenderWare (custom ~3.7 fork, statically linked) |
| **Title ID** | `0x4541005B` (EA-091) |
| **XDK Version** | 5849 |
| **Build Date** | 2004-07-29 |
| **Code Size** | 2.73 MB (.text section: game + CRT + RenderWare) |
| **Functions** | 22,097 (20,816 auto-detected + manual entries) |
| **Kernel Imports** | 147 Xbox kernel calls |

## Project Structure

```
burnout3/
├── README.md                 # You are here
├── CLAUDE.md                 # AI assistant session state
├── CMakeLists.txt            # Top-level build config
├── docs/                     # Documentation
│   ├── screenshots/          # Progress screenshots
│   ├── formats/              # Reverse-engineered file format docs
│   └── technical/            # Deep dive technical documentation
├── tools/                    # Toolchain (also in github.com/sp00nznet/xboxrecomp)
│   ├── xbe_parser/           # XBE file parser
│   ├── disasm/               # Disassembly and function detection
│   ├── func_id/              # Function identification (RW, CRT, vtable)
│   ├── recomp/               # x86→C static recompiler
│   └── dump_*.py             # Burnout 3 format analysis scripts
├── src/                      # Runtime source code
│   ├── kernel/               # Xbox kernel → Win32 (147 imports)
│   ├── d3d/                  # D3D8 → D3D11 translation layer
│   ├── apu/                  # MCPX APU audio emulation (from xemu)
│   ├── nv2a/                 # NV2A GPU register emulation (from xemu)
│   ├── audio/                # DirectSound → XAudio2 stubs
│   ├── input/                # Xbox input → XInput
│   └── game/                 # Game executable
│       ├── main.c            # Entry point, window, game loop
│       ├── bgv_loader.c/h    # Vehicle geometry (67 models)
│       ├── txd_loader.c/h    # RenderWare texture dictionaries
│       ├── track_loader.c/h  # Track geometry from streamed.dat
│       ├── static_textures.c/h # Track textures from static.dat
│       ├── video_player.c/h  # Boot video playback (MF Source Reader)
│       ├── rw_bridge.c/h     # RW→D3D11 rendering bridge
│       ├── rw_renderer.c/h   # 3D renderer (camera, scene, HUD)
│       ├── rw_math.h         # Matrix math utilities
│       ├── awd_loader.c/h    # AWD audio format loader + software mixer
│       └── recomp/           # Recompiled code infrastructure
│           ├── recomp_types.h    # Register model, ICALL macros
│           ├── recomp_manual.c   # 33+ manual function overrides
│           └── gen/              # Auto-generated (4.43M lines, gitignored)
└── Burnout 3 Takedown/       # Original game files (gitignored)
```

## Building

### Prerequisites
- Windows 11
- Visual Studio 2022 (MSVC 19.x)
- Python 3.10+
- CMake 3.20+
- Original `default.xbe` and game data files

### Build Steps
```bash
# 1. Generate recompiled code (~65 seconds, 4.43M lines of C)
py -3 -m tools.recomp "Burnout 3 Takedown/default.xbe" --all --split 1000

# 2. Configure and build
cmake -S . -B build
cmake --build build --config Release

# 3. Run (from the repo root — game files must be in 'Burnout 3 Takedown/')
bin/burnout3.exe
```

### Controls
| Key | Action |
|-----|--------|
| WASD | Drive / fly camera |
| Tab | Sprint (fly mode) |
| T | Cycle tracks (37 available) |
| F | Toggle fly/drive mode |
| U | Toggle 440Hz audio test tone |
| M | Toggle 3D model viewer |
| N/P | Next/previous vehicle model |
| F1/F2 | Settings / Debug menu (ImGui) |
| ESC | Quit |
| Gamepad | Left stick steer, RT/LT gas/brake, A/RB boost |

## Legal Notice

This project is for educational and preservation purposes. You must own a legitimate copy of Burnout 3: Takedown for Xbox to use this project. Original game assets are not included in this repository.

## References

- [XBE File Format](https://xboxdevwiki.net/Xbe) — Xbox Dev Wiki
- [Xbox Kernel Exports](https://xboxdevwiki.net/Kernel) — Xbox Dev Wiki
- [RenderWare](https://gtamods.com/wiki/RenderWare) — GTA Modding Wiki
- [NV2A GPU](https://xboxdevwiki.net/NV2A) — Xbox GPU documentation
- [Xbox Architecture](https://www.copetti.org/writings/consoles/xbox/) — Copetti's analysis
