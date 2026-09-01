# HoloBench product vision

## North star

HoloBench is a digital twin of a physical optical laboratory. A user should be
able to place ordinary optical instruments on a bench, assemble and align them,
adjust their physical and mechanical parameters, illuminate the layout, and
measure the resulting light field. Any experiment covered by the installed and
validated model families should emerge from that composition. It must not need
a hard-coded experiment screen or a private solver graph.

"Any optical experiment" is an extensibility requirement, not a claim that the
current release already models every physical regime. When an experiment needs
physics that is not installed or leaves a model's validity domain, HoloBench
must say so. The correct extension is a reusable instrument behavior,
propagation or interaction model, observable, and validation corpus—not a
plausible animation named after the experiment.

## Product contract

Every digital-twin experiment uses the same five kinds of truth:

1. **Instrument truth:** stable instrument class and specification, physical
   parameters, optical interfaces, spectra, materials, apertures, response, and
   declared validity limits.
2. **Mechanical truth:** bases, posts, stages, mounts, constraints, readings,
   and the explicit transform from the assembly to each optical interface.
3. **Calibration evidence:** optional instance identity and immutable external
   asset references with schema version, content hash, provenance, wavelength
   and environmental validity. Nominal, calibrated, and stale states remain
   visible.
4. **Numerical truth:** a chosen validated model family and solver backend with
   convergence, sampling, approximation, and rejected-domain diagnostics.
5. **Measurement truth:** placed screens, field probes, cameras, detectors, or
   meters produce revision-bound observables. A scene edit invalidates derived
   evidence instead of leaving an old picture on the bench.

Procedurally generated render meshes make instruments tangible and adjustable,
but they are disposable visual caches. Explicit optical and mechanical proxies
remain solver truth. Manufacturer, model, GPU vendor, GPU model, device ID,
renderer, and driver strings never select numerical behavior; GPU adaptations
are driven only by queried capabilities and numerical probes.

## Experiment composition rule

Presets, tutorials, automation recipes, and CHIMERA construction may only place,
configure, align, and operate ordinary Bench instruments through public project
and solver contracts. They may not introduce hidden optics. A double-slit
experiment, interferometer, microscope, holographic recorder, Denisyuk replay,
or CHIMERA-like printer is therefore a saved arrangement and procedure—not a
separate product mode with privileged physics.

## Multi-fidelity growth

The platform grows by installing composable, evidence-backed model families:

- geometrical and sequential imaging;
- scalar coherent diffraction, interference, and Fourier optics;
- polarization, Fresnel and multilayer coating response;
- partially coherent, broadband, radiometric, and detector response;
- non-sequential scattering, ghost and stray-light analysis;
- thick, anisotropic, nonlinear, ultrafast, electromagnetic, quantum, or other
  specialist models when their instruments and validation evidence exist.

The selected fidelity follows the physical question and requested observable.
No single solver is stretched outside its documented domain merely to keep an
experiment looking active.

## Completion direction

M10 establishes believable PCG instruments, constrained assemblies, explicit
identity/calibration evidence, and general placed-probe measurements. It is a
platform milestone, not the end of the laboratory. Subsequent work expands the
instrument library and validated model families while preserving the same
Bench, project, automation, provenance, and measurement contracts.
