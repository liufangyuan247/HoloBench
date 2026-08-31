#!/usr/bin/env python3
"""Generate deterministic real-lens cross-validation data with Optiland."""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import math
import pathlib
import tempfile
from typing import Any

import numpy as np
from optiland.materials import Material
from optiland.materials.material_spec import MatchPolicy
from optiland.optic import Optic
from optiland.rays import RealRays


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
DEFAULT_PRESCRIPTION_DIR = SCRIPT_DIR / "prescriptions"
DEFAULT_GOLDEN_DIR = SCRIPT_DIR / "goldens"
OPTILAND_SOURCE_COMMIT = "019413c2d8a2a367b7f6f7e8c422c8f76d6eb5ad"
WAVELENGTHS_METRES = (486.1327e-9, 587.5618e-9, 656.2725e-9)
PUPIL_POINTS_METRES = (
    (0.0, 0.0),
    (0.0030, 0.0),
    (-0.0030, 0.0),
    (0.0, 0.0025),
    (0.0, -0.0025),
    (0.0020, 0.0015),
    (-0.0020, 0.0015),
    (0.0020, -0.0015),
    (-0.0020, -0.0015),
)


def sellmeier_material(
    material_id: str,
    display_name: str,
    minimum_metres: float,
    maximum_metres: float,
    coefficients: tuple[tuple[float, float], ...],
) -> dict[str, Any]:
    return {
        "id": material_id,
        "display_name": display_name,
        "wavelength_domain_m": {
            "minimum": minimum_metres,
            "maximum": maximum_metres,
        },
        "dispersion": {
            "type": "sellmeier_si",
            "terms": [
                {"b": b_value, "c_m2": c_micrometres_squared * 1e-12}
                for b_value, c_micrometres_squared in coefficients
            ],
        },
    }


VACUUM = {
    "id": "vacuum",
    "display_name": "Vacuum",
    "wavelength_domain_m": {"minimum": 1e-12, "maximum": 1.0},
    "dispersion": {"type": "constant", "refractive_index": 1.0},
}
N_BK7 = sellmeier_material(
    "schott_n_bk7",
    "SCHOTT N-BK7",
    300e-9,
    2.5e-6,
    (
        (1.03961212, 0.00600069867),
        (0.231792344, 0.0200179144),
        (1.01046945, 103.560653),
    ),
)
F2 = sellmeier_material(
    "schott_f2",
    "SCHOTT F2",
    334e-9,
    2.5e-6,
    (
        (1.34533359, 0.00997743871),
        (0.209073176, 0.0470450767),
        (0.937357162, 111.886764),
    ),
)
MATERIALS = {value["id"]: value for value in (VACUUM, N_BK7, F2)}


def surface(
    surface_id: str,
    z_metres: float,
    curvature_per_metre: float,
    before: str,
    after: str,
    *,
    conic: float = 0.0,
    asphere_terms: tuple[tuple[int, float], ...] = (),
    semi_diameter_metres: float = 0.008,
) -> dict[str, Any]:
    return {
        "id": surface_id,
        "z_metres": z_metres,
        "curvature_per_metre": curvature_per_metre,
        "conic": conic,
        "asphere_terms": asphere_terms,
        "semi_diameter_metres": semi_diameter_metres,
        "before": before,
        "after": after,
    }


