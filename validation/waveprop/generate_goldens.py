#!/usr/bin/env python3
"""Generate deterministic HoloBench cross-validation fields with waveprop."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.metadata
import json
import pathlib
import tempfile

import numpy as np
from waveprop.fraunhofer import fraunhofer
from waveprop.fresnel import fresnel_conv
from waveprop.rs import angular_spectrum
from waveprop.devices import SLMParam, SensorParam
from waveprop.slm import get_slm_mask


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
DEFAULT_OUTPUT = SCRIPT_DIR / "goldens"
VERSION_PACKAGES = (
    "waveprop",
    "numpy",
    "scipy",
    "torch",
    "torchvision",
    "pyffs",
)


def package_versions() -> dict[str, str]:
    return {name: importlib.metadata.version(name) for name in VERSION_PACKAGES}


def centred_axis(count: int, pitch_metres: float) -> np.ndarray:
    return (np.arange(count, dtype=np.float64) - count / 2.0) * pitch_metres


def tilted_gaussian(
    nx: int,
    ny: int,
    dx: float,
    dy: float,
    waist_x: float,
    waist_y: float,
    bin_x: int,
    bin_y: int,
) -> np.ndarray:
    x = centred_axis(nx, dx)[np.newaxis, :]
    y = centred_axis(ny, dy)[:, np.newaxis]
    frequency_x = bin_x / (nx * dx)
    frequency_y = bin_y / (ny * dy)
    envelope = np.exp(-((x / waist_x) ** 2 + (y / waist_y) ** 2))
    carrier = np.exp(1j * 2.0 * np.pi * (frequency_x * x + frequency_y * y))
    return np.asarray(envelope * carrier, dtype=np.complex128)


def double_slit(
    nx: int,
    ny: int,
    dx: float,
    dy: float,
    slit_width: float,
    slit_height: float,
    centre_separation: float,
) -> np.ndarray:
    x = centred_axis(nx, dx)[np.newaxis, :]
    y = centred_axis(ny, dy)[:, np.newaxis]
    vertical = np.abs(y) <= slit_height / 2.0
    left = np.abs(x + centre_separation / 2.0) <= slit_width / 2.0
    right = np.abs(x - centre_separation / 2.0) <= slit_width / 2.0
    return np.asarray(vertical & (left | right), dtype=np.complex128)


def coordinates(nx: int, ny: int, dx: float, dy: float) -> tuple[np.ndarray, np.ndarray]:
    return centred_axis(nx, dx)[np.newaxis, :], centred_axis(ny, dy)[:, np.newaxis]


def build_cases() -> list[dict[str, object]]:
    cases: list[dict[str, object]] = []

    nx, ny = 64, 32
    dx, dy = 8.0e-6, 12.0e-6
    wavelength, distance = 532.0e-9, 12.5e-3
    input_field = tilted_gaussian(nx, ny, dx, dy, 120.0e-6, 80.0e-6, 3, -2)
    output, x_out, y_out = angular_spectrum(
        input_field,
        wavelength,
        [dy, dx],
        distance,
        bandlimit=False,
        pad=False,
    )
    x_in, y_in = coordinates(nx, ny, dx, dy)
    cases.append(
        make_case(
            name="asm_rectangular_tilted_gaussian",
            algorithm="waveprop.rs.angular_spectrum",
            input_field=input_field,
            output_field=output,
            x_in=x_in,
            y_in=y_in,
            x_out=x_out,
            y_out=y_out,
            wavelength=wavelength,
            distance=distance,
            dx=dx,
            dy=dy,
            options={"bandlimit": False, "pad": False},
        )
    )

    nx = ny = 64
    dx = dy = 10.0e-6
    wavelength, distance = 500.0e-9, 10.0e-3
    input_field = tilted_gaussian(nx, ny, dx, dy, 95.0e-6, 125.0e-6, -2, 1)
    output, x_out, y_out = fresnel_conv(
        input_field,
        wavelength,
        float(dx),
        distance,
        pad=False,
    )
    x_in, y_in = coordinates(nx, ny, dx, dy)
    cases.append(
        make_case(
            name="fresnel_tf_square_gaussian",
            algorithm="waveprop.fresnel.fresnel_conv",
            input_field=input_field,
            output_field=output,
            x_in=x_in,
            y_in=y_in,
            x_out=x_out,
            y_out=y_out,
            wavelength=wavelength,
            distance=distance,
            dx=dx,
            dy=dy,
            options={
                "pad": False,
                "carrier_note": "distance/wavelength = 20000, so exp(+i*k*z) = 1",
            },
        )
    )

    nx, ny = 64, 32
    dx, dy = 8.0e-6, 10.0e-6
    wavelength, distance = 633.0e-9, 1.0
    input_field = double_slit(nx, ny, dx, dy, 32.0e-6, 160.0e-6, 96.0e-6)
    output, x_out, y_out = fraunhofer(input_field, wavelength, [dy, dx], distance)
    x_in, y_in = coordinates(nx, ny, dx, dy)
    cases.append(
        make_case(
            name="fraunhofer_rectangular_double_slit",
            algorithm="waveprop.fraunhofer.fraunhofer",
            input_field=input_field,
            output_field=output,
            x_in=x_in,
            y_in=y_in,
            x_out=x_out,
            y_out=y_out,
            wavelength=wavelength,
            distance=distance,
            dx=dx,
            dy=dy,
            options={
                "slit_width_metres": 32.0e-6,
                "slit_height_metres": 160.0e-6,
                "centre_separation_metres": 96.0e-6,
            },
        )
    )

    # waveprop indexes device values from its positive-Y/positive-X corner and
    # rasterizes the X cell one field sample to the right. Flip the canonical
    # HoloBench command grid and record the explicit +dx device-centre offset;
    # no post-generation shift is applied to either reference field.
    nx, ny = 128, 64
    dx = dy = 1.0e-6
    wavelength, focal_length = 532.0e-9, 80.0e-3
    slm_rows, slm_columns = 8, 16
    pitch_y = pitch_x = 8.0e-6
    cell_y = cell_x = 6.0e-6
    commands = np.zeros((slm_rows, slm_columns), dtype=np.float64)
    selected_row, selected_column = 5, 12
    commands[selected_row, selected_column] = 1.0
    slm_config = {
        SLMParam.PITCH: np.array([pitch_y, pitch_x]),
        SLMParam.CELL_SIZE: np.array([cell_y, cell_x]),
        SLMParam.DEADSPACE: np.array([pitch_y - cell_y, pitch_x - cell_x]),
    }
    sensor_config = {
        SensorParam.SIZE: np.array([slm_rows, slm_columns])
        * slm_config[SLMParam.PITCH]
        - slm_config[SLMParam.DEADSPACE],
    }
    waveprop_commands = np.flip(commands, axis=(0, 1))
    mask = get_slm_mask(
        waveprop_commands,
        slm_config,
        sensor_config,
        crop_fact=1.0,
        target_dim=np.array([ny, nx]),
        deadspace=True,
        pytorch=False,
    )[0].astype(np.complex128)
    output, x_out, y_out = fraunhofer(
        mask, wavelength, [dy, dx], focal_length
    )
    x_in, y_in = coordinates(nx, ny, dx, dy)
    cases.append(
        make_case(
            name="slm_selected_pixel_fraunhofer",
            algorithm="waveprop.slm.get_slm_mask + waveprop.fraunhofer.fraunhofer",
            input_field=mask,
            output_field=output,
            x_in=x_in,
            y_in=y_in,
            x_out=x_out,
            y_out=y_out,
            wavelength=wavelength,
            distance=focal_length,
            dx=dx,
            dy=dy,
            options={
                "slm_rows": slm_rows,
                "slm_columns": slm_columns,
                "pixel_pitch_x_metres": pitch_x,
                "pixel_pitch_y_metres": pitch_y,
                "cell_width_metres": cell_x,
                "cell_height_metres": cell_y,
                "selected_row_negative_to_positive_y": selected_row,
                "selected_column_negative_to_positive_x": selected_column,
                "holobench_center_x_metres": dx,
                "holobench_center_y_metres": 0.0,
                "waveprop_command_conversion": "flip both command axes",
                "waveprop_x_raster_origin": "+1 field sample; represented by HoloBench center_x",
                "deadspace": True,
            },
        )
    )

    return cases


def make_case(
    *,
    name: str,
    algorithm: str,
    input_field: np.ndarray,
    output_field: np.ndarray,
    x_in: np.ndarray,
    y_in: np.ndarray,
    x_out: np.ndarray,
    y_out: np.ndarray,
    wavelength: float,
    distance: float,
    dx: float,
    dy: float,
    options: dict[str, object],
) -> dict[str, object]:
    ny, nx = input_field.shape
    if output_field.shape != input_field.shape:
        raise RuntimeError(f"{name}: waveprop changed the requested output shape")
    return {
        "name": name,
        "algorithm": algorithm,
        "input": np.asarray(input_field, dtype=np.complex128),
        "output": np.asarray(output_field, dtype=np.complex128),
        "x_in": np.asarray(x_in, dtype=np.float64),
        "y_in": np.asarray(y_in, dtype=np.float64),
        "x_out": np.asarray(x_out, dtype=np.float64),
        "y_out": np.asarray(y_out, dtype=np.float64),
        "metadata": {
            "format_version": 1,
            "name": name,
            "generator": algorithm,
            "width": nx,
            "height": ny,
            "input_pitch_x_metres": dx,
            "input_pitch_y_metres": dy,
            "output_pitch_x_metres": float(x_out[0, 1] - x_out[0, 0]),
            "output_pitch_y_metres": float(y_out[1, 0] - y_out[0, 0]),
            "vacuum_wavelength_metres": wavelength,
            "refractive_index": 1.0,
            "propagation_distance_metres": distance,
            "complex_phasor_convention": "exp(-i*omega*t), forward propagation exp(+i*k*z)",
            "row_major_order": "y then x",
            "options": options,
            "package_versions": package_versions(),
        },
    }


def write_case(case: dict[str, object], output_dir: pathlib.Path) -> list[pathlib.Path]:
    name = str(case["name"])
    metadata_path = output_dir / f"{name}.json"
    data_path = output_dir / f"{name}.csv"
    metadata_path.write_text(
        json.dumps(case["metadata"], indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    input_field = np.asarray(case["input"])
    output_field = np.asarray(case["output"])
    x_in = np.asarray(case["x_in"])
    y_in = np.asarray(case["y_in"])
    x_out = np.asarray(case["x_out"])
    y_out = np.asarray(case["y_out"])
    with data_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(
            (
                "x_in_metres",
                "y_in_metres",
                "input_real",
                "input_imag",
                "x_out_metres",
                "y_out_metres",
                "output_real",
                "output_imag",
            )
        )
        for y_index in range(input_field.shape[0]):
            for x_index in range(input_field.shape[1]):
                writer.writerow(
                    f"{value:.17e}"
                    for value in (
                        x_in[0, x_index],
                        y_in[y_index, 0],
                        input_field[y_index, x_index].real,
                        input_field[y_index, x_index].imag,
                        x_out[0, x_index],
                        y_out[y_index, 0],
                        output_field[y_index, x_index].real,
                        output_field[y_index, x_index].imag,
                    )
                )
    return [metadata_path, data_path]


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def manifest_name(path: pathlib.Path, output_dir: pathlib.Path) -> str:
    if path.is_relative_to(output_dir):
        relative = pathlib.Path("goldens") / path.relative_to(output_dir)
    else:
        relative = path.relative_to(SCRIPT_DIR)
    return str(relative).replace("\\", "/")


def generate(output_dir: pathlib.Path) -> dict[str, object]:
    output_dir.mkdir(parents=True, exist_ok=True)
    generated_files: list[pathlib.Path] = []
    for case in build_cases():
        generated_files.extend(write_case(case, output_dir))

    bound_files = [
        SCRIPT_DIR / "generate_goldens.py",
        SCRIPT_DIR / "requirements-validation.txt",
        *generated_files,
    ]
    manifest = {
        "format_version": 1,
        "generator": "waveprop external validation",
        "package_versions": package_versions(),
        "files": {
            manifest_name(path, output_dir): sha256(path)
            for path in sorted(bound_files)
        },
    }
    manifest_path = output_dir / "SHA256SUMS.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return manifest


def verify(committed_output: pathlib.Path) -> None:
    committed_manifest_path = committed_output / "SHA256SUMS.json"
    committed_manifest = json.loads(committed_manifest_path.read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="holobench-waveprop-") as directory:
        temporary_output = pathlib.Path(directory) / "goldens"
        regenerated = generate(temporary_output)
        if regenerated != committed_manifest:
            raise RuntimeError("regenerated manifest differs from committed SHA256SUMS.json")
        for relative_name in regenerated["files"]:
            if not relative_name.startswith("goldens/"):
                continue
            committed = SCRIPT_DIR / relative_name
            temporary = pathlib.Path(directory) / relative_name
            if committed.read_bytes() != temporary.read_bytes():
                raise RuntimeError(f"regenerated bytes differ: {relative_name}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--verify", action="store_true")
    arguments = parser.parse_args()
    if arguments.verify:
        verify(arguments.output.resolve())
        print("waveprop golden verification: byte-for-byte stable")
    else:
        manifest = generate(arguments.output.resolve())
        print(json.dumps(manifest, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
