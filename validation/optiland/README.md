# Optiland real-lens external validation

This validation-only environment independently traces five committed M4
prescriptions with Optiland 0.6.2: a plano-convex singlet, positive meniscus,
cemented achromatic doublet, conic singlet, and even-asphere singlet. HoloBench
and Optiland receive identical explicit axial rays at the Fraunhofer F, d, and C
vacuum wavelengths. No pupil aimer or production root solver is shared.

Optiland uses millimetres and micrometres while HoloBench uses SI. The generator
converts radius with `R_mm = 1000 / c_per_m`. For a HoloBench even-asphere term
`A_n r_m^n` with `A_n` in `m^(1-n)`, the Optiland coefficient is
`A_n_mm = A_n_SI * 10^(3-3n)` because its sag and radius are both measured in
millimetres. Optiland coefficient index zero is the `r^2` term, so HoloBench's
first supported `r^4` term is stored at index one. Scalar apertures passed to
Optiland are full diameters.

The committed metadata records the PyPI version, the `v0.6.2` source commit,
the package's historical module-version string, units, conversions, explicit
input rays, per-surface intersections and outgoing directions, cumulative
optical path, fixed-plane spot coordinates, wavelength best focus, and
longitudinal focal shift. Python and Optiland remain outside shipped binaries.

Generate or verify from the repository root:

```powershell
py -m venv out/validation/optiland-0.6.2
out/validation/optiland-0.6.2/Scripts/python -m pip install -r validation/optiland/requirements-validation.txt
out/validation/optiland-0.6.2/Scripts/python validation/optiland/generate_goldens.py
out/validation/optiland-0.6.2/Scripts/python validation/optiland/generate_goldens.py --verify
```

`--verify` regenerates all prescription and golden files in a temporary
directory, checks the manifest, then requires byte-for-byte equality.
