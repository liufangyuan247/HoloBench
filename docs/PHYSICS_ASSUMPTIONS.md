# Physics and numerical assumptions

This file records active physical assumptions, sign conventions, and validity domains for each solver in HoloBench.

## Global Conventions Locked

- **Canonical Length Unit**: Metre ($\text{m}$). All persisted dimensional keys state their unit explicitly (e.g. `position_m`, `focal_length_m`, `radius_m`).
- **Angular Unit**: Radians ($\text{rad}$) with right-hand-rule-positive rotation around coordinate axes.
- **Coordinate System**: Right-handed Cartesian coordinates where:
  - `+Z` is the nominal optical propagation axis.
  - `+Y` is the world up axis.
  - `+X` is the view-right axis.
- **Separation of Concerns**: Optical physics models (`optics/`), field representations (`core/`), numerical engines (`compute/`), and visual meshes/gizmos (`render/`) are strictly decoupled.
- **Voxel Proscription**: A full laboratory-scale uniformly sampled 3D electromagnetic voxel grid is prohibited.

## M1 Geometric Optics Assumptions

### 1. Paraxial Thin Lens
- **Governing Equations**: Thin-lens imaging equation $\frac{1}{f} = \frac{1}{u} + \frac{1}{v}$ and transverse magnification $m = -\frac{v}{u}$.
- **Focal Length Sign Convention**: $f > 0$ denotes a converging lens; $f < 0$ denotes a diverging lens.
- **Validity Domain**: Strictly paraxial (rays close to the optical axis with small inclination angles $\theta \ll 1\text{ rad}$). An off-axis approximation warning is generated when rays exceed $\theta > 0.1\text{ rad}$.
- **Aperture Stop**: Circular clear aperture of radius $r$. Marginal rays exceeding $r$ are clipped. Downstream optical elements evaluate rear-aperture clipping.
- **Visual Representation**: The rendered 3D lens mesh is a visual aid only and does not define a physical surface prescription.

### 2. Planar Specular Mirror
- **Governing Equation**: Law of reflection $\vec{r} = \vec{d} - 2(\vec{d}\cdot\hat{n})\hat{n}$, where $\vec{d}$ is the normalized incident ray direction and $\hat{n}$ is the unit surface normal oriented to oppose $\vec{d}$.

### 3. Planar Dielectric Interface
- **Governing Equation**: Snell's law of refraction $n_1 \sin\theta_1 = n_2 \sin\theta_2$.
- **Total Internal Reflection (TIR)**: Occurs when $n_1 > n_2$ and $\sin\theta_1 > \frac{n_2}{n_1}$. Refracted ray is replaced by the specularly reflected ray.
- **Refractive Index Specification**: $n_{\text{incident}}$ ($n_1$) and $n_{\text{transmitted}}$ ($n_2$) must be supplied by the caller based on the propagation side. The solver does not automatically invert indices for reverse-propagating rays.

### 4. Image Diagnostics & Numerical Aperture
- **Numerical Aperture**: $\text{NA} = n \sin\alpha$, where $\alpha = \arctan(r / u)$ is the marginal ray acceptance semi-angle for a point source at distance $u$.
- **Image Regimes**:
  - Real image: $u > f \implies v > 0, m < 0$ (inverted).
  - Virtual image: $0 < u < f \implies v < 0, m > 1$ (upright magnified).
  - Infinity / Collimated: $u = f \implies v \to \infty$ (parallel output bundle).

### 5. Excluded Effects in M1 (Geometric Approximations)
- **Monochromatic Propagation**: Rays have a single designated wavelength $\lambda$; no material chromatic dispersion $n(\lambda)$ is modeled.
- **No Polarization**: Ray power/amplitude does not track transverse polarization states ($s/p$).
- **No Fresnel Loss**: Interface transitions assume 100% transmission (or 100% reflection under TIR/mirror); Fresnel reflection and transmission power coefficients are omitted in M1.
- **No Wave Diffraction / Interference**: Optical rays follow rectilinear geometric paths; wave interference and diffraction are deferred to M2.
- **No Recursive Branching**: Rays terminate upon absorption or screen impact; multi-bounce splitting is not performed in M1.

## M2 Wave Optics (To Be Locked Before Implementation)

The following conventions must be locked in ADR 0005 prior to M2 solver implementation:
- Complex phasor time-harmonic convention ($e^{-i\omega t}$ vs. $e^{i\omega t}$).
- Discrete Fourier Transform (DFT/FFT) forward/inverse sign convention and normalization factor ($1/N$ vs. $1/\sqrt{N}$).
- 2D grid spatial sampling pitch ($\Delta x, \Delta y$), grid centering (DC at $[0,0]$ vs. shifted to center), and even/odd grid dimension policy.
- Boundary conditions: Absorbing boundary layers (PML / apodization mask) vs. periodic wrap-around.
- Evanescent wave filtering policy in the Angular Spectrum Method ($k_x^2 + k_y^2 > k_0^2$).
- Energy conservation and detector intensity integration conventions.
