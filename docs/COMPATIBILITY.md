# Compatibility and upgrades

## Supported stack

The first release targets:

- Skyrim VR `1.4.15`
- SKSEVR `2.0.12`
- VR Address Library for SKSEVR
- CommonLibSSE-NG's VR-only package
- VRIK, HIGGS, PLANCK, and OpenComposite/Pimax OpenXR

The OpenXR runtime is not called directly. The plugin reads the pose already
available inside Skyrim VR, so changing between SteamVR and OpenComposite should
not require a different build.

## Upgrade boundaries

Runtime-sensitive code must remain in `GameIntegration.cpp`:

- HMD and player-body transforms
- frame callbacks or hooks
- game focus detection
- player rotation output
- relocation IDs or offsets

Control behavior belongs in `TurnModel.cpp` and must not access Skyrim APIs.
Settings and MCM code must communicate through the versioned native API.
Smoothing and rotation output are time-based, not frame-based, so changing HMD
refresh rate does not alter control response.

## Rules

1. Prefer CommonLibSSE-NG types and VR Address Library IDs.
2. Do not add absolute addresses or version-specific byte offsets outside
   `GameIntegration.cpp`.
3. Capability-probe every hook or transform before enabling output.
4. Fail closed: an unsupported runtime may log diagnostics but must not rotate.
5. Keep `DiagnosticOnly=true` for the first build after any runtime update.
6. Do not store runtime pointers, offsets, or transient turn state in savegames.
7. Preserve unknown INI keys so newer configurations survive older builds.

## When Skyrim VR or SKSEVR changes

1. Record the Skyrim VR, SKSEVR, VR Address Library, and CommonLib versions.
2. Update dependency baselines on a branch, never directly in a working release.
3. Run the CI build and pure turn-model tests.
4. Install only in a copied MO2 profile.
5. Start with `DiagnosticOnly=true` and verify HMD/body yaw logs.
6. Verify menu, loading-screen, mount, dialogue, death, and save-load behavior.
7. Enable rotation only after the diagnostic values and signs are correct.

## Settings and MCM versioning

`SchemaVersion` protects the INI contract. A newer unsupported schema disables
the plugin instead of guessing. Older schemas load with current defaults for
missing values.

The future Papyrus/MCM interface must expose `GetNativeApiVersion()`. The MCM
must disable controls and show an incompatibility message if its expected API
version differs from the DLL.

## Version ownership

- `CMakeLists.txt` owns the plugin semantic version and passes it into C++.
- `Version.h` owns the settings schema and native MCM API versions.
- `vcpkg-configuration.json` pins dependency registries and must change only on
  an explicitly tested dependency-update branch.
