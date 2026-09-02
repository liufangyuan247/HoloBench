# M10 — Procedural digital-twin optical instruments

## Goal

Turn the validated free-form Bench into a parameter-driven optical-instrument
digital twin. A placed component must be a believable adjustable instrument,
not a line symbol, while the optical solver remains tied to an explicit hidden
proxy rather than to decorative render triangles.

The long-term product target is an extensible digital twin of an optical
laboratory: a user should be able to construct any experiment supported by the
installed, validated physical models by combining ordinary instruments, without
waiting for that experiment to become a hard-coded product feature. Experiment
presets may place and configure those instruments, but may not create hidden
physics or special-case solver graphs. Unsupported domains must be reported
honestly and become explicit model extensions rather than plausible-looking
animations.

## Instrument contract

Each instrument definition has four coordinated layers:

1. **Optical truth** — SI-valued apertures, surfaces, media, spectra, response,
   and a rigid optical frame owned by `optics/`.
2. **Mechanical assembly** — parent/child parts and constrained degrees of
   freedom such as post height, stage travel, and mount yaw/pitch. A mechanical
   adjustment updates the optical frame through an explicit transform.
3. **Procedural appearance** — deterministic triangle meshes generated from
   the same dimensions and state. Fixed imported meshes may later supply small
   decorative parts only; they cannot define clear aperture or optical truth.
4. **Calibration evidence** — optional versioned serial/model identity,
   measured tolerances, response LUTs, and pose offsets. Missing calibration is
   visible and never replaced with a vendor-name heuristic.

Render meshes are disposable caches. They are not persisted, do not route
rays, and do not become collision or optical truth merely because they look
physically detailed.

## Delivery slices

### M10.1 — PCG solid-rendering foundation

- A headless, deterministic instrument-mesh generator with bounded tessellation.
- Parameter-scaled boxes, cylinders, plates, bezels, housings, screens, and
  virtual probe surfaces for all current Bench component kinds.
- Separate depth-tested triangle rendering with world-space normals and simple
  neutral lighting; diagnostic rays, proxy outlines, axes, and gizmos remain a
  separate line layer.
- Geometry tests prove finite vertices, normalized normals, non-degenerate
  triangles, rigid-transform following, and physical-size response.

### M10.2 — Adjustable mechanical assemblies

- Persisted mount/base/post/stage state with explicit travel and rotation limits.
- Direct manipulation of meaningful handles and knobs rather than raw abstract
  transforms for ordinary play; exact SI editing remains available.
- Optical proxy frames update from mechanical state without using render meshes
  as solver input.

Implementation delivered: the generic base/post/XYZ/tip-tilt assembly is
persisted in Bench format v4, validates against its resolved optical frame, and
drives both PCG parts and brass viewport controls. Ordinary whole-instrument
move/rotate/alignment operations rebase the assembly without changing its
readings. See [ADR 0026](../adr/0026-mechanical-assembly-and-optical-frame.md).

### M10.3 — Digital-twin identity and calibration

- Versioned generic instrument specifications and optional manufacturer/model
  metadata that never select numerical backend behavior.
- Measured clear aperture, pose offsets, coatings/material response, SLM,
  detector, and stage calibration hooks with provenance and validity domains.
- Nominal and calibrated modes are visibly distinguished.

Implementation in progress: Bench format v5 gives every component a strict
generic specification and optional instance metadata plus hashed external
calibration references. The Inspector attaches/removes those references and the
Inspector/viewport distinguish nominal, calibrated, and stale state. Stored
evidence does not claim to affect physics until the corresponding model adapter
resolves and validates the asset. See [ADR 0027](../adr/0027-instrument-identity-and-calibration-references.md).

The first applied calibration kind is complete: imported real-lens JSON/CSV
assets carry an exact bounded-file SHA-256 and immutable ID/content/provenance.
A placed assembly binds the verified asset through one calibrated component
edit. Project load resolves relative sources, verifies the hash before parsing,
checks format/content/specification identity, builds a fresh catalog, and fails
transactionally on drift. Every later Bench edit revalidates the binding, and
editing imported optical truth requires a new ID plus reload before rebinding.
See [ADR 0032](../adr/0032-hashed-lens-prescription-assets.md).

