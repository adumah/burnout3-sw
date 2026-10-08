# Burnout 3: Takedown (Xbox) static recompilation — notes for Claude

Xbox NTSC-U Burnout 3: Takedown (title 0x4541005B, XDK 5849, Criterion
RenderWare, D3D8LTCG), lifted to C with xboxrecomp and built for Linux and
Nintendo Switch (libnx NRO). Started 2026-10-08 from the NFS Carbon port
(`..\nfsc-xbox`, itself from NFSU1/NFSU2; read `..\nfsu2-xbox\CLAUDE.md` for
everything the runtime does on Horizon). Not a git repo yet.

Upstream has a Windows-only Burnout 3 port on the stock toolkit
(github.com/sp00nznet/burnout3, software NV2A, races at 2-4 fps); a read-only
clone is in `/root/b3x/ref/burnout3` (its docs/boot.md lists what the title
needed). This port uses the NFS toolkit fork instead (GPU renderers, Switch).

## Rules

- Never commit game data (disc, `default.xbe`, `switch_sd/`) or generated C
  (`gen/`). Ask before committing or pushing.
- The Switch build reads the **unpacked** disc at `sdmc:/switch/b3x/game/`.
- Renderer work targets Vulkan (nv2a_vk), as in the NFS ports.
- Linux test hygiene as NFSU2: one run at a time, kill by PID (`run/pid`);
  never `pkill -f` a pattern that matches your own shell.

## Layout and builds

| Where | What |
|---|---|
| `/root/b3x/` (WSL) | `game/` extracted disc, `gen/` lifted C, `build-linux/` (GL), `build-vk-linux/` (Vulkan, lavapipe), `run/`, `ref/burnout3` |
| `/root/b3x/run.sh` | run `SECS` with `PAD=` script, dumps every `DUMPN` frames to `run/d/` |
| `/root/b3x/live.sh` | start with a live pad file `run/pad.cmd` (`VK=1 BIN=...build-vk-linux/b3_recomp` for Vulkan, `EXTRA="VAR=1 ..."`) |
| `/root/b3x/step.sh "<pad line>" N` | press, wait for N new dumps, newest frame -> scratchpad last.png |
| `/root/b3x/nav.sh globe\|car\|race` | from boot to that screen with the saved profile (after live.sh) |
| `/root/b3x/peek.py PID ADDR '*PTR+OFF'` | read guest memory of a running build |
| `xboxrecomp/` | vendored toolkit = nfsc-xbox's + the changes below |

- Disc: `xboxrecomp\Burnout 3 - Takedown (USA).iso`, `python3 -m tools.xiso unpack`.
- Regenerate: `tools/regen.sh` (`LIFT_ONLY=1` after manual-override edits;
  `TRACE_FUNCS=<json list>` adds `[TRACE]` entry logging).
- Linux: `cmake --build /root/b3x/build-linux -j12`; Vulkan:
  `/root/b3x/build-vk-linux` (`-DNFSU2_VULKAN=ON`).
- Saves land in `/root/b3x/game/UDATA/4541005b/` (profile "BURNOUT" made there).

## Findings

- **Library hooks remapped from Carbon** (bodies byte-identical apart from
  addresses): memmove 0x2442A0, _ftol2 0x244DF0, DSOUND DSP ack 0x2F7EC2,
  AC97 reset 0x2FE736 (lock 0x2F3F70/0x2F3F92, re-arm 0x2FE540, table
  0x300448). XPP is the same code as Carbon's (seeds from upstream).
- **D3D8LTCG hooks** (same device layout as NFS, own conventions): device
  0x35D6A0 via 0x35FB48; BlockOnTime 0x351770 (fence in eax, flags on the
  stack, ret 4); vblank handler 0x356DA0 (block in **eax**, = device+0x1C28);
  PGRAPH handler 0x357000 (ecx); ISR 0x356AD0; GetDisplayFieldStatus
  0x34E0A0 (VBlankCount = device+0x1DE8 = block+0x1C0). `B3_D3D_TRACE=1`
  logs vblank/PGRAPH/slow fence waits.
- **Upstream lifter commits ported** into the vendored toolkit: 64796df +
  5d507e7 (x87 precision control, PC=24), a006905 (x87 unordered), 206b733
  (all SSE compares; `cmpltss` was UNIMPL), the comiss half of 894ceef (its
  lahf half was already done differently here).
- **Intro movie never advanced (fixed, toolkit):** `in al, dx` was a no-op.
  D3D's vblank handler reads the video field from port 0x80C0 bit 5 into
  device+0x1DF8; Burnout runs an interlaced mode, so GetDisplayFieldStatus
  reported field 1 forever and the XMV player (GetNextFrame 0x2CCB1C, called
  from 0x1C93D0) never started its clock (only field 2 starts it). The
  lifter now emits `xbox_PortIn/xbox_PortOut` (test_port_io.py); the runtime
  (kernel_bridge.c) flips 0x80C0 bit 5 every vblank.
- **Viewport constants (toolkit, from upstream 0e9cff9):** SET_VIEWPORT_OFFSET
  / _SCALE now also land in vertex constants c[59]/c[58].
- **Surface bytes per pixel (toolkit, from upstream 777c1cd):** pitch / clip
  width gave 5-12 "bytes a pixel" for Burnout's narrow menu clips; the GL
  back end then rebuilt (and cleared) the back buffer at the wrong width.
  surface_bpp() now reads SET_SURFACE_FORMAT.
- **Off-screen batch cull drops Burnout draws:** with it on, the car-select
  thumbnails vanished. main.c sets `RECOMP_CULL=0` (cull_batch's corner test
  needs a look before it is turned back on).
- **3D scene off screen (open):** at car select RenderWare draws the garage
  car into an off-screen 640x480 target 0x82B24000 (dump it with
  `RECOMP_VK_DUMP_SURF=82B24000`: car + reflection render correctly, garage
  walls missing), while the displayed back buffers (0x827A0000/0x828CC000)
  get only the UI. Nothing samples the target in the traced frames, no
  other subchannel is used (no blit), no CPU reads it (gdb rwatch), the
  PVIDEO overlay is unprogrammed and PCRTC shows only the back buffers.
  Old pushbuffer memory holds 4-stage SET_TEXTURE_OFFSET = 0x02B24000
  (format 0x00011229, linear A8R8G8B8) blocks, so some pass does sample it.
  Upstream has the same gap (its vehicle-select shot shows noise).
- **Debug tools added (toolkit):** `RECOMP_FRAME_TRACE=<flag file>` (pb_exec:
  one line per draw/clear/other-subchannel method of the next frame, with
  the CPU-transformed first vertices; touch the file again for another
  frame); `RECOMP_VK_DUMP_SURF=<va>` (nv2a_vk: dump that surface with each
  RECOMP_GL_DUMP frame); `RECOMP_GL_TRACE=1` on Vulkan also logs each change
  of render target (pitch/bpp/clip).
- "You have accepted an invitation to play" box after Start on every boot:
  not investigated (A dismisses it).

## Status (2026-10-08)

- Linux GL and Vulkan: EA/Criterion/RenderWare intros, title, profile create,
  save and load, menus, training videos, race loads and drives (HUD, speed)
  at 60 fps. **3D world missing** everywhere (garage car, Crash Nav globe,
  race): the title submits only ~30 (2D/UI) draws a frame; under
  investigation.
- Switch: `b3x.nro` (GL) and `b3x-vulkan.nro` built and staged in
  `switch_sd/switch/b3x/` with the disc (2026-10-08); not run on Eden or
  hardware yet. Icon cropped from the title screen; loader logo still NFSU2's.
