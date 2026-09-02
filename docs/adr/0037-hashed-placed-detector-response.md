# ADR 0037 - Hashed placed-detector spectral response

## Status

Accepted for the M10 calibrated Screen / Detector camera path.

## Context

The camera solver already accepted a strict wavelength-to-linear-RGB response
table, but the product supplied that table as an application-owned nominal
preview. A placed detector could carry a generic `detector_response` reference,
yet the reference did not resolve verified bytes or affect the image. That was
calibration metadata, not a digital-twin instrument.

The existing response model is relative and linear. It does not contain sensor
area integration, quantum efficiency in electrons per photon, gain, exposure
time, noise, saturation, CFA geometry, temperature dependence, or display
colour management. Applying the model must not imply those missing properties.

## Decision

- A detector response asset is the existing bounded format-v1 JSON spectral
  LUT. Loading reads once, hashes those exact bounded bytes with SHA-256, then
  parses the same bytes. The
  parsed calibration ID is immutable within a runtime catalog: the same
  ID/content/provenance registration is idempotent, while reuse for different
  truth or provenance rejects.
- Only a physical `ScreenDetector` may bind `detector_response`. A virtual
  `FieldProbe` remains a non-hardware measurement plane and cannot claim a
  verified detector calibration.
- Binding replaces at most one detector-response reference, records exact
  format/source/hash/specification identity, declares wavelength and temperature
  validity, selects calibrated mode, validates the complete component, and
  commits through the ordinary Bench edit path.
- Project restoration resolves relative paths against the Bench file, verifies
  SHA-256 before parsing, checks format and content ID, validates that the
  declared wavelength domain is contained by the sampled LUT, builds a fresh
  catalog, and swaps it transactionally with the project. Missing, changed,
  mismatched, stale, or wrongly attached assets fail closed.
- CHIMERA camera capture selects response truth from the actual placed
  observation component at the declared operating wavelengths and temperature.
  A verified physical detector applies its catalog response. A component with
  no detector-response reference uses the explicitly named nominal preview;
  an existing but invalid reference never falls back.
- The result retains the response calibration ID, whether a placed verified
  asset was used, its content hash, and the evaluation temperature. UI, smoke,
  tests, and the named benchmark distinguish `verified placed calibration`
  from `explicit nominal preview`.
- Manufacturer, model, serial number, GPU vendor, renderer, and device identity
  never select the response or a numerical backend.

## Consequences

A user can import a response JSON, bind it to an ordinary placed physical
detector, save the Bench, reload it on another machine with a relative asset,
and obtain a camera image driven by the verified LUT. Byte tampering, ID reuse,
specification drift, invalid wavelength/temperature context, and virtual-probe
misbinding are deterministic errors.

The nominal preview remains useful for a virtual Probe and for projects without
measured detector data, but its status is explicit. The current LUT still maps
incident relative linear intensity to relative linear RGB signal. Absolute
photoelectrons, exposure, noise, saturation, CFA sampling, calibrated
temperature dependence, and ICC/display transforms remain separate reusable
detector models.
