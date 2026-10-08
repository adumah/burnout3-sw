# Burnout 3: Takedown — Xbox static recompilation for Nintendo Switch and Linux

The original Xbox (NTSC-U) release of **Burnout 3: Takedown**, lifted
instruction by instruction to C with
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) and running natively,
with no emulator. Built on the runtime of the NFS Underground 2 / Carbon
Switch ports (GPU renderers, Horizon support); an upstream Windows port with
a software GPU exists at [sp00nznet/burnout3](https://github.com/sp00nznet/burnout3).

> **Work in progress.** Boots through the intro movies, menus, profile
> save/load and into races on Linux, but the 3D scene is not shown yet
> (see Status). Not tested on a console yet.

> **No game data is included.** You need your own copy of the Xbox disc,
> extracted (`default.xbe`, `Data/`, `Tracks/`, `ovid/`, ...).

## Playing on Switch

1. Copy the NRO to `sdmc:/switch/b3x/` (`b3x.nro` for OpenGL,
   `b3x-vulkan.nro` for Vulkan).
2. Copy the **extracted** disc (not the ISO) to `sdmc:/switch/b3x/game/`.
3. Start it with **title takeover** (hold **R** while launching any game).

Settings go in `sdmc:/switch/b3x/b3x_env.txt` (KEY=VALUE per line); the log
is `b3x_log.txt` in the same folder.

## Building

```sh
tools/regen.sh                                   # lift default.xbe to C (/root/b3x/gen)
cmake -S . -B build -DNFSU2_GEN_DIR=/root/b3x/gen && cmake --build build -j12   # Linux
JOBS=6 bash switch/build.sh                      # Switch, OpenGL
VULKAN=1 JOBS=6 bash switch/build.sh             # Switch, Vulkan
```

## Status

| | |
|---|---|
| Intro movies (XMV), title, menus | working |
| Profile create / save / load | working |
| Pad, audio | working (Linux) |
| Races (logic, HUD) | running at 60 fps on Linux |
| 3D scene in menus and races | **missing**: rendered to an off-screen target the display never receives |
| Switch | built, untested |
