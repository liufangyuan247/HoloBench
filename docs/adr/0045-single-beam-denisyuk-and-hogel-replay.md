# ADR 0045: Illumination-gated Denisyuk and detached hogel replay

Date: 2026-09-05
Status: Accepted (bounded scalar model)

## Recording and optical truth

A diffuse object can opt in to `requiresIllumination`. It no longer emits an
independent root beam. The ordinary geometric tracer must deliver an incident
ray to its front-facing rectangular reference footprint. A recording plate can
collect the incident reference field while transmitting its explicit
`recordingPowerTransmission` fraction. Zero transmission preserves legacy
termination. The return child inherits wavelength and coherence, retains the
incoming optical path, and carries incident power times `diffuseReflectance`.
Its parent branch ID links it to the complete illumination trace. The child's
local propagation path starts at the placed object. The incoming optical path
is included once in its source phase before existing layered diffuse-wave
propagation. Subsequent object scattering is absorbed: this is a single-scatter
model, not an iterative light transport solver.

This is a **centre-ray illumination gate with nominal aggregate scattered
power**, not spatial radiometric illumination of every primitive surface. The
existing layered scalar Lambertian rough-phase source model supplies outgoing
shape and depth. It does not compute partial beam overlap, surface illumination
phase gradients, spatial shadow maps, polarization, or multiple scattering.
The substrate is a zero-thickness scalar transmission for routing; material
thickness still controls the stored volume-grating model. No substrate Fresnel
reflection or refractive optical path correction is claimed. Blocking the
incident chief ray prevents emission; this does not certify complete beam
clearance. Missing/misaligned object illumination cannot silently fall back to
the old independent source.

## Reconstruction-grade CHIMERA exposure

The legacy bounded exposure remains available for diagnostics/checkpoints.
`retainShowroomRecording` instead preserves the requested physical hogel window
and chooses a power-of-two grid large enough for both external incident centre
carriers and their product, with 25% headroom. The grid is at most 2048 per axis;
unsupported size/angle combinations reject the exposure with guidance rather
than shrinking the physical hogel or pretending an aliased preview is valid.
This carrier check is necessary, not a proof of convergence of all SLM/object
spatial frequencies. Existing finite local wave windows, ideal single Fourier
lens, paraxial envelope projection, and material calibration limits remain.

Freezing occurs inside the exact staged, SLM-command-bound channel scene. The
frozen asset retains its plate-local centre, wavelength, material and normalized
complex object times conjugate-reference field. It is never re-stamped to the
unstaged Bench revision. Only a complete RGB exposure is published. The UI's
background job owns copies of scene, recipe, dataset, plan and SLM calibration;
it uses the deterministic CPU backend. Results from a changed Bench/plan are
discarded. Checkpoint-only exposures explicitly require re-exposure for replay.
Cancellation is cooperative between sampled fields/channels; an in-flight
FFT completes before the cancellation is observed. Partial RGB results are
never published. Closing the application also requests cancellation.

The showroom opens the selected hogel's detached recordings. Users can select
one of the recorded wavelengths and replay it with the existing scalar observer.
This is monochromatic intensity, not a source-view texture and not white-light
RGB compositing. A single hogel is a local angular sample of a stereogram; it
cannot be expected to reproduce the complete multi-hogel object on its own.

## Numerical conventions

The existing negative-sign forward DFT and inverse 1/(Nx Ny) normalization,
exp(+i k z) propagation, SI metres, explicit plate-local X/Y and preferred
Helmholtz hemisphere remain unchanged. The observer uses zero-padded finite
windows and excludes evanescent orders. It does not voxelize the laboratory.
Observer output remains bounded to 2048 per axis; the existing propagator pads
that window internally. A carrier-resolved recording may still lose energy at
finite boundaries, which is reported. No new high-NA or white-light validation
is claimed.

## Persistence and geometry

Optional object illumination / plate transmission fields keep old Bench files
unchanged when defaults apply. An optional embedded CHIMERA recipe stores hogel
pitch and grid with the complete Bench and participates in edit history.
Resizing changes plate dimensions and recording windows, preserving other
instrument poses. Pitch also defines the exposure step and nominal square
recording window; the actual optical spot depends on the lenses and apertures.

Detached format 1 single-channel files remain readable. Format 2 bundles hold
one to three format 1 channel payloads under a SHA-256 envelope. Each channel
has at most 2048 squared complex samples; single files are bounded to 256 MiB
and bundles to 768 MiB. JSON is an inspectable reference format, not a compact
production material cache. Replay needs no source images or live Bench.
An optional `focus_depth_m` field is an initial accommodation hint for directly
observed diffuse objects. It does not alter coupling, grating or propagation;
older payloads without the field default to focusing at the plate.

## Validation

Analytic tests check the single-beam 2d object/reference optical path difference,
parent/coherence identity, transmission times reflectance power, blocker gating,
recording/replay, carrier checks, detached file round trips, and transactional
hogel resizing. The default 1 mm CHIMERA test executes all three actual SLM
channels and requires nonzero detached pupil/sensor signals after discarding
source workflow data. Application smoke covers single-beam recording, plate
mouse rotation, and a 0.25 mm CHIMERA exposure entering the actual showroom.
Named final run results are recorded in PROJECT_STATE.md.
