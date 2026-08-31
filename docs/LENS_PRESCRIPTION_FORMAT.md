# Lens prescription exchange formats

HoloBench M4 supports two versioned, lossless prescription formats. JSON is the
canonical structured exchange format. CSV is a normalized, record-oriented
format intended for spreadsheet inspection and deterministic import/export.
Both use metres and SI-derived coefficients internally and both are validated
as a complete sequential prescription after parsing.

## JSON version 1

The root object contains exactly these fields:

- `format`: `holobench-lens-prescription`
- `format_version`: `1`
- `id`: non-empty prescription identifier
- `materials`: ordered material array
- `surfaces`: ordered sequential surface array

A material records `id`, `display_name`, an inclusive
`wavelength_domain_m`, and one dispersion object:

- `constant`: `refractive_index`
- `cauchy_si`: `a`, `b_m2`, and `c_m4`
- `sellmeier_si`: ordered `terms`, each containing dimensionless `b` and
  `c_m2`

A surface records its identifier, before/after material identifiers, geometry,
and a rigid local-to-world pose. Geometry fields are `curvature_per_m`,
`conic_constant`, `clear_semi_diameter_m`, and ordered
`even_asphere_terms`. Each asphere term stores `radial_order` and
`coefficient_si`. The pose stores `translation_m` and the three right-handed,
orthonormal local basis axes expressed in world coordinates.

Unknown or missing fields fail explicitly. Version 1 readers do not silently
accept future versions.

## CSV version 1

The first two records are:

```csv
holobench_lens_prescription_csv,1
prescription,prescription_id
```

The remaining record types are:

```text
material,id,display_name,lambda_min_m,lambda_max_m,constant,n
material,id,display_name,lambda_min_m,lambda_max_m,cauchy_si,A,B_m2,C_m4
material,id,display_name,lambda_min_m,lambda_max_m,sellmeier_si,term_count
sellmeier_term,material_id,zero_based_index,B,C_m2

surface,id,curvature_per_m,conic_constant,clear_semi_diameter_m,
        tx_m,ty_m,tz_m,Xx,Xy,Xz,Yx,Yy,Yz,Zx,Zy,Zz,
        material_before_id,material_after_id,asphere_term_count
asphere_term,surface_id,zero_based_index,radial_order,coefficient_si
```

Physical newlines above only improve readability; every `surface` record is one
CSV row with exactly 20 fields. Child term indices must be contiguous and start
at zero. Declared child counts must match the rows present. Text follows CSV
double-quote escaping, so commas, quotes, carriage returns, and newlines are
lossless. Floating-point output uses enough decimal digits for exact binary64
round-trip and is locale-independent.

## Validation and compatibility

Import rejects non-finite numbers, malformed quoting, unknown record/model
types, duplicate identifiers, invalid child counts or indices, non-rigid poses,
invalid surface domains, invalid dispersion models, unknown material
references, and discontinuous media between adjacent surfaces.

Schema changes require a new format version and migration tests. Runtime code
must not infer units, glass catalogs, Euler angle order, or omitted material
transitions from filenames or vendor conventions.
