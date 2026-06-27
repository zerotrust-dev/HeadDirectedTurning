# MCM package

The MCM will be added after the native diagnostic build confirms:

1. HMD yaw is stable with Pimax OpenXR/OpenComposite.
2. Body-relative yaw has the expected sign and range.
3. Rotation output can be applied without fighting VRIK.

The final package will contain:

- `HeadDirectedTurning.esp`
- `Scripts/HeadDirectedTurning_MCM.pex`
- `Scripts/Source/HeadDirectedTurning_MCM.psc`
- translation strings

The native DLL will expose Papyrus functions for reading and updating settings.

The first function implemented must be `GetNativeApiVersion()`. The MCM must
refuse to write settings when its expected API version differs from the DLL.
This prevents an old ESP/Papyrus package from silently misconfiguring a newer
native plugin.
