# HoloBench

HoloBench is an interactive 3D optical bench, a multi-fidelity physics simulator, and a long-term R&D tool for CHIMERA-like holographic printing.

Its product north star is a digital twin of an optical laboratory: experiments
must emerge from reusable instruments, validated model families, and placed
measurements rather than hard-coded experiment screens. See the
[product vision](docs/PRODUCT_VISION.md) for the complete contract and the
honest boundary between extensibility and physics currently implemented.

**Milestone status**: M1-M6 physics/reference foundations, the M7/M8
free-form holography sandbox, and the M9 CHIMERA virtual-printer automation are
validated in their documented domains. M10 is active: HoloBench is becoming an
extensible digital twin of an optical laboratory, with parameter-driven PCG
instrument bodies, constrained mechanical assemblies, explicit optical proxies,
and calibrated measurements. Instrument meshes are generated rather than fixed
imported assets, and remain disposable visual caches rather than solver truth. The
hardware interaction gate now assembles transmission, reflection/Denisyuk, and
RGB full-colour experiments from an empty Bench through real shelf, transform,
alignment, spectrum, Record, and Reconstruct input. The application includes an editable
3D bench with typed placeable components, geometry-derived branched rays,
ordinary transmission/reflection/RGB presets, placed thin-hologram recording,
and reconstruction on placed Screen/Probe components. Reflection-volume
recording derives the grating from the actual opposite-side plate branches and
reconstructs a Bragg-weighted sampled field on a placed reflection-side
Screen/Probe. The RGB preset records and replays three independent spectral
channels and combines only their display intensities. CHIMERA recipes now build
an ordinary editable 23-component Bench and drive hashed view/hogel data,
selected or resumable RGB exposure, bounded reconstruction, camera preview,
and transparent parameter sweeps on that same scene. Steam/store/distribution
work is removed from scope. For the current repository state and roadmap, see
[docs/PROJECT_STATE.md](docs/PROJECT_STATE.md) and
[HoloBench_CHIMERA_Project_Plan.md](HoloBench_CHIMERA_Project_Plan.md).

For a direct, Bench-first walkthrough of free placement, transmission,
reflection/Denisyuk, RGB full-colour, and CHIMERA workflows, see the
[Optical Bench operation guide](docs/OPTICAL_BENCH_GUIDE.md).

The free-form viewport now has an always-visible searchable component shelf.
The same Bench header exposes Empty, Transmission, Reflection/Denisyuk, RGB,
and CHIMERA entry points; each example is an ordinary editable scene, and each
holography entry also selects its matching recording mode instead of inheriting
stale mode state from the previous experiment.
Clicking places a component at the current table view centre; dragging a
component into the viewport intersects the camera ray with the horizontal
optical table and places it at that physical location. The Inspector remains
available for exact transforms and physical parameters. A selected component
shows colour-coded X/Y/Z handles: Move constrains to world axes, Rotate uses
the component's local axes, and configurable translation/rotation snapping
supports repeatable optical alignment.

A compact alignment bar beside the Bench, mirrored by precision controls in the
Inspector, operates on those same ordinary transforms: aim local +Z at another
component, project onto and copy a target optical axis, match height, place at a
signed target-axis spacing, or snap to the nearest finite visible beam segment.
Each action retraces the bench and participates in history/autosave; it creates
no hidden optical connection.

Selecting a laser or object/wavefront source exposes R/G/B spectrum presets;
lasers also expose an RGB preset with three independent channels. A
single-colour preset preserves the source power, while RGB divides the laser's
existing total power equally and assigns matching per-colour coherence IDs.
Object sources remain explicitly single-channel, so a full-colour object is
built from three ordinary placed sources rather than hidden wavelength mixing.

Selecting a holographic plate now opens a compact experiment bar in the
Optical Bench itself. It reports the object/reference incidence and available
transmission, reflection, and strict RGB pairings, then provides Record,
replay-illumination, placed Screen/Probe selection, Reconstruct, and
current/stale state without requiring the long Inspector workflow. Auto mode
records only an unambiguous single pair or exactly three independent RGB
pairs. New thin/RGB recipes preflight the bounded plate response and persist a
measured relative-exposure rescale when the default reference would clip; they
never suppress clipping or mix wavelengths.

