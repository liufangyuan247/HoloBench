# Known limitations

## Optical models (M1)

- **Geometric optics only**: M1 supports paraxial thin-lens imaging, deterministic ray-plane intersections, specular reflection, Snell's law refraction, Total Internal Reflection (TIR), and Numerical Aperture (NA) cone analysis. Wave diffraction, interference, and Fourier optics are not modeled in M1 (slated for M2).
- **Paraxial approximation**: Thin-lens direction deflection and image conjugate equations assume small incident angles and paraxial proximity. Off-axis validity and rear-aperture clipping warnings are emitted when breached.
- **Unmodeled physical effects**: Monochromatic rays only; no Fresnel coefficients (amplitude/power splitting across reflection/transmission), no polarization/Stokes parameters, and no recursive multi-bounce ray tracing.
- **Interface media specification**: For planar dielectric interfaces, `nIncident` and `nTransmitted` are supplied by the caller according to the propagation side and are not automatically swapped for reverse-incident rays.

## Platform & runtime

- Requires an OpenGL 4.6 Core context; macOS is unsupported.
- Local verification has been executed on Windows Clang/Ninja.
- Remote CI workflows for Windows and Linux are configured in the repository; execution on remote GitHub Actions runners is pending git push (remote CI status is not claimed as verified).

## Architecture & data

- Compute backend interfaces for GPU diffraction (ASM) are placeholders pending M2.
- JSON project document format v1 is intentionally minimal, with strict version rejection and no migration framework yet.
- There is no undo/redo, autosave, crash recovery, localization, accessibility layer, or Steam integration.
- Hardware control and digital-twin calibration are long-term roadmap modules.
