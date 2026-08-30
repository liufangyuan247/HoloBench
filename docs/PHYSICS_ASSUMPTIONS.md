# Physics and numerical assumptions

This file records active assumptions, not long-term aspirations.

## M0

No optical prediction is implemented. The application must not present any rendered object as a validated physical result.

## Global conventions already locked

- Canonical internal length unit: metre.
- Persisted dimensional fields include their unit in the key, for example `position_m`.
- Geometry, field representations, and visual meshes remain separate.
- Ray and wave propagation use distinct solvers.
- A full laboratory-scale uniformly sampled 3D electromagnetic voxel grid is prohibited.

## Must be locked before relevant implementation

- World handedness, optical axis, surface-normal orientation, and positive-radius convention.
- Angle unit and sign convention.
- Complex phasor time convention.
- Fourier-transform sign and normalization.
- FFT indexing, centering, and even/odd grid convention.
- Sampling location (pixel centres versus edges).
- Boundary, padding, wrap-around, and evanescent-component policies.
- Energy normalization and detector integration convention.
- Refractive-index wavelength unit and valid-domain policy.

Approximations such as thin lens, paraxial propagation, scalar field, monochromatic illumination, full coherence, and absence of multiple reflections must be attached to the solver/result that uses them.

