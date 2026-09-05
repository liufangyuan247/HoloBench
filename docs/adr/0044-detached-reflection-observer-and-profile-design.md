# ADR 0044: Detached reflection observation and meridional instrument editing

## Status

Implemented increment, 2026-09-05. Numerical and UI evidence is recorded in
PROJECT_STATE.md; this does not close the full spectral CHIMERA showroom goal.

## Decision and conventions

`RecordedHologram` is a detached plate-local scalar response. Freezing requires
current opposite-side recording evidence, retained co-sampled complex fields,
resolved carriers and nonzero overlap. It retains the normalized complex product
`U_object * conj(U_reference)`, material and full centre grating vector, rather
than input perspective colours or a rendered image. The centre carrier of the
product must also fit Nyquist. Scene revisions are checked at freeze time, never
rewritten to make old results pass. Observation poses do not mutate the asset.

The format-v1 `.holo.json` envelope has a canonical JSON payload SHA-256, explicit
metres, a named approximation and at most 1,048,576 complex samples / 64 MiB on
disk. It is independent of Bench JSON, so existing Bench files retain their
semantics. The currently sampled analysis window is the displayed aperture;
unsampled plate regions are not invented. Window centre metadata is retained,
but the displayed instance is centred on that window.

The existing `exp(-i omega t)` convention, unnormalised forward FFT, inverse
`1/(Nx Ny)` and metre coordinates apply. The grating sign is
`K = k_object - k_reference`. Uniform replay illumination multiplies the frozen
complex response by its current local plane-wave carrier and the existing first
isotropic shrinkage carrier correction. Outgoing tangential wavevector is
`k_incident,t + K_t/(1-shrinkage)`, with the recorded reflected hemisphere.
The outgoing power is the projected incident power on the sampled window times
the equivalent-symmetric scalar-TE Kogelnik efficiency. This is the same bounded
normalised local-response approximation as the existing reflection replay, not
a rigorous multiplexed/slanted/vector grating or measured material model.

Rigid plate pose transforms both illumination and outgoing direction. A rotated
angular spectrum propagates to the fixed world-XY entrance pupil, using the
existing doubled zero-padding/interpolation and positive-Z outgoing hemisphere.
Unrepresentable preferred carriers fail explicitly. The pupil has a finite
circular aperture. Boundary energy is reported as a diagnostic; finite-window
padding is not claimed to prove convergence.

For positive sensor distance b and positive focus distance s, a paraxial thin
camera has `1/f = 1/s + 1/b`. Its pupil-to-sensor intensity is the physically
scaled Fourier integral of `U_pupil exp(-ik r^2/(2s))`, with sensor pitch
`lambda*b/(N*dx)`. The Fourier utility's global phase and the omitted sensor
quadratic phase have no effect on this intensity-only observation. Sensor
inversion is corrected only in display. Pupil NA and the focus phase sampling
are checked. The camera thus observes the field through a finite pupil rather
than colouring a plate texture or restoring the input object mesh.

Display is explicitly grayscale with fixed reference intensity after the first
nonzero image; it is not spectral colour prediction. The initial product exposes
one recorded channel (green for an RGB Bench recording). Continuous white light,
all-channel colour, coherent multi-hogel replay, full-volume material response,
and arbitrary-angle observation remain requested work.

The background worker owns immutable data. Every new request cancels prior work
and clears pending display results. Stages preserve recording pitch and all
complex samples; the second stage doubles the pupil window and sensor sampling
count when it fits the bounded 1024x1024 observer window; larger source grids
use one final stage. Eye translations outside the represented source window
reject instead of wrapping a periodic image back into view. Cancellation is
checked between propagation/FFT operations and in field
loops, not inside the existing FFT implementation. No sub-millisecond
cancellation latency or rendering performance is claimed.

## Bench mechanics and lens editing

CHIMERA compiler v2 translates the complete generated optical scene by +100 mm Y
and attaches explicit nominal assemblies (95 mm post, base top +5 mm). This
preserves optical separations. Plate exposure uses the stage DOFs with fixed
bases and rejects travel outside their limits. Existing saved Bench scenes are
not translated or recompiled on load. Existing compiler provenance remains
readable; newly generated scenes name compiler v2.

Analytic oriented base boxes share parameter-derived sizes with PCG geometry.
Fifteen separating axes detect penetrations; contact within 1 nm is permitted.
The UI reports these as base checks, not complete casing, optical-clearance or
motion-envelope certification. The current transmissive thin SLM now has an
open frame instead of an opaque LCOS-like back housing. No reflective LCOS or
polarisation routing is implied.

The 2D lens editor samples `evaluateSurfaceSag` on the real sequential
prescription. Curves are disposable meridional views. Dragging edge sag solves
`c = 2*s/(r^2 + (1+k)*s^2)` after subtracting the known even-asphere contribution,
then validates the single-valued conic branch. Spacing edits translate the
selected and downstream surfaces together. Appended N-BK7 elements reuse the
existing material model and default lens prescription. Mutations are
transactional and clear imported-calibration eligibility.

Direct section editing is coaxial and rotationally symmetric. A 257-radius
sampled check rejects surface intersections and gaps below 1 um across the
common aperture. It is not a continuous proof against arbitrarily oscillatory
high-order aspheres or a manufacturing tolerance analysis. Decentre, tilt,
materials and asphere terms remain available in the existing prescription
editor; free-form splines are not silently fitted to unsupported physics.

## Validation

Independent spherical-wave camera focus, lens-conjugate defocus, pupil/sensor
power conservation, light-power scaling, Bragg wavelength detuning, real placed
recording freeze, stale/undersampled rejection, file corruption rejection and
latest-request cancellation have deterministic tests. The circular sag oracle,
group spacing, rejected intersecting edits, mounted table height and oriented
base contact/penetration have deterministic tests. A finite-depth off-axis point
also verifies eye-translation parallax against the independent lens conjugate;
the full development suite passes 649/649. Further angular, spectral,
multi-depth and complete product acceptance remains separate evidence.
