# First Diagnostic Test

This build reads pose data and writes a log. Keep `DiagnosticOnly=true` so its
packaged output path remains inactive during diagnostic tests.

## Install

1. Use a copied MGO MO2 profile.
2. Install the GitHub Actions ZIP as a normal MO2 mod.
3. Keep `DiagnosticOnly=true` in `HeadDirectedTurning.ini`.
4. Place the mod late enough that no other mod replaces its DLL or INI.

## Test

1. Start Skyrim VR through the copied MO2 profile.
2. Load a quiet interior save and hold your head naturally forward for three
   seconds while the automatic center is measured.
3. Look approximately 30 degrees left, return to center, then look right.
4. Repeat while looking slightly up and then slightly down.
5. Exit normally to desktop.

Do not continue if Skyrim crashes during startup or loading. Disable the mod
before the next launch and preserve the SKSE log.

## Expected Log

The log is:

`Documents\My Games\Skyrim VR\SKSE\HeadDirectedTurning.log`

A successful test contains:

- `Plugin loaded successfully`
- `VR pose diagnostics initialized; player update hook installed`
- `Turn controller started in diagnostic mode`
- repeated `pose hmd=... body=... relative=... output=...` lines

For the first test, confirm that:

- `relative` is near zero while looking forward;
- left and right produce opposite signs;
- pitch does not substantially change `relative`;
- returning to center brings `relative` back near zero;
- the game never turns because of this plugin.

## Remove

Disable or uninstall the MO2 mod. The plugin does not modify saves and contains
no ESP, scripts, or persistent game data.
