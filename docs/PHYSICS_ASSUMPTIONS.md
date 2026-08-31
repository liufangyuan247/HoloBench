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
- **Legacy geometric scene**: M1 bench rays still carry one designated wavelength and its planar interfaces use caller-supplied constant indices. M4 defines validated constant, Cauchy, and Sellmeier phase-index models; they become physically active only through the explicit real-lens prescription tracer, not by silently changing legacy scenes.
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
- Sampled field observables in `core/field/`:
  - Pointwise linear intensity is $I(x,y) = |U(x,y)|^2 = \operatorname{Re}(U)^2 + \operatorname{Im}(U)^2$. Exact zero amplitude ($0+0i$) evaluates to 0.0. Non-zero amplitudes whose true mathematical intensity is strictly less than double-precision $\text{denorm\_min}$ ($\text{std::numeric\_limits<double>::denorm\_min()}$) throw `std::underflow_error` before final rounding (via base-2 `frexp`/`ldexp` analysis derived from `std::numeric_limits<double>`); arithmetic overflows throw `std::overflow_error`. Exact $\text{denorm\_min}$ and representable subnormals are preserved and returned accurately.
  - Pointwise decibel log intensity is $I_{\text{dB}}(x,y) = \max(20\log_{10}|U(x,y)| - 10\log_{10} I_{\text{ref}}, \text{floor}_{\text{dB}})$, requiring $I_{\text{ref}} > 0$ and $\text{floor}_{\text{dB}} \le 0$. Evaluated via stable amplitude logarithms without squaring first, supporting subnormal and extreme finite amplitudes. Exact zero amplitude directly evaluates to $\text{floor}_{\text{dB}}$.
  - Pointwise wrapped phase is normalized to the unique half-open interval $\phi(x,y) \in [-\pi, +\pi)$ rad, mapping the negative real axis uniformly to $-\pi$ rad regardless of imaginary zero sign ($\pm 0$). For threshold $I_{\min} = 0$, only exact zero amplitude ($0+0i$) is invalid; all non-zero finite amplitudes (including subnormals) produce valid phase. For $I_{\min} > 0$, magnitude $|U|$ is stably compared against $\sqrt{I_{\min}}$ via $\operatorname{hypot}$ without premature squaring underflow, where $|U| \ge \sqrt{I_{\min}}$ is valid and $|U| < \sqrt{I_{\min}}$ is invalid (returning deterministic 0.0 rad phase and `validityMask` entry 0).
  - Discrete transverse plane integrated relative intensity is $\Delta x\Delta y \sum |U(x,y)|^2$ (for `ComplexField2D`) and $\Delta x\Delta y \sum I(x,y)$ (for `ScalarField2D`), evaluated via base-2 scaled accumulation ($\operatorname{frexp}/\operatorname{ldexp}$ derived from `std::numeric_limits<double>`) to support balanced extreme scale fields (e.g. huge amplitude $\times$ tiny pitch or tiny amplitude $\times$ huge pitch). Exact zero fields evaluate to 0.0. Non-zero fields whose total integral is strictly less than double-precision $\text{denorm\_min}$ ($\text{std::numeric\_limits<double>::denorm\_min()}$) throw `std::underflow_error` before final rounding; overflows throw `std::overflow_error`. (Units: $\text{field-amplitude-squared}\cdot\text{m}^2$). Absolute radiometric power (Watts) requires separate optical impedance and source calibration.
  - Observables reject non-finite inputs (NaN, Inf) and invalid bounds ($\text{floor}_{\text{dB}} > 0$, $I_{\text{ref}} \le 0$, $I_{\min} < 0$) via `std::invalid_argument`.
