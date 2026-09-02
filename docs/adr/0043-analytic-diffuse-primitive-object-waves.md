# ADR 0043: Analytic diffuse primitive object waves

## Status

Accepted, 2026-09-03.

## Context

The Bench could previously record an `ObjectWavefrontSource` only as a uniform
rectangular complex envelope. A rendered object mesh would therefore have been
decorative and could not test whether a placed three-dimensional sample was
actually recorded and reconstructed. Using PCG render triangles as scattering
truth would also reverse the required dependency between optics and rendering.

## Decision

The first solid sample family is deliberately narrow: cube/cuboid,
sphere/ellipsoid, and triangular pyramid (tetrahedron), all opaque scalar
diffuse surfaces. `optics/` intersects these shapes analytically in the source
frame and returns the nearest visible surface depth and normal. The emitted
amplitude uses a Lambertian illumination weight. A fixed 25 micrometre
physical-space correlation cell and persisted 64-bit seed generate a stable
coherent rough phase.

Visible samples are quantized into at most six non-empty axial layers. Each
layer is propagated to the placed source reference plane by the existing
angular-spectrum service, coherently summed, and normalized to the declared
total scattered object-wave power. The resulting field then follows the same
ray-derived, placed-element, plate-recording, and reconstruction path as other
Bench fields. A primitive source always requests FFT-backed local-wave
refinement; it cannot silently fall back to the legacy rectangle during product
recording.

The component transform continues to aim the emitted centre branch. Separate
primitive yaw and pitch rotate the sample relative to that branch. PCG consumes
the same dimensions and derived primitive pose, but its triangles remain a
disposable visual cache and are never queried by the optical solver.

Unified Bench format v6 persists geometry, width, height, depth, primitive yaw
and pitch, and roughness seed. Formats v1-v5 migrate their old rectangular
object source to an internal `UniformPlane` compatibility mode. New placement
and the Inspector expose only the three solid shapes.

## Declared boundary

`channel.power_w` is the total already-scattered object-wave power. This slice
does not yet trace an independent `Laser -> Object` interaction, illumination
occlusion, cast shadows, interreflection, specular response, transparency,
refraction, texture, polarization, or multiple scattering. Those require a
separate reusable scattering interaction rather than hidden experiment logic.

## Consequences

- Moving a Screen/Probe or replay plane observes propagation and defocus from
  bounded axial structure rather than a view-dependent render texture.
- Coherent diffuse speckle has higher local peaks than a uniform plane, so the
  default thin-plate relative exposure reference is 250 kW/m2 to keep the
  nominal linear response unclipped.
- RGB channels remain independent and never cross-interfere. A current
  full-colour object is represented by three ordinary matched single-channel
  object sources; a native multi-channel sample is future work.