def benchmark_cases() -> tuple[dict[str, Any], ...]:
    return (
        {
            "name": "plano_convex_singlet",
            "description": "N-BK7 plano-convex singlet, curved face first",
            "surfaces": (
                surface("front_sphere", 0.0, 20.0, "vacuum", "schott_n_bk7"),
                surface("rear_plane", 0.005, 0.0, "schott_n_bk7", "vacuum"),
            ),
        },
        {
            "name": "positive_meniscus",
            "description": "N-BK7 positive meniscus with two same-sign radii",
            "surfaces": (
                surface("front_sphere", 0.0, 25.0, "vacuum", "schott_n_bk7"),
                surface("rear_sphere", 0.004, 12.5, "schott_n_bk7", "vacuum"),
            ),
        },
        {
            "name": "cemented_achromatic_doublet",
            "description": "Cemented SCHOTT N-BK7/F2 achromatic doublet",
            "surfaces": (
                surface("crown_front", 0.0, 20.0, "vacuum", "schott_n_bk7"),
                surface("cemented_interface", 0.004, -28.0, "schott_n_bk7", "schott_f2"),
                surface("flint_rear", 0.006, -10.0, "schott_f2", "vacuum"),
            ),
        },
        {
            "name": "conic_singlet",
            "description": "N-BK7 singlet with a prolate-ellipsoidal front face",
            "surfaces": (
                surface(
                    "front_conic",
                    0.0,
                    22.0,
                    "vacuum",
                    "schott_n_bk7",
                    conic=-0.65,
                ),
                surface("rear_sphere", 0.005, -16.0, "schott_n_bk7", "vacuum"),
            ),
        },
        {
            "name": "even_asphere_singlet",
            "description": "N-BK7 singlet with SI r^4 and r^6 front-face terms",
            "surfaces": (
                surface(
                    "front_even_asphere",
                    0.0,
                    20.0,
                    "vacuum",
                    "schott_n_bk7",
                    conic=-0.5,
                    asphere_terms=((4, 1.0e4), (6, -5.0e7)),
                ),
                surface("rear_sphere", 0.005, -14.0, "schott_n_bk7", "vacuum"),
            ),
        },
    )


def asphere_si_to_optiland_mm(radial_order: int, coefficient_si: float) -> float:
    """Convert A_n [m^(1-n)] to the coefficient for r_mm^n and sag_mm."""
    if radial_order < 2 or radial_order % 2 != 0:
        raise ValueError("asphere radial orders must be positive and even")
    return coefficient_si * 10.0 ** (3 - 3 * radial_order)


