# ADR 0021 - CHIMERA perspective raster adapter

## Status

Accepted for the M9 source-image boundary.

## Context

`HogelDataset` deliberately consumes one linear-RGB sample per hogel for each
view. Its original 5 by 3 perspective set is a deterministic synthetic oracle,
not a real scene input. Camera renders and photographs normally arrive as
higher-resolution rasters and often contain sRGB-encoded values. Treating those
encoded values as optical intensity would produce the wrong SLM amplitudes.

The core repository has no general PNG/JPEG runtime decoder. The physical data
contract should not depend on a particular UI or image library.

## Decision

- `RgbRasterImage` is a decoder-neutral, normalized RGB raster boundary. The
  caller declares either linear or IEC 61966-2-1 sRGB transfer. Row zero maps
  to hogel row zero; orientation changes belong to the decoder/import UI and
  must not occur implicitly in the physics layer.
- `adaptPerspectiveRasterViews` validates stable view IDs, recipe FOV, finite
  normalized samples, exact dimensions, and bounded pixel counts. It converts
  sRGB to linear intensity before using exact pixel-area weights to resample
  each input into the recipe's hogel grid.
- The default hard limit is 16,777,216 input pixels per view. Adapted output is
  bounded by the existing 5,000,000-sample hogel contract. No full laboratory
  volume or hidden scene renderer is introduced.
- A strict zero-dependency Netpbm loader supports P3 text and P6 binary RGB
  files, including the standard big-endian 16-bit P6 representation. PPM does
  not reliably tag colour transfer, so callers must declare linear or sRGB.
- PNG, JPEG, EXR, camera, or renderer plugins can decode to `RgbRasterImage`
  later without changing `HogelDataset`, Fourier mapping, exposure, or replay.

## Consequences

Real view files and externally rendered/captured rasters can now enter the
hashed CHIMERA chain with explicit colour and sampling semantics. PPM is the
built-in interoperable file path, not a claim that it is the final product UI
format. Alpha, ICC profiles, HDR values above one, lens distortion, camera
calibration, and measured radiometry remain future calibrated adapters.
