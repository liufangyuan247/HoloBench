# ADR 0039 - Hashed optical-pose calibration and split mechanical/solver frames

Status: Accepted for the M10 placed-instrument optical paths.

## Context

A measured instrument can have a small rigid displacement between the nominal
mount frame and its actual optical axis or surface. Moving the persisted
component transform to represent this correction would also move its base,
post, controls, and PCG housing. Keeping only an `optical_pose` reference would
do the opposite: it would display calibration metadata while every ray and
field still used nominal geometry.

## Decision

- Optical-pose format v1 is a strict JSON `rigid_optical_frame_offset`. Its
  `nominal_to_measured_optical` transform is expressed in the nominal
  instrument frame, uses metres and a right-handed orthonormal basis, and is
  bounded to a 100 mm translation and 30 degree rotation. These bounds describe
  a calibration correction, not a second arbitrary placement mechanism.
- The loader accepts at most 64 KiB, reads the file once, and computes SHA-256
  and parses from those same bytes. The immutable ID is
  `optical-pose-sha256-<digest>`. Project restoration resolves relative paths,
  verifies the hash before parsing, checks format/content/specification
  identity, and swaps the runtime catalog only after complete validation.
- One verified pose may bind any current physical Bench component. A virtual
  Field Probe cannot claim a measured hardware pose. Wavelength and environment
  temperature validity are checked against the active scene before any
  calibrated scene is produced.
- The persisted Bench remains the nominal mechanical and appearance scene. A
  transactionally derived scene composes each verified local correction into
  the component optical transform and removes only the copied mechanical
  assembly constraint. Dynamic rays, Screen/Probe fields, thin and volume
  holography, RGB/Denisyuk routing, and CHIMERA exposure/reconstruction/camera
  paths consume this calibrated scene.
- Rendering deliberately consumes both scenes: PCG solids come from the
  nominal mechanical scene, while optical proxy outlines and rays use the
  calibrated scene. A short amber connector makes a nonzero origin correction
  inspectable. The Inspector retains calibration ID, content hash, and nominal
  versus applied frame evidence.
- Project edits, undo, autosave, and serialization retain the reference rather
  than a derived transform. A missing, changed, stale, duplicated,
  out-of-domain, or semantically invalid asset rejects without nominal
  fallback.

## Consequences and limits

The optical axis can now be calibrated without corrupting mechanical readings
or treating render triangles as solver truth. Rebuilding the derived scene is
bounded by component count and happens only on accepted scene changes or an
explicit viewport refresh.

Format v1 is one static rigid correction. It does not model stage hysteresis,
temperature-dependent interpolation, elastic deformation, surface figure,
time drift, or uncertainty distributions. Those require separate reusable
calibration models rather than widening this transform into a generic hidden
animation system.