The hardware OpenGL smoke drives these controls with real ImGui mouse events:
it clears the Bench, drags a laser and plate from the shelf onto camera-derived
table positions, drags a constrained world-axis handle, clicks Aim +Z and
checks the resulting physical frame, presses E and drags a local rotation
handle, clicks Snap to beam, and presses W to restore movement. It then clears
the Bench three times and assembles transmission, reflection/Denisyuk, and RGB
full-colour experiments entirely from shelf components before Record and
Reconstruct; a later shelf edit must stale and hide the RGB result.

A current reconstruction is drawn back onto the actual placed Screen/Probe as
a corner-projected texture quad using that component's physical extent and rigid
transform. The overlay is keyed to the replay observation ID and exact scene
revision, so editing the bench removes stale imagery instead of leaving an old
result on the table.

An ordinary Screen / Detector or non-blocking Field Probe can also measure the
current placed complex field. The Bench switches the same revision-bound result
between intensity, peak-relative dB, and validity-masked phase. Inspector cursor
and cross-section tools report local physical coordinates, complex amplitude,
W/m^2 intensity, integrated power, wavelength, coherence identity, and
horizontal/vertical profiles; they never sample the rendered 8-bit texture as
measurement truth.

For supported ray-routed paths, placed ideal lenses, mirror/splitter clear
areas, apertures, pinholes, and SLM finite pixels/dead space now act on a
beam-following local complex field before plate recording. Ideal mirror and
splitter folds transport the transverse frame explicitly; tilted zero-thickness
masks use their projected physical footprint, and the final oblique plate
restores the resolved centre carrier. Tilted powered lenses, real-prescription
paths, and high-NA/vector effects remain explicit limitations. Thin, RGB, and
reflection-volume reconstruction supports both bounded decentered parallel
Screen/Probe planes and non-grazing rotated observation planes through explicit
2x-padded shifted or rotated angular-spectrum propagation with rejected-band
and interpolation diagnostics.

Placed SLMs now carry an editable, persisted amplitude/phase command with
uniform, wrapped linear-ramp, or checkerboard patterns, declared bit depth and
phase range, plus a stable manual/automation provenance ID. The command is
evaluated in the SLM's actual projected pixel coordinates and therefore changes
the complex field used by transmission, reflection, and RGB recording.

Recording a placed thin, reflection-volume, or RGB hologram now writes a
versioned recipe into the ordinary bench project. The recipe keeps stable
source-to-plate branch selectors, sampling, response, and material parameters;
large complex fields remain disposable results. After load, the plate
Inspector reports whether each recipe still resolves uniquely and can
recompute it without silently selecting another path.

M9 now has a strict versioned CHIMERA recipe and deterministic recipe-to-bench
compiler. The canonical recipe creates a normal 23-component RGB reflection
printer layout with placed SLM/relay/stop object arms, folded mirror/splitter
reference arms, one volume plate, one reconstruction Probe, and three ordinary
M8 recording recipes. The Lab can build the canonical layout or load a recipe
JSON, displays explicit FOV/NA/sampling/material constraints, and leaves every
generated component editable through the same Bench UI and save format.
The same automation path now generates hashed hogel/angular data and RGB
exposure events, reconstructs bounded directional hogel views with M8 replay
efficiency and Airy cross-talk evidence, and evaluates bounded deterministic
parameter sweeps. Sweep output keeps every candidate recipe, constraint,
metric, and rejection reason; it does not claim an exposure optimum until a
measured material-dose response is available.
Real perspective content can enter the same hashed data chain through a
decoder-neutral RGB raster adapter with explicit linear/sRGB transfer and
area-weighted hogel resampling. A strict P3/P6 PPM loader supplies the built-in
dependency-free file path; richer image decoders can target the same boundary.
Single-hogel execution can also attach the existing measured complex SLM LUT
and a strict measured material dose-response LUT. It derives total and
fringe-modulation dose from the actual sampled object/reference fields and uses
the calibrated index modulation and shrinkage in the ordinary M8 recorder.
The deterministic CHIMERA sweep can consume the same calibrations on a bounded
representative hogel, retain every RGB dose/material metric, and rank exposure
candidates without extrapolating beyond the measured domain.
Directional reconstruction can now feed a bounded finite-pupil camera: pupil
position selects arriving hogel/view rays, wavelength-specific Airy kernels
deposit them on a finite sensor, and a strict measured spectral LUT maps the
independent optical wavelengths into linear camera RGB response.

