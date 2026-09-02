# ADR 0033: Revision-bound sampled wavefront evidence for volume holograms

- Status: Accepted
- Date: 2026-09-02

## Context

The reflection/Denisyuk volume model already derived the exact local grating
vector from the two routed centre directions and evaluated Bragg selectivity
with the declared plate material. Its placed reconstruction, however, sampled
the object and reference envelopes only when replay was requested. That was
equivalent for an unchanged simple source path, but it was not an honest
recording contract once an ordinary placed lens, SLM, aperture, fold, or other
supported wave transform shaped the exposure. A later sample could also use a
different grid or omit a required prescription resolver.

CHIMERA adds a related but distinct case. A bounded hogel preview deliberately
does not always resolve the optical transverse carrier; the exact analytic
grating vector remains valid, and the preview is still useful dose and SLM
evidence, but it must not be presented as a directly replayable sampled
carrier.

## Decision

### Recorded field evidence

- Product single-channel and RGB volume recording samples both selected
  branches at record time through the shared beam-following service. The
  resulting complex fields and their complete path diagnostics are retained in
  the revision-bound `VolumePlateRecordingResult`.
- The same verified `ILensPrescriptionResolver` used by global routing is
  passed into recording. A real-lens path with no resolver, an unknown
  prescription, an unsupported prescription domain, or any omitted supported
  wave transform rejects the whole recording.
- Recorded object and reference evidence must identify the exact plate,
  revision, branch IDs and roles; share wavelength, external refractive index,
  dimensions, pitch, extent, and centre; and carry finite non-zero intercepted
  power. The attachment is transactional.
- The geometric-only overload remains for analytic Kogelnik and parameter-sweep
  work that does not claim a sampled exposure. Interactive product recording
  uses the refined overload.

### Replay semantics

- Reflection reconstruction uses the retained object and reference fields. It
  does not recompute those exposure fields after recording. The recorded
  reference field is also reused when that same physical branch illuminates
  the plate for replay.
- Requested replay sampling must exactly match the retained record window.
  Changing dimensions, extent, centre, or external medium rejects instead of
  resampling the stored optical truth.
- The reconstructed complex field remains the recorded object-reference phase
  product driven by the physical replay field. Its total outgoing power is
  normalized separately to the evaluated Kogelnik diffraction efficiency.
  These are complementary scalar models, not a claim of vector coupled-wave
  propagation through the full three-dimensional emulsion.

### Automation evidence

- Automation that already sampled an SLM exposure may attach those exact fields
  through `recordVolumePlateFromSampledFields`; it cannot substitute fields
  from another branch, plate, revision, wavelength, grid, or medium.
- A bounded automation preview may retain `carrierSampled=false`. This status
  remains visible and direct field replay rejects it. The analytic grating
  vector and calibrated dose workflow may still consume the explicitly limited
  evidence.
- CHIMERA now retains both its sparse-SLM object field and reference field in
  each directly executed M8 volume-recording result instead of keeping
  detached diagnostics only.
- A CHIMERA batch crosses an explicit compaction boundary after each complete
  RGB hogel has been summarized. Its checkpoint and returned batch exposures
  retain independent object/reference path diagnostics, dose evidence,
  Kogelnik efficiency, and directional-reconstruction evidence, but discard
  the six sampled preview fields. A standalone exposure still returns the full
  recording evidence. This keeps slice memory from growing by six complex
  grids per hogel without weakening the declared compact batch contract.

## Consequences

- A placed verified real lens can shape reflection/Denisyuk and RGB volume
  recording through the same general Bench path already used for screens and
  thin-transmission holograms. There is no experiment-specific solver graph.
- Inspector diagnostics can show the exact applied wave elements and real-lens
  prescription IDs for each recorded branch.
- Derived sampled fields are runtime evidence, not embedded project payloads.
  The persisted recording recipe recomputes them after project load against the
  current revision and verified external assets.
- The accepted domain remains bounded scalar, coherent, low-NA beam-following
  propagation plus the existing Kogelnik efficiency model. Vector/high-NA,
  polarization-selective, scattering, and full three-dimensional index-volume
  solvers remain explicit future model families.