- The Fresnel transfer-function propagator (`FresnelTransferFunctionPropagator`, convenience short alias `FresnelPropagator`) computes paraxial scalar propagation using $H(f_x, f_y) = \exp(+ikz)\exp(-i\pi\lambda z(f_x^2+f_y^2))$, where $\lambda = \lambda_0 / n$ is the medium wavelength in metres and $k = 2\pi n / \lambda_0$. Output spatial sampling matches the input grid ($\Delta x_2 = \Delta x_1, \Delta y_2 = \Delta y_1$). It is a paraxial approximation valid for small propagation angles ($\lambda^2 (f_x^2 + f_y^2) \ll 1$) and is not claimed exact for high-NA wide angles. All spatial frequency bins retain $|H| = 1$ without evanescent wave attenuation. It emits `FresnelDiagnostics` containing:
  - `propagatedBinCount`: total number of propagated frequency bins ($N_x \times N_y$).
  - `mediumWavelengthMetres`: medium wavelength $\lambda = \lambda_0 / n$ ($\text{m}$).
  - `periodicBoundary`: always `true`, documenting the periodic boundary assumed by DFT.
  - `automaticPadding`: always `false`, indicating no implicit zero-padding was applied.
  - `nonPropagatingBinCount`: number of bins exceeding exact Helmholtz cutoff $f_x^2 + f_y^2 > 1/\lambda^2$.
  - `nonPropagatingSpectralEnergyFraction`: fraction of forward-FFT spectral energy in non-propagating bins (0.0 for zero-energy fields).
  - `maximumParaxialParameter`: $\max \lambda \sqrt{f_x^2 + f_y^2}$ across all grid frequency bins (dimensionless).
  - `maxAdjacentPhaseStepRadians`: maximum unwrapped quadratic phase step $\Delta\psi$ between adjacent discrete frequency bins along $X$ or $Y$ ($\text{rad}$).
  - `transferFunctionUndersampled`: boolean flag indicating whether $\Delta\psi > \pi\text{ rad}$ (transfer-function frequency-domain phase aliasing).
  - Representable-domain exceptions: Multi-factor phase computations (carrier phase, quadratic spectral phase, and adjacent phase steps) use mantissa-exponent decomposition. Exact zero factors ($z = 0$ or $f_x = 0$) evaluate deterministically to 0.0. When all factors are non-zero but the exact product underflows below the double-precision representable range (below subnormal `denorm_min`), `std::underflow_error` is thrown rather than silently zeroing the phase; arithmetic overflows or non-finite factors throw `std::overflow_error`. Strong exception safety guarantees that the input field remains bitwise unmodified if an exception is thrown.
- The Fraunhofer propagator (`FraunhoferPropagator`) is an approximate paraxial far-field diffraction solver, not an exact wave solver (`isExact = false`):
  \[
  U_{\text{out}}(x_2, y_2, z) = \frac{e^{ikz} e^{i\frac{k}{2z}(x_2^2 + y_2^2)}}{i \lambda z} \iint U_{\text{in}}(x_1, y_1) e^{-i \frac{2\pi}{\lambda z}(x_2 x_1 + y_2 y_1)} \, dx_1 dy_1
  \]
  where $\lambda = \lambda_0 / n$ and $k = 2\pi n / \lambda_0$.
- The output grid pitch scales proportionally to distance: $\Delta x_{\text{out}} = \frac{\lambda z}{N_x \Delta x_{\text{in}}}$, $\Delta y_{\text{out}} = \frac{\lambda z}{N_y \Delta y_{\text{in}}}$.
- The discrete Fraunhofer implementation enforces exact spatial centering at $(x=0, y=0)$ and strictly conserves integrated intensity $\sum |U_2|^2 \Delta x_{\text{out}} \Delta y_{\text{out}} = \sum |U_1|^2 \Delta x_{\text{in}} \Delta y_{\text{in}}$ via Parseval's theorem.
- Fraunhofer propagation requires strictly positive finite propagation distance $z > 0$ and far-field conditions $z \gg D^2/\lambda$ (Fresnel number $N_F = D^2 / (\lambda z) \ll 1$). Diagnostics report $N_F$, support source (caller diameter/extents or conservative full grid default), maximum paraxial parameter $\lambda \sqrt{f_{x,\max}^2 + f_{y,\max}^2} = \max(r_{\text{out}})/z$, maximum adjacent quadratic phase step $\Delta\psi_{\max}$ evaluated from exact discrete index differences $(2m_{\max}-1)$ across both even and odd grids, and combine diagnostic warning messages when $N_F \ge 0.1$, paraxial parameter $\ge 0.1$, or $\Delta\psi_{\max} > \pi$.
- Boundary conditions are periodic on the discrete sampling grid without automatic padding (`periodicBoundary = true`, `automaticPadding = false`).
- Phasor angles are range-reduced via `std::remainder(phase, 2*pi)` to keep arguments to `std::polar` in $[-\pi, \pi]$. Extreme distances or coordinates exceeding double-precision capacity ($kz \gtrsim 2^{52} \approx 4.5\times 10^{15}\text{ rad}$) will undergo numerical precision loss, and intermediate quadratic phase overflows throw explicit `std::overflow_error`.