## M1 Features

- **Optical Sources & Elements**: Point Source, Collimated Source, Ideal Thin Lens, Circular Aperture, Screen / Detector Plane, Planar Mirror, and Planar Dielectric Interface.
- **Geometric Ray Tracing**: Deterministic CPU ray-plane intersections, paraxial thin-lens refraction ($1/f = 1/u + 1/v$), specular reflection, Snell's law refraction, and Total Internal Reflection (TIR).
- **Physical Analysis & Diagnostics**: Real, virtual, and collimated/infinity image plane prediction, Numerical Aperture (NA) cone visualization, and automated warnings for off-axis paraxial approximations and rear-aperture clipping.
- **Fixed-axis 3D Reference UI**: Orbit/pan/zoom camera, metric world grid, ray segment renderer, and lens/screen `+Z` gizmos around a fixed point-source/lens/aperture/screen scene. M7 replaces this with a dynamic component sandbox.
- **Project Serialization**: Versioned JSON document model with semantic and byte-stable round-trip persistence.

## M2 Features

- **Sampled Wave Fields**: Complex and scalar 2D fields with explicit SI sampling, wavelength, and refractive-index metadata.
- **CPU Reference Solvers**: Deterministic radix-2 FFT, Angular Spectrum Method, Fresnel transfer-function propagation, and Fraunhofer far-field propagation.
- **Wave Sources & Elements**: Plane waves, Gaussian beams, circular/rectangular/double-slit apertures, and ideal thin-lens phase screens.
- **Field Observables**: Linear intensity, decibel intensity, wrapped phase with validity masking, and integrated relative intensity.
- **Independent Validation**: Analytic diffraction oracles plus three full-field cross-validation cases generated with `waveprop 0.0.12`; validation tooling is not a runtime dependency.
- **Interactive GPU Propagation**: Fused OpenGL 4.6 compute path for upload, FP32 FFT, spectral transfer, inverse FFT, and download, with no silent CPU fallback and CPU-reference parity tests.
- **Wave Detector UI**: Apply-gated source/aperture/lens/propagation controls plus intensity, log-intensity, and wrapped-phase views with hover and click-lock complex-sample probes.

## M3 Features

- **Fourier Optics**: Ideal Fourier lenses, 4-f relays, coherent spatial filters, sampled Airy PSF, and explicitly incoherent MTF.
- **Sampling Debugger**: Angular-spectrum classification, Nyquist/padding/wrap warnings, arbitrary-plane probes, and four-plane 4-f visualization.

## M4 Feature Set

- **Engineering prescription tracer**: Plane, sphere, conic, and even-asphere surfaces; rigid decenter/tilt; SI Cauchy/Sellmeier materials; sequential wavelength-aware refraction, clipping, TIR, and optical path evidence.
- **Spot and chromatic analysis**: Physical image-plane samples grouped by field and wavelength, rejected-ray evidence, RMS/geometric radii, best axial focus, and longitudinal colour shift.
- **Real Lens Workbench**: Versioned JSON/CSV import/export, interactive surface/material/field editing, wavelength-coloured XZ ray/surface visualization, XY spot plot, per-field statistics, and explicit limitations.
- **Independent engineering validation**: Five committed plano-convex, meniscus, achromat, conic, and even-asphere prescriptions compare every surface hit, outgoing direction, optical path, spot coordinate, best focus, and longitudinal colour shift with pinned Optiland 0.6.2 data and hashes.
- **Named performance gate**: The complete default 729-ray workbench refresh records p50 **6.422 ms**, p95 **6.594 ms**, and max **6.641 ms** on the reference Intel Core i7-9750H, meeting the p95 < 50 ms budget.

