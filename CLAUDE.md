# Burnout 3: Takedown (Xbox) static recompilation — notes for Claude

Xbox NTSC-U Burnout 3: Takedown (title 0x4541005B, XDK 5849, Criterion
RenderWare, D3D8LTCG), lifted to C with xboxrecomp and built for Linux and
Nintendo Switch (libnx NRO). Started 2026-10-08 from the NFS Carbon port
(`..\nfsc-xbox`, itself from NFSU1/NFSU2; read `..\nfsu2-xbox\CLAUDE.md` for
everything the runtime does on Horizon). Repo: git@github.com:antoxa2584x/burnout3-sw.git, branch `switch` (fork of sp00nznet/burnout3; `main` follows upstream, remote `upstream`). core.fileMode is off: files on /mnt/c look executable.

Upstream has a Windows-only Burnout 3 port on the stock toolkit
(github.com/sp00nznet/burnout3, software NV2A, races at 2-4 fps); a read-only
clone is in `/root/b3x/ref/burnout3` (its docs/boot.md lists what the title
needed). This port uses the NFS toolkit fork instead (GPU renderers, Switch).

## Rules

- Never commit game data (disc, `default.xbe`, `switch_sd/`) or generated C
  (`gen/`). Ask before committing or pushing.
- The Switch build reads the **unpacked** disc at `sdmc:/switch/b3x/game/`.
- Renderer work targets Vulkan (nv2a_vk), as in the NFS ports.
- Linux test hygiene as NFSU2: one run at a time, kill by PID (`run/pid`)
  with **-9** (b3_recomp ignores SIGTERM; live.sh now kill -9s the previous
  pid). Never `pkill -f` a pattern that matches your own shell. A stale
  instance shares run.log, pad.cmd and the FRAME_TRACE flag with the new one.

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
- **3D world missing (fixed, toolkit pb_exec):** Burnout renders every 3D
  scene into an off-screen 640x480 target (0x82B24000 at car select), then
  downsamples it (320x240 0x82C50000, 160x120 0x82C78000/0x82C84000 blur)
  and draws it onto the back buffer with one quad (scene + bloom, two
  texcoord sets). Those quads are immediate mode with the position in
  SET_VERTEX_DATA2F_M slot 0 (0x1880/0x1884), which pb_exec ignored: only
  SET_VERTEX3F/4F and DATA4F slot 0 completed a vertex. Now (as xemu) the
  last component of attribute 0 through any SET_VERTEX_DATA* completes it,
  and each immediate vertex carries every attribute written in the batch
  (float4 from attr_const), not just texcoord0. Race, garage car and motion
  blur appear on GL and Vulkan.
- **Car paint on Vulkan (open):** the paint's final combiner adds
  SPECULAR_FOG_FACTOR0 (1, .91, .82) x r1.a, and r1.a comes from the
  reflection cube's alpha (alpha stage 4: t0.a x t1.a). The cube faces
  (6 x 128x128 at 0x82CB6000 + n*0x10000, zeta 0x82D16000) are opaque
  wherever garage geometry was drawn, so the whole body gets the highlight
  (washed pink); presumably only the light strips (the dome drawn with the
  64x32 stripe texture 0x82D2B000, alpha-only rows) should. GL has no cube
  support, samples alpha 0 and looks plain. `RECOMP_VK_CUBE=0` hides it.
  The garage background is black in the scene and the faces; whether that
  is the game's dark showroom or missing walls is unverified.
- **Debug tools added (toolkit):** `RECOMP_FRAME_TRACE=<flag file>` (pb_exec:
  one line per draw/clear/other-subchannel method of the next frame, with
  the CPU-transformed first vertices; touch the file again for another
  frame); `RECOMP_VK_DUMP_SURF=<va>[,<va>...]` (nv2a_vk: dump those surfaces
  with each RECOMP_GL_DUMP frame, as _s.bmp, _s1.bmp, ...); `RECOMP_GL_TRACE=1` on Vulkan also logs each change
  of render target (pitch/bpp/clip).
- "You have accepted an invitation to play" box after Start on every boot:
  not investigated (A dismisses it).

## Status (2026-10-10)

- Linux GL and Vulkan: EA/Criterion/RenderWare intros, title, profile create,
  save and load, menus, training videos; races render the full 3D world
  with motion blur and drive (35-38 fps GL, ~25 fps Vulkan on lavapipe);
  car select shows the garage car. Open: car paint on Vulkan (above).
- Switch: `b3x.nro` (GL) and `b3x-vulkan.nro` built and staged in
  `switch_sd/switch/b3x/` with the disc (2026-10-08); not run on Eden or
  hardware yet. Icon cropped from the title screen; loader logo still NFSU2's.
