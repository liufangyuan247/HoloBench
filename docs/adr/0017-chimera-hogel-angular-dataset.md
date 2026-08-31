# ADR 0017 - CHIMERA hogel and angular dataset

Status: accepted, 2026-09-01.

## Context

The CHIMERA automation path needs a deterministic boundary between source
view content and later printing events. A whole-laboratory wave volume is not
appropriate, and encoding the requested field of view as an arbitrary placed
SLM phase ramp would confuse Fourier-plane position selection with a sampled
wavefront command.

## Decision

- Format-v1 `HogelDataset` stores the source recipe identity, SI/radian units,
  hogel geometry, source perspective views, one angular sample per view and
  hogel, sparse per-hogel RGB SLM commands, diagnostics, and a content hash.
- The initial perspective-image contract contains one linear-RGB source sample
  per hogel. The canonical 5 by 3 view set is a deterministic synthetic oracle,
  not a claim that HoloBench has rendered a real scene. Future image adapters
  must resample real inputs into this explicit contract before generation.
- View angles map to the ideal Fourier-lens plane with
  `x = f tan(theta_x)` and `y = f tan(theta_y)`. Positive physical Y maps to
  decreasing raster row. Samples outside the finite SLM reject rather than
  clamp into a valid-looking command.
- Input views are sorted by vertical angle, horizontal angle, and stable view
  ID. Commands are emitted in row-major hogel order and fixed red, green, blue
  order, making the result independent of caller insertion order.
- The hash algorithm is named `fnv1a64-canonical-json-v1`. It detects accidental
  corruption and enforces byte-stable data products; it is not a cryptographic
  authenticity mechanism. Parsing is strict and recomputes the hash.
- Sparse command pixels are an automation data product. They are not yet a
  full placed-SLM raster transfer and do not bypass the ordinary editable
  `BenchProject` or the M8 recording path.

## Consequences

The canonical 8 by 6 hogel grid and 5 by 3 view grid produces 720 angular
samples and 144 RGB hogel commands. Analytic tests independently verify the
Fourier-lens position and raster coordinate, view-order canonicalization,
strict schema and unit rejection, hash corruption detection, and bounded
generation.

Exposure planning must consume these stable command identities, convert them
to explicit stage/SLM/laser events, and invoke the three compiled M8 recording
recipes. Real perspective-image ingestion, calibrated SLM lookup tables, and
hardware control remain later adapters; they are not silently inferred here.
