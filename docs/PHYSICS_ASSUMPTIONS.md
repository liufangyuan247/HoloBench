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

## M2 Scalar Wave Optics Assumptions

[ADR 0005](adr/0005-wave-optics-conventions.md) locks the complete convention. Its active summary is:

- The physical field is $\operatorname{Re}\{Ue^{-i\omega t}\}$, so forward `+Z` propagation uses $e^{+ik_z z}$.
- Forward DFT uses the negative exponential without scaling; inverse DFT uses the positive exponential and divides by $N_xN_y$.
- Spatial storage is row-major with `x` fastest and centered coordinates `(index - floor(N/2)) * pitch`; spectra remain in unshifted FFT order internally.
- `lambda0` is vacuum wavelength, the propagation medium has explicit homogeneous index $n$, and $k=2\pi n/\lambda_0$.
- The finite grid is periodic. Padding/apodization is explicit, not silently inferred, and M2 does not claim open boundaries.
- Default ASM propagation removes evanescent bins instead of allowing unstable negative-distance exponential growth.
- Scalar intensity is proportional to $|U|^2$ and its discrete plane integral is $\sum |U|^2\Delta x\Delta y$; absolute radiometric calibration is separate.
- The deterministic CPU reference uses double-precision complex samples. GPU implementations must document precision and validate against it.
- The fundamental Gaussian-beam source is the scalar paraxial solution: `waistRadiusMetres` is the $1/e$ complex-amplitude radius, $z_R=\pi n w_0^2/\lambda_0$, and the phase includes positive carrier/curvature terms and negative Gouy phase. It is not claimed accurate when the waist approaches the wavelength or in a vector/high-NA regime.
- Binary circular, rectangular, and double-slit apertures classify sample centers; samples exactly on an aperture boundary are transmitted. M2 does not infer fractional pixel coverage or anti-aliased edge transmission.
- The ideal thin-lens field element is the lossless paraxial phase screen $\exp[-ik((x-x_c)^2+(y-y_c)^2)/(2f)]$. It preserves pointwise intensity and does not model thickness, material dispersion, Fresnel loss, aberrations, or finite clear aperture unless a separate aperture mask is applied.
- The Fresnel transfer-function propagator (`FresnelTransferFunctionPropagator`, alias `FresnelPropagator`) computes paraxial scalar propagation using $H(f_x, f_y) = \exp(+ikz)\exp(-i\pi\lambda z(f_x^2+f_y^2))$, where $\lambda = \lambda_0 / n$ is the medium wavelength in metres and $k = 2\pi n / \lambda_0$. Output spatial sampling matches the input grid ($\Delta x_2 = \Delta x_1, \Delta y_2 = \Delta y_1$). It is a paraxial approximation valid for small propagation angles ($\lambda^2 (f_x^2 + f_y^2) \ll 1$) and is not claimed exact for high-NA wide angles. All spatial frequency bins retain $|H| = 1$ without evanescent wave attenuation. It emits `FresnelDiagnostics` containing:
  - `propagatedBinCount`: total number of propagated frequency bins ($N_x \times N_y$).
  - `mediumWavelengthMetres`: medium wavelength $\lambda = \lambda_0 / n$ ($\text{m}$).
  - `periodicBoundary`: always `true`, documenting the periodic boundary assumed by DFT.
  - `automaticPadding`: always `false`, indicating no implicit zero-padding was applied.
  - `nonPropagatingBinCount`: number of bins exceeding exact Helmholtz cutoff $f_x^2 + f_y^2 > 1/\lambda^2$.
  - `nonPropagatingSpectralEnergyFraction`: fraction of forward-FFT spectral energy in non-propagating bins (0.0 for zero-energy fields).
  - `maximumParaxialParameter`: $\max \lambda \sqrt{f_x^2 + f_y^2}$ across all grid frequency bins (dimensionless).
  - `maxAdjacentPhaseStepRadians`: maximum unwrapped quadratic phase step $\Delta\psi$ between adjacent discrete frequency bins along $X$ or $Y$ ($\text{rad}$).
  - `transferFunctionUndersampled`: boolean flag indicating whether $\Delta\psi > \pi\text{ rad}$ (transfer-function frequency-domain phase aliasing).

