# HoloBench

HoloBench is an interactive 3D optical bench, a multi-fidelity physics simulator, and a long-term R&D tool for CHIMERA-like holographic printing.

**M0 — Engineering Foundation** is complete; M1 geometric-optics work is next. The product vision and long-term physics roadmap live in [HoloBench_CHIMERA_Project_Plan.md](HoloBench_CHIMERA_Project_Plan.md); the repository's actual current state is tracked in [docs/PROJECT_STATE.md](docs/PROJECT_STATE.md).

## Supported development environment

- Windows x64 or Linux x64
- CMake 3.28+
- Ninja or another CMake-supported generator
- A C++20 compiler
- OpenGL 4.6-capable GPU for the interactive application

Dependencies are fetched by CMake at pinned versions. Python libraries used for numerical validation will remain development-only dependencies.

## Build and test

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Run on Windows:

```powershell
./out/build/dev/HoloBench.exe
```

Automated local OpenGL smoke run (opens briefly, renders 120 frames, then exits):

```powershell
./out/build/dev/HoloBench.exe --smoke-frames 120
```

For a headless core-only build:

```powershell
cmake --preset core-ci
cmake --build --preset core-ci
ctest --preset core-ci
```

## Project rules

- Visualization, optical models, and numerical backends are separate layers.
- Ray and wave solvers are distinct and declare their assumptions.
- Every physics feature needs an analytic or trusted-oracle validation before it becomes teaching content.
- GPU implementations follow a CPU reference implementation and are compared against it.
- A visually plausible result is not evidence of physical correctness.

The software license has not yet been selected. Until a license file is added, no redistribution license is granted.