## M3 Fourier Optics and Sampling Assumptions

[ADR 0006](adr/0006-fourier-optics-and-sampling-diagnostics.md) locks the complete convention. Its active summary is:

- The ideal Fourier-lens transform represents propagation from the front focal plane through an ideal positive thin lens to the back focal plane. Its ABCD system is $P(f)L(f)P(f)$ with $A=D=0$, $B=f$, and $C=-1/f$; it is not implemented as or described as Fraunhofer free-space propagation.
- For medium wavelength $\lambda=\lambda_0/n$, the output sampling is $\Delta x_f=\lambda f/(N_x\Delta x)$ and $\Delta y_f=\lambda f/(N_y\Delta y)$. The field scale is $\Delta x\Delta y/(i\lambda f)$ with global axial phase $e^{i2kf}$.
- Two ideal Fourier-lens transforms form an ideal 4-f relay: the periodic-grid image is inverted, coordinate magnification is $M=-f_2/f_1$, complex-amplitude magnitude scales by $f_1/f_2$, and transverse integrated intensity is conserved.
- Fourier-plane low-pass, high-pass, and band-pass elements are ideal hard-edged scalar amplitude masks with radii measured in physical metres. Samples exactly on a circular boundary are transmitted; blocked samples are set to zero without phase or edge smoothing.
- A circular pupil has coherent amplitude cutoff $\nu_c=a/(\lambda f)$ and normalized intensity PSF $[2J_1(2\pi\nu_c r)/(2\pi\nu_c r)]^2$. The exposed MTF is explicitly the incoherent intensity MTF with cutoff $2\nu_c$; it is not labelled as a coherent MTF.
- The reported $\mathrm{NA}_{\text{paraxial}}=na/f$ is only the Fourier-model mapping. The response reports $a/f<0.1$ as its paraxial validity gate and does not claim exact geometric or vector high-NA behaviour.
- Large global axial phase is range-reduced before centred-index shift phases are added. This avoids turning cancellation error in a physically global phase into a spatially varying numerical phase error.
- The model is monochromatic, coherent, scalar, paraxial, and ideal. It does not include finite lens aperture, aberration, polarization, vector high-NA behaviour, automatic padding, or resampling.
- Axis Nyquist half-angle is $\theta_N=\arcsin(\min(1,\lambda/(2\Delta x)))$. Angular requests beyond this limit are reported as aliased; the solver is not silently restricted or modified.
- Periodic wrap-around and required padding use the conservative centred-support envelope $D_\text{required}=D+2|z|\tan\theta$. Caller-provided support must contain every non-zero field sample.
- The maximum sampled radial frequency uses the exact discrete FFT bins, including the smaller maximum positive/negative magnitude on odd grids, and is compared with $1/\lambda$ to report sampled evanescent content.
- Angular-spectrum maps use centred physical frequency coordinates. Bins at or below $1/\lambda$ are propagating; bins strictly above it are evanescent and report the positive decay frequency $\sqrt{f_x^2+f_y^2-1/\lambda^2}$.
- Arbitrary-plane probes preserve a fixed discrete transverse sample across requested positive or negative ASM distances. At exactly $z=0$ the source field is returned without applying the solver's evanescent cutoff.
- Sampling diagnostics report risks only. They never silently pad, suppress spectral bins, change propagation settings, or introduce device-specific performance limits.
