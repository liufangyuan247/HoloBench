# Validation status

| Area | Current status | Evidence | Release use |
|---|---|---|---|
| SI length conversion | Validated | Deterministic unit tests (`UnitsTests.cpp`) | Infrastructure |
| Project JSON round trip | Validated | Deterministic round-trip & version rejection tests (`ProjectDocumentTests.cpp`, `SceneProjectAdapterTests.cpp`) | Project persistence |
| OpenGL context & 3D bench | Validated | 120-frame smoke run (exit 0, 0 GL errors, AMD Radeon Pro 5300M, GL 4.6/GLSL 4.60); camera & gizmo unit tests (`CameraTests.cpp`, `GizmoTests.cpp`) | Interactive UI shell |
| Geometric optics (M1) | Validated | 92/92 deterministic tests across `dev` and `core-ci` presets (`ThinLensTests.cpp`, `SnellTests.cpp`, `GeometricElementsTests.cpp`, `NumericalApertureTests.cpp`, `BenchTracerTests.cpp`) | Interactive ray tracing |
| Wave optics (M2 ASM & Fraunhofer) | In progress | 139/139 deterministic tests across local `core-ci` (`AngularSpectrumPropagatorTests.cpp`, `FraunhoferPropagatorTests.cpp`, `FraunhoferDiffractionTests.cpp`, `FieldElementTests.cpp`, `WaveSourceTests.cpp`) | CPU reference solvers |
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

## M2 Wave Optics Validation Breakdown

### 1. Fraunhofer Propagator & Independent Diffraction Oracles (`FraunhoferPropagatorTests.cpp`, `FraunhoferDiffractionTests.cpp`)
- **Single slit**: Analytic $\operatorname{sinc}^2\left(\frac{\pi w x}{\lambda z}\right) \operatorname{sinc}^2\left(\frac{\pi h y}{\lambda z}\right)$ profile, on-axis peak intensity $I_0 = \frac{w^2 h^2}{(\lambda z)^2}$ within 0.5%, first null position $x = \frac{\lambda z}{w}$ within $5\times 10^{-4}$ relative intensity, and sidelobe cross-section profile within 1.5% tolerance.
- **Double slit**: Analytic $\operatorname{sinc}^2\left(\frac{\pi w x}{\lambda z}\right)\cos^2\left(\frac{\pi d x}{\lambda z}\right)$ Young's interference fringes and envelope, peak intensity $4\frac{w^2 h^2}{(\lambda z)^2}$ within 0.5%, fringe spacing $\Delta x = \frac{\lambda z}{d}$, and envelope null $x = \frac{\lambda z}{w}$.
- **Circular aperture (Airy disc)**: Analytic $[2 J_1(v)/v]^2$ pattern, on-axis peak intensity $\left(\frac{\pi D^2}{4 \lambda z}\right)^2$, first dark ring radius matching $r_1 \approx 1.21966989 \frac{\lambda z}{D}$ within half an output pixel ($\le 0.51 \Delta x_{\text{out}}$), and secondary ring peak intensity $I/I_0 \approx 0.0175$.
- **Off-axis plane-wave carrier**: Peak positions $x_{\text{out}} = \lambda z f_x$ and $y_{\text{out}} = \lambda z f_y$ verified with exact sign preservation and axis independence on non-square grids ($N_x \ne N_y, \Delta x \ne \Delta y$).
- **Odd dimension & Direct DFT verification**: Test-only direct DFT reference backend verifies centered input $\to$ unshifted native DFT $\to$ centered output pipeline for arbitrary odd dimensions ($5\times 7, 7\times 5$), ensuring no hidden radix-2 or even-size assumptions in propagator shift phases.
- **Sampling & approximation diagnostics**: Result exposes `mediumWavelengthMetres`, `outputPitchX/YMetres`, `periodicBoundary = true`, `automaticPadding = false`, `isExact = false`, support source tracking (caller diameter/extents vs full grid conservative default), Fresnel number $N_F = D^2/(\lambda z)$, and far-field approximation validity warnings ($N_F \ge 0.1$).
- **Phase range reduction & overflow protection**: Phase angles evaluated with `std::remainder(phase, 2*pi)` to guarantee bounded arguments in $[-\pi, \pi]$ for `std::polar`, and intermediate quadratic phase overflows throw explicit `std::overflow_error`.
- **Energy conservation**: Discrete integrated intensity strictly conserved under Parseval relation $\sum |U_2|^2 \Delta x_{\text{out}} \Delta y_{\text{out}} = \sum |U_1|^2 \Delta x_{\text{in}} \Delta y_{\text{in}}$ within $10^{-12}$ relative tolerance across varying medium refractive indices.
- **Centered spatial alignment & exception safety**: On-axis delta input generates centered spherical quadratic phase, uniform input focuses to output center bin, and throwing FFT backends leave input fields unmodified.

## Build and CI Execution Status

- **Local Build & Tests**:
  - `dev` preset: 92/92 deterministic tests passing (M1 baseline).
  - `core-ci` preset: 139/139 deterministic tests passing with strict `warnings-as-errors` on this branch.
  - `app-ci` preset: Compiles cleanly with strict `warnings-as-errors`.
- **OpenGL Smoke Test**:
  - 120-frame run on AMD Radeon Pro 5300M with OpenGL 4.6 Core / GLSL 4.60: completed with exit code 0 and 0 reported OpenGL debug errors.
- **GitHub Actions Remote CI**:
  - Run [33332649845](https://github.com/liufangyuan247/HoloBench/actions/runs/33332649845): all four jobs pass on main (M1 baseline).
  - Windows and Ubuntu `core-ci`: warnings-as-errors build and 92/92 deterministic tests pass on remote CI baseline.
  - Windows and Ubuntu `app-ci`: warnings-as-errors application compilation passes; the Windows job explicitly binds Glad generation to Python 3.14.7 with pinned Jinja2 3.1.6 and MarkupSafe 3.0.3 dependencies.
  - *Note*: Remote CI reflects the merged main baseline; current branch M2 wave optics additions (139 local tests) are validated locally on this worktree.
