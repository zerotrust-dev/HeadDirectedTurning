# Head Directed Turning

A Skyrim VR SKSE plugin for hands-free, head-directed smooth turning.

> **Work in progress:** This repository is under active development. No usable
> gameplay release exists yet. The current native integration deliberately
> fails closed and cannot rotate the player.

## Status

This is a diagnostic-first scaffold. Version 0.1.0 loads settings and the turn
controller, but intentionally fails closed because the runtime integration has
not been implemented yet. It cannot read a pose or rotate the player.

Nothing in this repository deploys automatically into Skyrim or Mod Organizer 2.

See [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md) for upgrade rules.

## Intended behavior

- Head movement inside the configurable dead zone remains free look.
- Looking beyond `StartAngle` begins smooth turning.
- Returning inside `StopAngle` stops immediately.
- Turning accelerates progressively toward `MaximumTurnSpeed`.
- Pitch, roll, and positional HMD movement are ignored.

## Build prerequisites

- Visual Studio 2022 with Desktop development with C++
- CMake 3.24 or newer
- Ninja
- Git
- vcpkg with the `VCPKG_ROOT` environment variable configured

## Build

```powershell
cmake --preset release-vr
cmake --build --preset release-vr
```

The intended build path is GitHub Actions. The workflow uses a fixed Windows
Server 2022 image, pinned vcpkg commit, versioned dependency registries, unit
tests, and an MO2-ready artifact.

## Safety

Keep `DiagnosticOnly=true` until the HMD and body yaw values have been validated
in `Documents/My Games/Skyrim VR/SKSE/HeadDirectedTurning.log`.

Do not install an active rotation build into the main MGO profile until it has
been tested in a copied MO2 profile.

## Dependency updates

Dependency baselines are intentionally pinned in `vcpkg-configuration.json`.
Update them on a separate branch and retain the previous successful artifact
until the new diagnostic build has passed an in-game compatibility test.
