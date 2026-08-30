# Third-party dependency notices

This inventory covers dependencies declared by M0. Distribution packaging must include the corresponding full license texts and repeat the audit against the resolved source revisions.

| Dependency | Pinned source | License | Runtime role |
|---|---|---|---|
| SDL | `f5e5f6588921eed3d7d048ce43d9eb1ff0da0ffc` (`release-3.2.30`) | zlib | Window, input, OpenGL context |
| Dear ImGui docking | `b48d1afbe8ee8b238e2961dc363a949dd7304e23` (`v1.92.9b-docking`) | MIT | Editor UI |
| nlohmann/json | `65ee68451d8eb2b5f3a30b410476ab83deb3289b` (`v3.12.0`) | MIT | Project JSON |
| doctest | `2d0a9359a60c51affe2a9bebb1be1dca47868151` (`v2.5.3`) | MIT | Tests only |
| glad generator | `73db193f853e2ee079bf3ca8a64aa2eaf6459043` (`v2.0.8`) | MIT; generated loader is public-domain/WTFPL/CC0 with Khronos notices | OpenGL 4.6 Core loading |
| GLM | `a532f5b1cf27d6a3c099437e6959cf7e398a0a67` (`1.0.2`) | MIT | Renderer matrices and camera math |

No third-party dependency changes the as-yet-undecided license of HoloBench itself.
