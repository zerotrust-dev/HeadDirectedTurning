# Head Directed Turning

`v1.0.0` is the proven velocity-turning release. Current development adds a
gaze-alignment mode that turns the VR room until the body heading catches the
physical gaze, then clutches the comfortable head return.

Set `TurningMode=GazeAlignment` for positional body alignment or
`TurningMode=Velocity` for the v1.0 hold-to-turn behavior.

A Skyrim VR SKSE plugin for hands-free, head-directed smooth turning.

> **Work in progress:** This repository is under active development. The
> packaged configuration enables rotation output with `DiagnosticOnly=false`
> and retains detailed diagnostics for controlled testing.

## Status

Version 0.1.0 reads the Skyrim VR upright HMD yaw and player heading from a
lifecycle-safe player update hook. It logs rate-limited samples for coordinate
validation and sends active rotation output through the companion.

Nothing in this repository deploys automatically into Skyrim or Mod Organizer 2.

See [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md) for upgrade rules.
Use [`docs/DIAGNOSTIC_TEST.md`](docs/DIAGNOSTIC_TEST.md) for the first in-game
pose validation.

## Required companion

The MO2 package includes:

`Tools\HeadDirectedTurning\HeadDirectedTurningCompanion.exe`

Launch the companion before starting Skyrim VR. Keep it running while playing.
It creates the virtual Xbox controller early enough for Skyrim to discover it,
then receives only the right-stick value from the SKSE plugin. The companion
centers the stick automatically if plugin updates stop and exits after Skyrim.
It writes applied ViGEm reports to
`Tools\HeadDirectedTurning\HeadDirectedTurningCompanion.log`; the SKSE log also
records requested and acknowledged stick values once per second.

Add the companion as an MO2 executable or launch it manually before starting
SKSE. ViGEmBus must already be installed.

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

Set `DiagnosticOnly=true` only when you intentionally want to collect pose
diagnostics without sending rotation output.

Do not install an active rotation build into the main MGO profile until it has
been tested in a copied MO2 profile.

## Dependency updates

Dependency baselines are intentionally pinned in `vcpkg-configuration.json`.
Update them on a separate branch and retain the previous successful artifact
until the new diagnostic build has passed an in-game compatibility test.