## M5 Feature Set

- **SLM CPU reference**: Ideal amplitude/phase modulation plus a finite pixel
  grid with physical pitch, independent X/Y fill factor, opaque dead space,
  phase range, and bit-depth quantization.
- **Measured and LCD response paths**: Versioned wavelength/command complex-
  response LUTs with deterministic JSON persistence, plus an explicitly limited
  Jones-retarder/input-polarizer/analyzer teaching model with RGB filter layouts.
- **Coherence and interference**: Fully coherent complex-field addition and a
  scalar mutual-coherence intensity model with explicit Gaussian/exponential
  1/e coherence-length conventions.
- **Angular experiment pipeline**: Headless `Laser -> SLM -> Lens -> Angular
  Probe` orchestration reports multi-wavelength angular coordinates, a selected
  pixel angular PSF, analytic-versus-measured pixel-to-angle mapping, and
  reference-beam interference.
- **Apply-gated SLM lab**: A dockable teaching workflow visualizes interference,
  angular intensity, and selected-pixel PSF without recomputing per frame;
  measured LUT import/export keeps calibration provenance visible.
- **Versioned experiment projects**: Strict, byte-stable M5 JSON preserves the
  complete draft experiment, embedded measured response, and calibration
  provenance independently from the optical-bench project schema.
- **Named CPU performance gate**: The fixed 128-square, three-wavelength,
  ideal/LUT/LCD full refresh records p95 **188.482 ms** with Clang and
  **278.183 ms** with MSVC on the reference i7-9750H, below the 350 ms budget.

## M6 Feature Set (complete)

- **Thin-hologram reconstruction foundation**: M6 records fully coherent
  object/reference interference into an explicit bounded amplitude response,
  preserves the physical full replay, and separately exposes ordinary virtual
  and conjugate real-image orders with complex-field quality metrics.
- **Phase-only hologram foundation**: Wavelength-specific commanded phase is
  wrapped and optionally quantized on a circular code space, with explicit
  invalid-target masks and RMS/maximum circular phase-error diagnostics. Ideal
  replay applies unit-modulus transmission, preserves illumination intensity,
  and rejects wavelength, medium, or grid mismatches rather than pretending a
  commanded phase pattern is achromatic. A back-propagate/encode/replay/forward-
  propagate workflow reports best-fit complex gain, matched-mode power, complex
  residual, and intensity residual so discarded amplitude and phase
  quantization remain measurable.
- **H1-to-H2 transfer and RGB basics**: Conjugate H1 replay supplies the H2
  object wave, H2 records/replays an explicit thin transmission order, and a
  signed image distance classifies the image on the negative side, transplane,
  or positive side of H2. Red, green, and blue run as independent coherent
  wavelength channels on a shared transverse grid, with no artificial colour
  offset and no claim that thin H2 represents a reflection volume hologram.
- **Diffraction-order placement diagnostics**: Ordinary and conjugate replay
  predict the zero/twin carrier centres relative to the desired image order and
  report propagation, Nyquist sampling, physical separation, and periodic-
  window escape independently. These diagnostics never filter an order out of
  the physical full replay.
- **Holography Lab product foundation**: A deterministic two-feature complex
  object generator drives the complete RGB H1/H2 pipeline. Draft/applied state
  prevents recomputation before Apply, display-only plane/channel changes avoid
  physics work, and a strict format-v3 JSON project preserves every grid,
  spectral, object, reference, response, placement, and volume-grating
  parameter plus project provenance byte-stably while migrating existing v1
  and v2 projects.
