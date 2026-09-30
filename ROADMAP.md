# Roadmap

## Next

- **Profile saving.** The profile is created every launch, so the unskippable
  tutorial video plays before the first event of every mode. With the save
  written to `saves\` and read back, it plays once per mode, as on hardware.
- **Speed.** Races run at 2–4 fps on the software NV2A. Profile the rasteriser
  and the vertex-program interpreter first; a GPU (D3D11) backend for the
  executor comes after.
- **Menu backdrop streaks.** A vertical-stripe pattern over the frontend's
  backdrop movie.
- **Sky dome seams.** Visible texture seams between the sky's segments.
- **Bump and dot-product texture stages.** They read as zero today, which dulls
  the car paint and road reflections.

## Later

- Toolkit PRs for the `work/bo3-integration` changes, split into one PR per
  logical change, after checking HL2 and the other titles for regressions.
  Then `Setup.cmd` and the README move to xboxrecomp `main`.
- Four-pad support end to end. The runtime has four ports; Burnout 3's modes are
  two-player split-screen and pass-the-pad Party Crash, so two are enabled.
- Linux, once the xboxrecomp POSIX runtime covers the NV2A executor and USB.

## Out of scope

- Xbox Live and System Link play.
- Redistributing anything from the game: generated source, assets, movies.
- Hand-written replacements for game code. If the title does something wrong
  here, the fix is in the recompiler or the runtime.
