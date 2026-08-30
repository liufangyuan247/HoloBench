# Validation status

| Area | Current status | Evidence | Release use |
|---|---|---|---|
| SI length conversion | Validated | Deterministic unit tests (`UnitsTests.cpp`) | Infrastructure |
| Project JSON round trip | Validated | Deterministic round-trip & version rejection tests (`ProjectDocumentTests.cpp`, `SceneProjectAdapterTests.cpp`) | Project persistence |
| OpenGL context & 3D bench | Validated | 120-frame smoke run (exit 0, 0 GL errors, AMD Radeon Pro 5300M, GL 4.6/GLSL 4.60); camera & gizmo unit tests (`CameraTests.cpp`, `GizmoTests.cpp`) | Interactive UI shell |
| Geometric optics (M1) | Validated | 92/92 deterministic tests across `dev` and `core-ci` presets (`ThinLensTests.cpp`, `SnellTests.cpp`, `GeometricElementsTests.cpp`, `NumericalApertureTests.cpp`, `BenchTracerTests.cpp`) | Interactive ray tracing |
| Wave optics (M2) | In progress | Deterministic unit tests (`AngularSpectrumPropagatorTests.cpp`, `FresnelPropagatorTests.cpp`, `GaussianSourceTests.cpp`, `PlaneWaveSourceTests.cpp`, `ApertureFieldTests.cpp`, `ThinLensFieldTests.cpp`, `ComplexField2DTests.cpp`, `CpuFftBackendTests.cpp`) | Prohibited (Pending M2 completion) |
| Holography (M4–M5) | Not implemented | None | Prohibited |

## M1 Validation Breakdown

### 1. Paraxial Thin Lens (`ThinLensTests.cpp`)
- Real image formation: Object at $u > f$ verified against $1/v = 1/f - 1/u$ and $m = -v/u$ within 0.1% relative error.
- Collimated/infinity imaging: Object at $u = f$ producing parallel output rays ($v \to \infty$).
- Virtual image formation: Object at $0 < u < f$ producing diverging rays with negative virtual image distance $v < 0$ and $m > 1$.
- Diverging lens: Lens with $f < 0$ producing virtual upright reduced images ($v < 0, 0 < m < 1$).

### 2. Snell's Law & Total Internal Reflection (`SnellTests.cpp`)
- Refraction across planar dielectric interface verified against $n_1 \sin\theta_1 = n_2 \sin\theta_2$ within $10^{-6}$ rad tolerance.
- Critical angle $\theta_c = \arcsin(n_2 / n_1)$ and Total Internal Reflection (TIR) verified for $\theta_1 > \theta_c$ when $n_1 > n_2$.
- Normal incidence ($\theta_1 = 0$) transmission without deflection.
- Planar specular mirror reflection verified against $\vec{r} = \vec{d} - 2(\vec{d}\cdot\hat{n})\hat{n}$.

### 3. Ray-Plane Intersection & Primitives (`GeometricElementsTests.cpp`)
- Forward intersections, behind-origin rejection, parallel/coplanar rays, and grazing incidence tests.
- Point source spherical bundle generation and collimated source parallel bundle generation.
- Circular aperture clipping and screen plane ray interception.

### 4. Numerical Aperture & Paraxial Warnings (`NumericalApertureTests.cpp`, `BenchTracerTests.cpp`)
- Acceptance cone semi-angle $\alpha = \arctan(r / u)$ and numerical aperture $\text{NA} = n \sin\alpha$ calculations verified.
- Off-axis paraxial validity warnings triggered when ray angle exceeds paraxial threshold ($> 0.1$ rad).
- Rear-aperture clipping warnings triggered when marginal rays exceed downstream element clear apertures.

## M2 Wave Optics Validation Breakdown (In Progress)

### 1. Fresnel Transfer-Function Propagator (`FresnelPropagatorTests.cpp`)
- $z=0$ identity: Verified that $z=0$ propagation performs an exact FFT round-trip within $2\times 10^{-12}$ precision across all bins and initializes baseline diagnostics.
- DC plane-wave phase: Verified that uniform fields acquire the exact longitudinal carrier phase $\exp(+ikz)$ (specifically tested $\pi/2$ phase at $z = \lambda/4$).
- Single spectral bin: Analytic quadratic dispersion phase $\exp(+i[kz - \pi\lambda z(f_x^2+f_y^2)])$ verified against discrete frequency grid within $2\times 10^{-10}$.
- Negative-Nyquist bin phase: Verified that the negative-Nyquist frequency bin in even grid dimensions ($f_x = -1 / (2\Delta x)$) acquires the exact analytic quadratic phase.
- Medium wavelength scaling ($n \neq 1$): Verified that medium wavelength $\lambda = \lambda_0 / n$ and wavenumber $k = 2\pi n / \lambda_0$ correctly scale the quadratic dispersion phase.
- Rectangular pitch preservation: Verified that asymmetric sampling pitches ($\Delta x \neq \Delta y$) evaluate $f_x$ and $f_y$ independently without axis or pitch swapping.
- Energy conservation: Unconditional Parseval energy / integrated intensity $\sum |U|^2 \Delta x \Delta y$ conservation verified within $2\times 10^{-12}$ relative tolerance.
- Reversibility: Positive distance forward propagation followed by negative distance back-propagation returns to the initial state within $5\times 10^{-12}$.
- Paraxial agreement with ASM: Verified tight agreement with exact Angular Spectrum Method (ASM) under low-NA conditions ($< 10^{-5}$ sample difference).
- High-NA divergence: Demonstrated explicit divergence from ASM when spatial frequencies leave the paraxial regime ($> 0.1$ sample difference).
- Evanescent behavior & unit modulus: Verified that sub-wavelength spatial frequencies retain unitary modulus $|H|=1$ under Fresnel propagation while ASM filters them to zero; diagnostics report $f_t > 1/\lambda$ non-propagating bin counts and energy fraction.
- Diagnostics & sampling risk: Verified accurate calculation of `mediumWavelengthMetres`, `periodicBoundary=true`, `automaticPadding=false`, `maximumParaxialParameter`, `nonPropagatingSpectralEnergyFraction` (with robust zero-energy handling), and `transferFunctionUndersampled` phase aliasing threshold ($\Delta\psi > \pi$).
- Robustness & exception safety: Validated rejection of non-finite inputs, unsupported grid dimensions, phase overflow, and verified strong exception safety on backend failures.

## Build and CI Execution Status

- **Local Build & Tests**:
  - `dev` preset: clean warnings-as-errors build and all deterministic unit tests pass.
  - `core-ci` preset: clean warnings-as-errors build and all deterministic unit tests pass (headless, no OpenGL dependency).
  - `app-ci` preset: clean warnings-as-errors application compilation.
- **OpenGL Smoke Test**:
  - 120-frame run on AMD Radeon Pro 5300M with OpenGL 4.6 Core / GLSL 4.60: completed with exit code 0 and 0 reported OpenGL debug errors.
- **GitHub Actions Remote CI**:
  - Baseline run [33332649845](https://github.com/liufangyuan247/HoloBench/actions/runs/33332649845) validates the M1 milestone.
  - Ongoing M2 wave optics additions (including Fresnel transfer-function propagation) are validated under local test presets.