- **Interactive Holography Lab**: A docked workflow edits the grid, independent
  RGB media, complex Gaussian object, H1/H2 positions, recording references,
  and thin-plate responses. It renders H1 exposure/real-image and H2
  exposure/replay planes, exposes signed transplane placement and zero/twin
  sampling diagnostics, and keeps all FFT work behind explicit Apply. The
  OpenGL smoke gate requires a drawable holography texture and a semantic
  experiment-project round trip. The same Apply gate controls separate volume
  geometry, thickness, index modulation, wavelengths, internal angles, and
  shrinkage, with coupling/detuning/efficiency shown beside the thin views.
- **Separate volume/Kogelnik reference**: A distinct lossless sinusoidal phase-
  grating model derives transmission or reflection Bragg mismatch from record/
  replay wavelength, internal angle, thickness, and isotropic shrinkage. The
  scalar-TE coupled-wave solution exposes coupling, detuning, grating period,
  propagating-order state, and efficiency without reusing or relabelling the
  thin-mask pipeline.
- **Named CPU performance gate**:
  `holography/rgb_h1_h2_32_64_cpu_full_refresh` runs complete 32x32 and
  64x64 RGB H1/H2 refreshes plus volume diagnostics. Reference Clang/MSVC/GCC
  p95 values are **38.675 / 51.780 / 43.410 ms**, all below the platform-neutral
  150 ms budget with identical checksum `1512.57504282`.

## Accepted sandbox milestones

- **M7 — Free-form 3D Optical Bench (accepted)**: One dynamic project with a placeable
  component library, arbitrary rigid transforms, viewport editing, and
  deterministic branching/merging optical paths. RGB lasers, mirrors,
  splitters/combiners, lenses, apertures, spatial filters, SLMs, screens,
  probes, and holographic plates must interact through their actual 3D
  placement. See [the M7 brief](docs/milestones/M7_OPTICAL_BENCH_SANDBOX.md).
- **M8 — Holography Sandbox (accepted)**: Transmission, reflection/Denisyuk, and RGB
  full-colour hologram recording and reconstruction are driven by coherent
  branches reaching placed plates. See [the M8 brief](docs/milestones/M8_HOLOGRAPHY_SANDBOX.md).
- **M9 — CHIMERA Automation (accepted)**: A versioned recipe compiles to an editable
  CHIMERA-like bench, generates hogel/angular and SLM data, creates RGB exposure
  events, and simulates bounded reconstruction. See [the M9 brief](docs/milestones/M9_CHIMERA_AUTOMATION.md).
- **M10 — Procedural Digital-Twin Instruments (active)**: Every current Bench
  component now has a bounded parameter-driven solid body rendered independently
  from diagnostic rays and proxy outlines. Persisted base/post/XYZ/tip-tilt
  assemblies now drive both direct viewport controls and the explicit solver
  optical frame. The remaining slices add calibration identity and general
  measurement closure. See [the M10 brief](docs/milestones/M10_DIGITAL_TWIN_INSTRUMENTS.md).

The existing ten guided panel workflows, templates, localization/font,
progress, history, and teaching benchmarks remain tested reference assets.
They are deferred as product UI because fixed controls cannot substitute for
building the experiment on the shared bench.

## Physical Assumptions & Limitations (M1)

- **Paraxial Approximation**: Thin lenses and ray propagation assume small angles relative to the optical axis ($+Z$).
- **Unmodeled Effects in M1**: Monochromatic ray tracing only; no Fresnel transmission/reflection loss, no polarization ($s/p$ states), no wave diffraction, and no recursive multi-bounce ray splitting.
- **Planar Interface Indices**: `nIncident` and `nTransmitted` are specified by the caller based on the propagation direction and are not automatically swapped for reverse-incident rays.

## Supported Development Environment

- Windows x64 or Linux x64
- CMake 3.28+
- Ninja or another CMake-supported generator
- A C++20 compiler
- OpenGL 4.6-capable GPU for the interactive application

Dependencies are pinned and fetched automatically via CMake FetchContent.

## Build and Test

Standard dev build and test suite (563 deterministic CPU/application cases,
including the OpenGL GPU test executable):

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Headless core/physics build and test (no display/OpenGL required, 561/561 tests passing):