Physical Screen / Detector response assets now use the same fail-closed
lifecycle. Exact JSON bytes are hashed before parse, calibration IDs remain
immutable, relative project paths restore transactionally, and wavelength /
temperature validity is checked at capture. A bound response actually drives
the placed CHIMERA camera and retains ID/hash/temperature evidence; a virtual
Field Probe is never labelled as calibrated hardware and uses the explicit
nominal preview. See
[ADR 0037](../adr/0037-hashed-placed-detector-response.md).

Placed SLM response assets now close the same loop for the existing scalar
complex-response LUT. Format-v1 JSON is read once with a 16 MiB bound, addressed
by the SHA-256 of those exact bytes, bound only to an ordinary placed SLM, and
restored transactionally with specification plus wavelength/temperature
checks. The shared routed-field service applies the response to persisted
manual/procedural commands and transient sparse rasters, so Screen/Probe
observation, thin/volume/RGB holography, Denisyuk replay paths, and CHIMERA use
one instrument truth. A placed binding and transient response are an explicit
conflict, never an implicit precedence rule. See
[ADR 0038](../adr/0038-hashed-placed-slm-response.md).

Measured optical pose now uses a strict format-v1 rigid-offset asset with the
same exact-byte, content-addressed, specification-bound, transactional
lifecycle. The persisted mechanical/PCG scene stays nominal; a separate
derived optical scene composes verified local offsets and is the input to
dynamic rays, Screen/Probe fields, holography, Denisyuk, and CHIMERA paths.
The renderer draws solids from the nominal scene and proxy outlines/rays from
the calibrated scene, while the Inspector exposes applied ID/hash/frame
evidence. Virtual Field Probes cannot claim hardware pose calibration. See
[ADR 0039](../adr/0039-hashed-optical-pose-calibration.md).

### M10.4 — Measurement and general experiment closure

- Field Probe amplitude, intensity, phase, dB, spectral-channel, cursor, and
  cross-section inspection.
- Same-wavelength coherent field merging through arbitrary supported routed
  components; different wavelengths remain independent intensity channels.
- Acceptance experiments cover diffraction, interferometers, imaging, Fourier
  filtering, transmission/reflection/RGB holography, and CHIMERA construction
  using the same ordinary instrument definitions.