def optiland_coefficients(surface_value: dict[str, Any]) -> list[float]:
    terms = surface_value["asphere_terms"]
    if not terms:
        return []
    maximum_order = max(order for order, _ in terms)
    result = [0.0] * (maximum_order // 2)
    for radial_order, coefficient_si in terms:
        result[radial_order // 2 - 1] = asphere_si_to_optiland_mm(
            radial_order, coefficient_si
        )
    return result


def make_prescription(case: dict[str, Any]) -> dict[str, Any]:
    material_ids: list[str] = []
    for surface_value in case["surfaces"]:
        for key in ("before", "after"):
            material_id = surface_value[key]
            if material_id not in material_ids:
                material_ids.append(material_id)
    return {
        "format": "holobench-lens-prescription",
        "format_version": 1,
        "id": case["name"],
        "materials": [MATERIALS[material_id] for material_id in material_ids],
        "surfaces": [
            {
                "id": value["id"],
                "geometry": {
                    "curvature_per_m": value["curvature_per_metre"],
                    "conic_constant": value["conic"],
                    "clear_semi_diameter_m": value["semi_diameter_metres"],
                    "even_asphere_terms": [
                        {"radial_order": order, "coefficient_si": coefficient}
                        for order, coefficient in value["asphere_terms"]
                    ],
                },
                "local_to_world": {
                    "translation_m": [0.0, 0.0, value["z_metres"]],
                    "local_x_axis_in_world": [1.0, 0.0, 0.0],
                    "local_y_axis_in_world": [0.0, 1.0, 0.0],
                    "local_z_axis_in_world": [0.0, 0.0, 1.0],
                },
                "material_before_id": value["before"],
                "material_after_id": value["after"],
            }
            for value in case["surfaces"]
        ],
    }


def material_for_optiland(material_id: str):
    if material_id == "vacuum":
        return "air"
    if material_id == "schott_n_bk7":
        return Material("N-BK7", catalog="schott", match_policy=MatchPolicy.STRICT)
    if material_id == "schott_f2":
        return Material("F2", catalog="schott", match_policy=MatchPolicy.STRICT)
    raise ValueError(f"unknown Optiland material mapping: {material_id}")


def make_optiland_optic(case: dict[str, Any]) -> Optic:
    optic = Optic(name=case["name"])
    optic.surfaces.add(index=0, surface_type="standard", material="air")
    surfaces = case["surfaces"]
    for index, value in enumerate(surfaces, start=1):
        next_z = (
            surfaces[index]["z_metres"]
            if index < len(surfaces)
            else value["z_metres"]
        )
        thickness_mm = (next_z - value["z_metres"]) * 1000.0
        kwargs: dict[str, Any] = {
            "index": index,
            "surface_type": "even_asphere" if value["asphere_terms"] else "standard",
            "radius": (
                math.inf
                if value["curvature_per_metre"] == 0.0
                else 1000.0 / value["curvature_per_metre"]
            ),
            "conic": value["conic"],
            "thickness": thickness_mm,
            "aperture": 2.0 * value["semi_diameter_metres"] * 1000.0,
            "material": material_for_optiland(value["after"]),
            "comment": value["id"],
        }
        if value["asphere_terms"]:
            kwargs["coefficients"] = optiland_coefficients(value)
            kwargs["tol"] = 1e-12
            kwargs["max_iter"] = 200
        optic.surfaces.add(**kwargs)
    return optic


def validate_optiland_conversions(case: dict[str, Any], optic: Optic) -> None:
    """Prove the committed SI/mm mapping against Optiland's configured objects."""
    for index, value in enumerate(case["surfaces"], start=1):
        configured = optic.surfaces.surfaces[index]
        expected_aperture_mm = value["semi_diameter_metres"] * 1000.0
        if not math.isclose(
            float(configured.aperture.r_max),
            expected_aperture_mm,
            rel_tol=0.0,
            abs_tol=1e-14,
        ):
            raise RuntimeError("Optiland scalar-aperture diameter mapping drifted")

        radial_metres = min(0.0037, value["semi_diameter_metres"] * 0.6)
        curvature = value["curvature_per_metre"]
        radial_squared = radial_metres * radial_metres
        if curvature == 0.0:
            expected_sag_metres = 0.0
        else:
            radicand = 1.0 - (1.0 + value["conic"]) * curvature * curvature * radial_squared
            expected_sag_metres = (
                curvature
                * radial_squared
                / (1.0 + math.sqrt(radicand))
            )
        for radial_order, coefficient_si in value["asphere_terms"]:
            expected_sag_metres += coefficient_si * radial_metres**radial_order

        actual_sag_metres = (
            float(configured.geometry.sag(radial_metres * 1000.0, 0.0)) * 1e-3
        )
        if not math.isclose(
            actual_sag_metres,
            expected_sag_metres,
            rel_tol=2e-13,
            abs_tol=2e-16,
        ):
            raise RuntimeError(
                f"{case['name']} surface {value['id']} SI/mm sag mapping drifted"
            )


def input_rays() -> list[dict[str, Any]]:
    rays: list[dict[str, Any]] = []
    for wavelength in WAVELENGTHS_METRES:
        for pupil_index, (x_metres, y_metres) in enumerate(PUPIL_POINTS_METRES):
            rays.append(
                {
                    "id": f"lambda_{wavelength:.10e}_pupil_{pupil_index}",
                    "origin_m": [x_metres, y_metres, -0.020],
                    "direction": [0.0, 0.0, 1.0],
                    "vacuum_wavelength_m": wavelength,
                    "power": 1.0,
                }
            )
    return rays


def to_float_list(values: Any) -> list[float]:
    return [float(value) for value in np.asarray(values, dtype=np.float64)]


def fit_best_focus(final_rays: list[dict[str, Any]], wavelength: float) -> dict[str, Any]:
    selected = [
        ray for ray in final_rays if ray["vacuum_wavelength_m"] == wavelength
    ]
    ax_values: list[float] = []
    ay_values: list[float] = []
    bx_values: list[float] = []
    by_values: list[float] = []
    for ray in selected:
        x_value, y_value, z_value = ray["origin_m"]
        l_value, m_value, n_value = ray["direction"]
        bx_value = l_value / n_value
        by_value = m_value / n_value
        ax_values.append(x_value - bx_value * z_value)
        ay_values.append(y_value - by_value * z_value)
        bx_values.append(bx_value)
        by_values.append(by_value)
    ax = np.asarray(ax_values, dtype=np.float64)
    ay = np.asarray(ay_values, dtype=np.float64)
    bx = np.asarray(bx_values, dtype=np.float64)
    by = np.asarray(by_values, dtype=np.float64)
    dax = ax - np.mean(ax)
    day = ay - np.mean(ay)
    dbx = bx - np.mean(bx)
    dby = by - np.mean(by)
    quadratic = float(np.mean(dbx * dbx + dby * dby))
    linear_half = float(np.mean(dax * dbx + day * dby))
    plane_z = -linear_half / quadratic
    x_at_focus = ax + bx * plane_z
    y_at_focus = ay + by * plane_z
    centroid_x = float(np.mean(x_at_focus))
    centroid_y = float(np.mean(y_at_focus))
    rms_radius = float(
        np.sqrt(
            np.mean(
                (x_at_focus - centroid_x) ** 2
                + (y_at_focus - centroid_y) ** 2
            )
        )
    )
    return {
        "vacuum_wavelength_m": wavelength,
        "plane_z_m": plane_z,
        "rms_radius_m": rms_radius,
        "ray_count": len(selected),
    }


def trace_case(case: dict[str, Any]) -> dict[str, Any]:
    optic = make_optiland_optic(case)
    validate_optiland_conversions(case, optic)
    inputs = input_rays()
    origins_mm = np.asarray([value["origin_m"] for value in inputs]) * 1000.0
    directions = np.asarray([value["direction"] for value in inputs])
    wavelengths_um = np.asarray(
        [value["vacuum_wavelength_m"] for value in inputs]
    ) * 1e6
    rays = RealRays(
        x=origins_mm[:, 0],
        y=origins_mm[:, 1],
        z=origins_mm[:, 2],
        L=directions[:, 0],
        M=directions[:, 1],
        N=directions[:, 2],
        intensity=np.ones(len(inputs), dtype=np.float64),
        wavelength=wavelengths_um,
    )
    optic.surfaces.trace(rays, skip=1)

    physical_surfaces = tuple(optic.surfaces.surfaces[1:])
    surface_arrays = []
    for surface_value in physical_surfaces:
        surface_arrays.append(
            {
                "x": to_float_list(surface_value.x),
                "y": to_float_list(surface_value.y),
                "z": to_float_list(surface_value.z),
                "l": to_float_list(surface_value.L),
                "m": to_float_list(surface_value.M),
                "n": to_float_list(surface_value.N),
                "intensity": to_float_list(surface_value.intensity),
                "optical_path": to_float_list(surface_value.opd),
            }
        )

    traced: list[dict[str, Any]] = []
    for ray_index, input_value in enumerate(inputs):
        records = []
        for surface_index, case_surface in enumerate(case["surfaces"]):
            values = surface_arrays[surface_index]
            if values["intensity"][ray_index] <= 0.0:
                raise RuntimeError(
                    f"{case['name']} ray {ray_index} was clipped in Optiland"
                )
            records.append(
                {
                    "surface_id": case_surface["id"],
                    "point_m": [
                        values["x"][ray_index] * 1e-3,
                        values["y"][ray_index] * 1e-3,
                        values["z"][ray_index] * 1e-3,
                    ],
                    "outgoing_direction": [
                        values["l"][ray_index],
                        values["m"][ray_index],
                        values["n"][ray_index],
                    ],
                    "cumulative_optical_path_m": values["optical_path"][ray_index]
                    * 1e-3,
                }
            )
        final_record = records[-1]
        traced.append(
            {
                "id": input_value["id"],
                "vacuum_wavelength_m": input_value["vacuum_wavelength_m"],
                "origin_m": final_record["point_m"],
                "direction": final_record["outgoing_direction"],
                "cumulative_optical_path_m": final_record[
                    "cumulative_optical_path_m"
                ],
                "surface_records": records,
            }
        )

    focus = [fit_best_focus(traced, wavelength) for wavelength in WAVELENGTHS_METRES]
    reference_focus = next(
        value
        for value in focus
        if value["vacuum_wavelength_m"] == WAVELENGTHS_METRES[1]
    )
    image_plane_z = reference_focus["plane_z_m"]
    spot_samples = []
    for ray_index, final_ray in enumerate(traced):
        x_value, y_value, z_value = final_ray["origin_m"]
        l_value, m_value, n_value = final_ray["direction"]
        distance = (image_plane_z - z_value) / n_value
        spot_samples.append(
            {
                "source_ray_index": ray_index,
                "vacuum_wavelength_m": final_ray["vacuum_wavelength_m"],
                "x_m": x_value + l_value * distance,
                "y_m": y_value + m_value * distance,
            }
        )
    return {
        "format": "holobench-optiland-real-lens-golden",
        "format_version": 1,
        "name": case["name"],
        "description": case["description"],
        "generator": {
            "package": "optiland",
            "package_version": importlib.metadata.version("optiland"),
            "reported_module_version": getattr(
                __import__("optiland"), "__version__", "unknown"
            ),
            "source_tag": "v0.6.2",
            "source_commit": OPTILAND_SOURCE_COMMIT,
            "numpy_version": np.__version__,
            "length_unit": "mm",
            "wavelength_unit": "um",
        },
        "coordinate_convention": {
            "world": "right-handed; nominal propagation +Z",
            "surface_vertices": "first physical surface z=0; later z from prior thickness",
            "radius_mm": "1000 / curvature_per_m",
            "aperture_scalar": "full diameter in mm",
            "asphere_conversion": "A_n_optiland_mm = A_n_SI * 10^(3-3n)",
        },
        "prescription_file": f"prescriptions/{case['name']}.json",
        "input_rays": inputs,
        "traced_rays": traced,
        "analysis": {
            "focus_bounds_m": [case["surfaces"][-1]["z_metres"] + 0.001, 0.5],
            "best_focus_by_wavelength": focus,
            "longitudinal_focal_shift_m": max(
                value["plane_z_m"] for value in focus
            )
            - min(value["plane_z_m"] for value in focus),
            "image_plane_z_m": image_plane_z,
            "spot_samples": spot_samples,
        },
    }


def write_json(path: pathlib.Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def manifest_name(
    path: pathlib.Path,
    prescription_dir: pathlib.Path,
    golden_dir: pathlib.Path,
) -> str:
    if path.is_relative_to(prescription_dir):
        return str(pathlib.Path("prescriptions") / path.relative_to(prescription_dir)).replace(
            "\\", "/"
        )
    if path.is_relative_to(golden_dir):
        return str(pathlib.Path("goldens") / path.relative_to(golden_dir)).replace(
            "\\", "/"
        )
    return str(path.relative_to(SCRIPT_DIR)).replace("\\", "/")


def generate(
    prescription_dir: pathlib.Path, golden_dir: pathlib.Path
) -> dict[str, Any]:
    generated_files: list[pathlib.Path] = []
    for case in benchmark_cases():
        prescription_path = prescription_dir / f"{case['name']}.json"
        golden_path = golden_dir / f"{case['name']}.json"
        write_json(prescription_path, make_prescription(case))
        write_json(golden_path, trace_case(case))
        generated_files.extend((prescription_path, golden_path))
    bound_files = [
        SCRIPT_DIR / "generate_goldens.py",
        SCRIPT_DIR / "requirements-validation.txt",
        *generated_files,
    ]
    manifest = {
        "format_version": 1,
        "generator": "Optiland real-lens external validation",
        "optiland_version": importlib.metadata.version("optiland"),
        "optiland_source_commit": OPTILAND_SOURCE_COMMIT,
        "files": {
            manifest_name(path, prescription_dir, golden_dir): sha256(path)
            for path in sorted(bound_files)
        },
    }
    write_json(golden_dir / "SHA256SUMS.json", manifest)
    return manifest


def verify() -> None:
    committed_manifest_path = DEFAULT_GOLDEN_DIR / "SHA256SUMS.json"
    committed_manifest = json.loads(committed_manifest_path.read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="holobench-optiland-") as directory:
        temporary_root = pathlib.Path(directory)
        prescriptions = temporary_root / "prescriptions"
        goldens = temporary_root / "goldens"
        regenerated = generate(prescriptions, goldens)
        if regenerated != committed_manifest:
            raise RuntimeError("regenerated manifest differs from committed SHA256SUMS.json")
        for relative_name in regenerated["files"]:
            if relative_name.startswith("prescriptions/"):
                committed = SCRIPT_DIR / relative_name
                temporary = temporary_root / relative_name
            elif relative_name.startswith("goldens/"):
                committed = SCRIPT_DIR / relative_name
                temporary = temporary_root / relative_name
            else:
                continue
            if committed.read_bytes() != temporary.read_bytes():
                raise RuntimeError(f"regenerated bytes differ: {relative_name}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify", action="store_true")
    arguments = parser.parse_args()
    if arguments.verify:
        verify()
        print("Optiland real-lens golden verification: byte-for-byte stable")
    else:
        manifest = generate(DEFAULT_PRESCRIPTION_DIR, DEFAULT_GOLDEN_DIR)
        print(json.dumps(manifest, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