```powershell
cmake --preset core-ci
cmake --build --preset core-ci
ctest --preset core-ci
```

Strict compiler diagnostics (`warnings-as-errors`):

```powershell
cmake --preset app-ci
cmake --build --preset app-ci
```

## Running the Application

Interactive 3D bench mode (targets display refresh with `vsync=1`):

```powershell
./out/build/dev/HoloBench.exe
```

Automated local OpenGL smoke test (renders 120 frames, verifies 0 GL errors, and exits with code 0):

```powershell
./out/build/dev/HoloBench.exe --smoke-frames 120
```

Named M6 CPU full-refresh performance gate:

```powershell
./out/build/dev/holobench_m6_benchmark.exe
```

Named M8 placed transmission, reflection, and RGB record/replay performance
gates (256x256 CPU reference fields):

```powershell
./out/build/dev/holobench_m8_benchmark.exe
```

Named M9 selected-hogel RGB exposure/reconstruction/camera resource gate
(use the optimized `core-ci` build for performance results):

```powershell
./out/build/core-ci/holobench_m9_benchmark.exe
```

Named 1920x1080 CHIMERA editable-Bench renderer gate (optimized application
build, 60 warm-ups plus the requested measured frames):

```powershell
./out/build/app-ci/HoloBench.exe --chimera-benchmark-frames 120
```

Automated 3D viewport throughput benchmark (disables VSync, forces per-frame `glFinish`):

```powershell
./out/build/dev/HoloBench.exe --benchmark-frames 300 --ray-count 5000
```

Automated 1024x1024 fused ASM recompute benchmark (5 warmups, 30 measured samples, `glFinish` around each sample):

```powershell
./out/build/dev/holobench_gpu_benchmark.exe
```

### Reference Benchmark Results

Tested on AMD Radeon Pro 5300M (OpenGL 4.6.0 Core, GLSL 4.60):
- **Resolution**: 1920 x 1080 window and viewport
- **Workload**: 5,000 rays, 10,000 displayed line segments, 3D grid, component meshes, gizmos, and ImGui overlays
- **Configuration**: 60 warmup frames, 300 measured frames, `vsync=0`, `gpu_sync=true` (`glFinish` per frame)
- **Throughput**: **1119.76 FPS** average
- **Frame Times**: p50 = **0.855 ms**, p95 = **1.275 ms**, max = **2.206 ms**

*Note*: The benchmark measures raw GPU rendering throughput with VSync disabled and synchronous CPU-GPU sync. In normal interactive mode (`vsync=1`), the application syncs to display refresh (e.g. 60 Hz). Earlier ~32 FPS observations on certain displays were due to window compositor swap pacing rather than GPU rendering bottlenecks.

The optimized M2 wave benchmark on the same AMD Radeon Pro 5300M was
revalidated on 2026-09-01 at p50 = **28.332 ms**, p95 = **30.335 ms**, and
max = **30.515 ms**, meeting the p95 < 50 ms budget. Debug builds are not
performance evidence. Twiddle selection is capability-driven: each newly
generated shader table is read back once and checked against the CPU reference,
with CPU-generated twiddles used only if the active implementation fails that
numerical probe. NVIDIA measurements remain an additive user-hardware
validation; vendor/model workarounds are prohibited.

## CI & Automated Workflows

- Windows and Linux CI workflows are defined in `.github/workflows/` with `warnings-as-errors` compilation and test execution. Remote GitHub Actions runs execute upon git push.

## Project Rules

- Visualization, optical models, and numerical backends are separate layers.
- Ray and wave solvers are distinct and explicitly declare their physical assumptions.
- Every physics feature requires an analytic or trusted-oracle validation test.
- GPU implementations follow a deterministic CPU reference implementation.
- GPU limits and compatibility choices come from runtime capability and numerical-correctness probes. Vendor, model, and driver allowlists/denylists are prohibited; a failing capability may select a narrowly scoped fallback without reducing precision, capability, or performance for GPUs that pass.
- A visually plausible result is not evidence of physical correctness.