Implementation in progress: an ordinary placed Screen / Detector or virtual
Field Probe now exposes revision-bound complex cursor samples, local physical
coordinates, amplitude, W/m^2 intensity, peak-relative dB, phase validity,
wavelength/coherence identity, integrated power, and horizontal/vertical
intensity sections. The same cached field switches between intensity, dB, and
wrapped-phase textures without re-propagation. Multiple direct
source-to-observation branches now partition into exact wavelength/coherence
channels; contributors within one channel add as complex fields, while channel
switching only rerenders the cache. One shared beam-following service now
applies mirror/splitter folds, apertures, aligned ideal lenses, explicit
spatial-filter pinholes, and finite SLM commands for both plates and placed
measurement planes. The selected observation Inspector exposes every branch's
ordered path, OPL, working grid, segment count, applied transforms, retained
warnings, and bounded drag/settled sampling controls. Unsupported incident paths
reject the complete observation.
The shelf's Mach-Zehnder starter is seven ordinary mounted instruments and uses
the same routing, interaction, propagation, merge, and measurement services as a
bench assembled from Empty; it is not an experiment-specific solver.
Placed Real Lens Assemblies now resolve stable prescription IDs through a
solver-facing runtime catalog. Global routing follows every placed sequential
surface and retains wavelength-dependent glass OPL; a bounded forward-coaxial,
centred, low-NA scalar split-step adapter applies physical surface sag,
apertures, intermediate refractive indices, and thickness to Screen/Probe
fields. Unsupported prescription geometry rejects rather than becoming an
ideal thin lens. The same resolver now drives FFT-refined single/RGB
thin-transmission plate recording and retains applied prescription IDs.
Imported prescription binding is an ordinary revision-advancing component edit,
and hashed project-asset restoration now fails closed on missing or changed
bytes. Single/RGB reflection-volume recording now retains both exact sampled
exposure fields and their applied prescription diagnostics; reconstruction
reuses those record-time fields and rejects a changed sampling grid. CHIMERA
attaches its bounded sparse-SLM object/reference previews to the same M8 record
contract while preserving an unresolved-carrier flag that direct replay must
reject. Single- and three-channel RGB reflection-volume reconstruction are now
explicit derived fields from the ordinary placed plate: each exact centre ray
follows the current Bench and each wavelength-separated sampled field reuses
the same beam-following element and real-prescription adapters to the selected
plate, Screen, or Probe. RGB channels combine only as observation-plane display
intensities. The Inspector retains per-channel plate-to-beam rotation, routed
component/prescription, boundary, and approximation evidence; no temporary
source, private holography graph, or PCG geometry participates in the physics.
The CHIMERA product camera now also resolves a user-selected placed Real Lens
Assembly and Screen/Probe. Every RGB chief ray traverses the wavelength-
dependent sequential prescription and authoritative Bench route; component and
surface apertures clip physically, the placed sensor pose sets the hit, and a
prescription-derived RGB paraxial EFL drives the bounded Airy readout. Results
retain exact revision, IDs, prescription, three focal lengths, per-channel
surface/coordinate/status evidence, and fail closed on missing assets,
reversed paths, ambiguous branches, or unsupported intervening optics. The
former request-driven ideal camera remains only an analytic oracle.
Each accepted channel also traces a deterministic 49-ray bundle spanning the
effective pupil. Prescription aberration, marginal clipping, and the actual
sensor's axial/tilted pose form a geometric spot whose individual intercepts
receive the wavelength-specific Airy core. Per-channel centroid/RMS/radius and
completed/rejected/sensor-hit counts are inspectable. This adds physical
defocus without claiming coherent aberrated-wavefront diffraction.
See
[ADR 0028](../adr/0028-revision-bound-placed-field-measurements.md) and
[ADR 0029](../adr/0029-coherent-merge-and-independent-measurement-channels.md),
plus [ADR 0030](../adr/0030-shared-beam-following-field-paths.md) and
[ADR 0031](../adr/0031-placed-real-lens-prescription-adapter.md),
[ADR 0032](../adr/0032-hashed-lens-prescription-assets.md), and
[ADR 0033](../adr/0033-recorded-volume-wavefront-evidence.md), plus
[ADR 0034](../adr/0034-derived-field-routing-from-placed-planes.md), and
[ADR 0035](../adr/0035-chimera-placed-prescription-camera.md), and
[ADR 0036](../adr/0036-chimera-prescription-spot-defocus.md), plus
[ADR 0037](../adr/0037-hashed-placed-detector-response.md),
[ADR 0038](../adr/0038-hashed-placed-slm-response.md), and
[ADR 0039](../adr/0039-hashed-optical-pose-calibration.md).

## Platform extensibility acceptance

M10 is not a claim that one scalar/paraxial solver already covers every optical
phenomenon. It closes the instrument and measurement architecture needed to add
new physical domains without redesigning the product. A new capability must be
expressible as reusable instrument behavior, an explicit propagation/interaction
model, declared validity limits, observables, and numerical/reference evidence.
It must not require a bespoke experiment screen.

The platform roadmap grows through composable model families: geometrical and
sequential imaging; scalar coherent diffraction and interference; polarization
and coating response; partially coherent and broadband light; non-sequential
stray-light/scattering; detector and source response; and, when separately
validated, nonlinear, ultrafast, quantum, or electromagnetic models. Fidelity is
selected by physical need and evidence, never by the name of an experiment.

## Architecture and quality rules

- Global routing remains rays plus local sampled 2-D complex fields. The room
  is never voxelized to make the visuals look more physical.
- Appearance parameters may follow optical/mechanical truth; optical truth may
  never be inferred from a tessellated mesh.
- PCG output is deterministic, finite, bounded, cacheable, and regenerated only
  when relevant component or presentation state changes.
- GPU behavior remains capability- and numerical-probe-driven. Instrument
  manufacturer/model identity is data, never a GPU workaround selector.
- Current scalar/paraxial limitations remain visible until a separately
  validated model extends them.
