# HoloBench：面向教学、工程验证与 CHIMERA-like 全息打印的可视化光学模拟器

> 文档类型：项目背景 / 产品目标 / 技术架构 / 验收标准 / 完整开发计划 / ChatGPT 驱动研发计划  
> 文档版本：v0.2  
> 日期：2026-08-30  
> 项目代号：**HoloBench**（暂定，可后续改名）  
> 长期目标：从严肃的交互式光学教学软件，逐步演化为真实光学实验的数字孪生与设计工具，最终辅助开发自有 **CHIMERA-like 全彩数字反射全息打印系统**。

---

## 0. 执行摘要

本项目不是“做一个二维薄透镜演示器”，也不是“重新实现一个完整 Zemax”。项目应从一开始围绕一个更明确的核心方向设计：

> **在一个可交互的 3D 虚拟光学实验台中，用不同精度等级的几何光学、波动光学与全息模型解释真实光学现象；同时保留足够严肃的物理模型、自定义器件能力和实测校准接口，使软件最终能够参与真实 CHIMERA-like 全息打印系统的设计、标定和调试。**

项目分为三个长期层次：

1. **交互式实验沙箱**：直观展示焦距、成像、NA、衍射、傅里叶面、空间滤波、SLM、干涉、全息记录/重建、hogel 等概念，并形成可独立使用的严肃光学实验软件。
2. **工程模拟器**：支持真实透镜 prescription、自定义非球面、玻璃色散、真实器件误差、精确标量波传播、PSF/MTF/角谱等，能够指导实际光路搭建。
3. **CHIMERA 工具链**：模拟和标定 SLM → 光学打印头 → hogel 的角度映射，生成 hogel 数据，仿真参考光干涉、记录材料和重建，并逐步接入真实运动平台、SLM/DMD、激光器、相机和功率计。

技术主栈建议采用：

- **C++20/23**
- **SDL3**：窗口、输入、平台抽象
- **OpenGL 4.6**：3D 可视化与通用 GPU 计算
- **OpenGL Compute Shader**：默认跨显卡 GPU 光学计算后端
- **CUDA/cuFFT**：NVIDIA 上的可选高性能后端
- **Dear ImGui（Docking）**：编辑器式工具 UI
- **CPU reference backend**：用于单元测试、精度验证和无 GPU 环境

在软件执行层面，本项目默认采用 **ChatGPT 主导开发 + 自动化测试/验证 + Living Document 驱动** 的模式；开发速度以 milestone gate 而非代码量衡量。

项目必须从第一天区分：

- **Scene/Visualization**：用户看到的 3D 光学台。
- **Optical Model**：真实物理模型。
- **Numerical Backend**：ray / FFT / angular spectrum / vector diffraction 等数值实现。

这三个层次不能绑定死，否则项目后期会很难从“漂亮的教学 Demo”升级成工程工具。

---

# 1. 项目背景

## 1.1 项目起点

本项目的直接动机来自对真实反射全息、H1→H2 转印、数字全息立体图以及 CHIMERA/Zebra Imaging 等系统的研究。

传统光学资料在学习以下概念时存在明显门槛：

- 焦平面为什么特殊；
- SLM 上的位置为什么可以对应传播角度；
- Fourier plane 到底“在哪里”；
- 空间滤波到底滤掉了什么；
- NA 为什么限制空间频率和视场；
- 一个 hogel 为什么能够包含很多方向的信息；
- 为什么不同方向的物光可以记录到同一个 hogel；
- 干涉条纹如何记录波前；
- H1 为什么可以共轭重放形成实像；
- H2 为什么可以让像跨越或突出基板；
- CHIMERA 为什么能通过 SLM + 光学打印头 + 参考光逐 hogel 写入真实体积全息材料。

这些内容只看公式或静态光路图非常难形成直觉。

因此软件的核心价值是：

> **把公式、几何光线、复数光场、角谱和真实三维光路放在同一个可交互环境中。**

用户可以真正拖动透镜、改变焦距、移动白屏、改变光阑、缩小 NA、移动 SLM、查看 Fourier plane，并实时看到对应的光线、波前、强度、相位、PSF 和重建图像变化。

---

## 1.2 为什么最终目标指向 CHIMERA-like 系统

Yves Gentet 与 Philippe Gentet 在 2019 年公开的 CHIMERA 系统是一种第三代数字全息打印系统。公开指标包括：

- RGB 连续低功率激光；
- 250–500 μm hogel；
- 120° full parallax；
- 最大约 60 × 80 cm；
- 初期 ≥25 hogel/s；后续公开工作达到约 60 hogel/s；
- 每个 RGB hogel 由 SLM 光学系统生成物光，再与相干参考光干涉写入 Ultimate U04 材料。

参考：

- Yves Gentet, Philippe Gentet, *CHIMERA, a new holoprinter technology combining low-power continuous lasers and fast printing*, Applied Optics 58 (2019): https://opg.optica.org/ao/abstract.cfm?uri=ao-58-34-G226
- 后续 250 μm / 60 Hz / 三 SLM / 120° 打印描述：https://pmc.ncbi.nlm.nih.gov/articles/PMC12424619/

CHIMERA 的具体商业打印头 prescription 没有完全公开，因此真正复现它不是简单“照论文买零件”。但这恰恰适合作为一个长期工程目标：模拟器首先帮助理解，再帮助设计，再参与真实标定。

---

# 2. 项目定位

## 2.1 一句话定位

**HoloBench 是一个“可视化 3D 光学实验室 + 多精度物理模拟器 + 全息打印研发工具”。**

---

## 2.2 第一阶段产品定位

第一阶段不要宣传为“Zemax 替代品”。

更合适的定位是：

> **面向高校学生、光学爱好者、工程师和全息摄影爱好者的交互式物理光学实验室。**

它应该比传统教学 App 严肃，比专业光学 CAD 容易理解。

第一阶段沙箱应让用户通过真实器件摆放和观察逐步理解：

1. 光线与波前；
2. 反射与折射；
3. 凸透镜成像；
4. 焦平面与准直；
5. 衍射；
6. Fourier optics；
7. 空间滤波；
8. NA / PSF / 分辨率；
9. 相干与干涉；
10. 全息记录与重建；
11. H1/H2；
12. SLM / hogel / holographic stereogram；
13. CHIMERA-like 打印原理。

---

## 2.3 长期工程定位

长期版本应能够：

- 输入真实透镜参数；
- 输入真实激光波长、光束腰、M²、线宽等；
- 输入 SLM/DMD 像素参数；
- 输入真实光阑、镜组、位置误差；
- 模拟角谱和 PSF；
- 输出实际设备的预期映射；
- 导入相机测量结果，建立 measured LUT；
- 对比“理论模型 vs 实际设备”；
- 根据实测数据更新数字孪生；
- 生成真实 CHIMERA-like 打印系统所需的 hogel/angular image 数据。

---

# 3. 非目标（必须主动控制范围）

以下内容在早期**明确不是目标**：

1. 不做完整 Zemax/Code V 的所有镜头优化功能。
2. 不做通用 Maxwell 3D FDTD 求解器。
3. 不在整个实验室空间使用亚微米 3D voxel 模拟电磁场。
4. 不一开始实现完整偏振、矢量衍射、非线性介质和量子光学。
5. 不一开始做 120° / NA≈0.866 的 CHIMERA 打印头完整物理复现。
6. 不承诺 simulator 第一版就能替代真实实验。
7. 教学引导、自由沙箱和工程模式必须共用同一物理核心，不能维护互相矛盾的代码。

---

# 4. 总体项目目标

## G0：可视化教学

用户可以在 3D 光学台上拖放器件，以“看得见”的方式理解光学。

## G1：物理正确性

核心模型必须能够与解析解、成熟开源库和实际实验交叉验证。

## G2：可扩展器件模型

透镜不能只有一个 `focalLength` 参数，最终必须支持真实 surface prescription。

## G3：实时交互

快速模式下用户拖动元件时必须立即看到结果；高精度计算允许异步执行。

## G4：光学实验沙箱产品化

提供自由 3D Sandbox、实验任务、保存/分享、自动化构建和高质量 UI。

## G5：数字孪生

软件能够导入真实测量结果并对模型参数进行校准。

## G6：CHIMERA-like 研发

最终能够参与以下真实系统的开发：

```text
3D Scene / Camera Array
        ↓
Perspective Views
        ↓
Hogel / Angular Image Generator
        ↓
SLM / DMD
        ↓
Optical Printing Head
        ↓
Object Beam + Reference Beam
        ↓
Holographic Material
        ↓
Hogel-by-Hogel Printing
```

---

# 5. 验收标准总览

项目不能以“看起来差不多”为验收方式。每一个阶段都需要数值测试。

---

## 5.1 几何光学验收

### 薄透镜

对满足薄透镜条件的测试：

\[
\frac{1}{f}=\frac{1}{u}+\frac{1}{v}
\]

模拟实像位置与解析值误差：

- 教学模式：≤ 1%
- 工程 CPU reference：≤ 0.1%

### Snell 定律

对平面界面：

\[
n_1\sin\theta_1=n_2\sin\theta_2
\]

角度误差：≤ 1e-5 rad（double CPU reference）。

### 球面 / 非球面交点

必须有：

- 正入射；
- 切线；
- 极近切线；
- 从内部出射；
- total internal reflection；
- decenter / tilt

自动化测试。

---

## 5.2 波动光学验收

至少用以下解析/标准实验验证：

1. 单缝 Fraunhofer diffraction；
2. 双缝干涉；
3. 圆孔 Airy pattern；
4. Gaussian beam propagation；
5. 透镜 Fourier transform；
6. 4-f spatial filtering。

建议指标：

- 主峰位置误差 < 1%；
- 第一暗环位置误差 < 1%；
- 归一化强度 RMS 误差 < 2%；
- 无吸收传播时功率守恒误差 < 1%（考虑数值窗口裁剪后单独统计）。

---

## 5.3 开源工具交叉验证

在 `validation/python/` 建立 Python oracle。

同一输入分别运行 HoloBench 和至少一种成熟库，对比输出。

推荐：

- **waveprop**：MIT，支持 Fraunhofer/Fresnel/Angular Spectrum、off-axis、SLM 模型和 PyTorch GPU。  
  https://github.com/ebezzam/waveprop
- **TorchOptics**：MIT，可微 Fourier optics。  
  https://github.com/MatthewFilipovich/torchoptics
- **Optiland**：MIT，开放式 optical design，支持真实/近轴 ray、PSF/MTF、优化与 GPU。  
  https://github.com/optiland/optiland
- **prysm**：MIT，physical optics、ray tracing、PSF/MTF、Sellmeier 等。  
  https://github.com/brandondube/prysm
- **POPPY**：BSD-3-Clause，成熟 physical optics propagation。  
  https://github.com/spacetelescope/poppy
- **LightPipes**：BSD-3-Clause，可作为额外衍射参考。  
  https://github.com/opticspy/lightpipes

这些库建议作为**参考实现和自动验证工具**，而不是桌面 runtime 的强依赖。

---

## 5.4 光学实验沙箱验收

沙箱主线至少满足：

- 可自由旋转/平移/缩放 3D 光学台；
- 拖放至少 12 类光学元件；
- Screen/Probe 可放置在任意位置；
- Ray View 与 Wave View 可以切换；
- Screen 可显示 intensity；
- Probe 可显示 phase / angular spectrum；
- 透镜可以实时改变焦距；
- 可以演示 real image / virtual image；
- 可以演示 spatial filtering；
- 可以演示 NA 变化对 PSF 的影响；
- 可以演示 SLM position → angle；
- 可以演示两束相干光干涉；
- 可以完成透射式、反射式和 RGB 全彩 hologram record/reconstruct；
- 可以从版本化 recipe 自动构建并检查 CHIMERA-like 虚拟光路；
- 可以生成 hogel/SLM/曝光计划并做有限区域重建模拟；
- Sandbox 可以保存工程；
- UI 操作不依赖公式输入。

---

## 5.5 工程模式验收

- 支持厚透镜；
- 支持多 surface lens assembly；
- 支持球面；
- 支持 conic；
- 支持 even asphere；
- 支持自定义 aperture；
- 支持玻璃材料 Sellmeier；
- 支持 RGB wavelength；
- 支持 decenter / tilt；
- 输出 spot diagram；
- 输出 PSF；
- 输出 MTF；
- 输出 pupil/angular spectrum；
- 支持 JSON/CSV prescription 导入导出。

---

# 6. 核心架构原则

## 6.1 3D 可视化，2D/局部场计算

用户看到的是完整 3D 光路：

```text
Laser → Mirror → SLM → Lens → Aperture → Screen
```

但内部绝不能把整个空间 voxel 化。

波动光学使用多个局部截面：

\[
U(x,y)=A(x,y)e^{i\phi(x,y)}
\]

在元件平面之间传播。

这样一个 1024×1024 complex field 只需要约百万级采样点，而不是模拟 1 m³ 空间里的全部电磁场。

---

## 6.2 几何光学与波动光学必须并存

### Ray Mode

用于：

- 直觉；
- 全局路径；
- 大尺度系统布局；
- 找焦点；
- Field of View；
- aperture clipping；
- 初步 aberration。

### Wave Mode

用于：

- diffraction；
- interference；
- Fourier plane；
- spatial filtering；
- PSF；
- SLM diffraction；
- holography。

不能试图用一套模型解决所有问题。

---

## 6.3 精度等级

建议定义四级 Solver：

### L0 — Geometric Ray

- 最快；
- 3D ray tracing；
- 实时编辑默认模式。

### L1 — Paraxial / Fresnel Wave

- 教学和快速交互；
- Fresnel approximation；
- thin lens phase；
- FFT。

### L2 — Exact Scalar Wave

- Angular Spectrum；
- exact propagation phase；
- off-axis / resampling；
- 工程分析主要模式。

### L3 — High-NA Vector

长期目标：

- vector field；
- polarization；
- Richards–Wolf / Debye integral；
- high NA PSF；
- 120° CHIMERA 打印头局部验证。

---

# 7. 一个必须提前认识到的数值问题：120° 不能天真地全局均匀采样

对于二维离散光场，采样间距 `dx` 的 Nyquist spatial frequency 为：

\[
f_{max}=\frac{1}{2dx}
\]

平面波满足：

\[
\sin\theta=\lambda f_x
\]

因此若要直接表示 ±60°：

\[
dx \lesssim \frac{\lambda}{2\sin60^\circ}
\]

对 532 nm：

\[
dx \lesssim 0.307\,\mu m
\]

这意味着如果你试图直接用一个 10 mm 宽的 uniform field 表示完整 ±60° angular spectrum，每轴就可能需要三万级采样，二维场会达到十亿级 complex sample。

因此 CHIMERA 模拟不能采用：

> “全实验台统一一个超高分辨率波场网格”。

必须采用**多尺度/局部坐标/角谱域**架构，例如：

- global ray tracing；
- local wave patch；
- pupil/angular domain representation；
- resampling / scaled FFT；
- local high-NA solver；
- 只在真正需要的 250 μm hogel 区域做高精计算。

这一点应作为项目架构的硬约束。

---

# 8. 软件总体架构

```text
┌──────────────────────────────────────────────┐
│                  Application                 │
│ SDL3 / Window / Input / Project IO          │
├──────────────────────────────────────────────┤
│                  Editor UI                   │
│ Dear ImGui / Inspector / Timeline / Lesson  │
├──────────────────────────────────────────────┤
│              3D Visualization                │
│ OpenGL / Mesh / Rays / Wavefront / Gizmos   │
├──────────────────────────────────────────────┤
│               Optical Scene                  │
│ Components / Transform / Connections         │
├──────────────────────┬───────────────────────┤
│      Ray Engine      │      Wave Engine      │
│ Surface Intersection│ Complex Field         │
│ Snell/Fresnel        │ FFT / ASM / Fresnel  │
├──────────────────────┴───────────────────────┤
│             Holography Engine                │
│ Recording / Replay / H1-H2 / Hogel          │
├──────────────────────────────────────────────┤
│         Calibration / Digital Twin           │
│ LUT / Measurements / Parameter Fitting       │
├──────────────────────────────────────────────┤
│             Compute Backends                 │
│ CPU / OpenGL Compute / CUDA                  │
└──────────────────────────────────────────────┘
```

---

# 9. 推荐技术选型

## 9.1 基础语言

**C++20 或 C++23**。

原因：

- 与 SDL/OpenGL/CUDA 集成直接；
- 适合长期桌面产品；
- 可控制 GPU memory；
- 适合跨平台桌面部署；
- 方便接真实设备 SDK。

---

## 9.2 窗口和输入

推荐 **SDL3**。

SDL 的授权允许商业桌面应用使用。
官方项目：https://github.com/libsdl-org/SDL

---

## 9.3 UI

推荐 **Dear ImGui Docking**。

它非常适合工具类界面：

```text
┌ 3D View ─────────────┬ Inspector ─────┐
│                      │ Lens            │
│ Optical Bench        │ R1              │
│                      │ R2              │
├──────────────────────┼─────────────────┤
│ Intensity / Phase    │ Angular Spectrum│
└──────────────────────┴─────────────────┘
```

MIT license，适合商业发布。  
https://github.com/ocornut/imgui

---

## 9.4 3D 图形

**OpenGL 4.6**。

用于：

- 光学台渲染；
- ray visualization；
- lens mesh；
- gizmo；
- intensity texture；
- phase color visualization；
- wavefront visualization。

注意：OpenGL 4.x 不适合作为未来 macOS 的主后端。第一阶段建议明确支持：

- Windows x64；
- Linux x64。

若未来需要 macOS，可提前把 Renderer API 抽象出来，再增加 Metal/Vulkan backend。

---

## 9.5 GPU 计算

### 默认

**OpenGL Compute Shader**。

用于：

- complex field operations；
- phase mask；
- aperture；
- propagation transfer function；
- ray batch；
- image/field post process。

### FFT

OpenGL 没有标准高质量 FFT API，因此必须做 backend abstraction：

```cpp
class IFftBackend {
public:
    virtual void Forward2D(ComplexField&) = 0;
    virtual void Inverse2D(ComplexField&) = 0;
};
```

推荐：

1. **CPU reference：KissFFT**（BSD-3-Clause）  
   https://github.com/mborgerding/kissfft
2. **NVIDIA high performance：cuFFT**
3. **OpenGL portable：自己实现 Stockham/radix FFT compute shader**
4. 如果未来采用 Vulkan backend，可考虑 **VkFFT**（MIT，支持 Vulkan/CUDA/HIP/OpenCL/Metal 等）  
   https://github.com/DTolm/VkFFT

不建议直接把 FFTW 作为闭源桌面 runtime 默认依赖，因为 FFTW 的标准授权是 GPL；如使用需认真处理许可证问题。

---

## 9.6 数学库

推荐：

- GLM：graphics transform；
- Eigen：矩阵、拟合、优化、least squares。

---

## 9.7 测试

推荐 Catch2 或 doctest。

重点不是 UI 测试，而是物理 solver 必须拥有大量 deterministic unit tests。

---

# 10. 光学元件系统

建议所有元件继承统一抽象：

```cpp
OpticalElement
├── Source
├── Mirror
├── BeamSplitter
├── LensAssembly
├── Aperture
├── SLM
├── Screen
├── Detector
├── HolographicPlate
└── SpatialFilter
```

但不同 solver 对同一元件有不同表示：

```text
Lens
├ RayModel
├ WaveModel
└ VisualizationModel
```

这三个模型不能耦合在一个 Shader 中。

---

# 11. 自定义透镜设计（核心功能）

## 11.1 教学薄透镜

参数：

- focal length；
- clear aperture；
- position；
- rotation。

---

## 11.2 工程厚透镜

每个 surface：

```text
Surface #0
Radius       +38.420 mm
Thickness      6.000 mm
Glass          N-BK7
Aperture       22.000 mm

Surface #1
Radius      -112.300 mm
Thickness     20.000 mm (air)
```

---

## 11.3 Surface 类型

第一阶段支持：

- Plane
- Sphere
- Conic
- Even Asphere

非球面形式：

\[
z(r)=\frac{cr^2}{1+\sqrt{1-(1+k)c^2r^2}}+A_4r^4+A_6r^6+A_8r^8+\cdots
\]

后续：

- XY Polynomial
- Zernike freeform
- measured surface map

---

## 11.4 Glass Model

必须支持：

- Constant n；
- Cauchy；
- Sellmeier。

RGB 必须通过 `n(λ)` 自然产生色差，而不是人为写 RGB 偏移。

---

# 12. Ray Engine

Ray：

```cpp
struct Ray {
    double3 origin;
    double3 direction;
    double wavelength;
    double power;
};
```

核心模块：

- analytic surface intersection；
- reflection；
- Snell refraction；
- total internal reflection；
- aperture clipping；
- Fresnel power coefficient（第二阶段）；
- polarization/Jones（后期）。

可视化：

- chief rays；
- fan rays；
- marginal ray；
- NA cone；
- caustic；
- focal plane。

---

# 13. Wave Engine

## 13.1 基本数据

```cpp
struct ComplexField2D {
    int width;
    int height;
    double dx;
    double dy;
    double wavelength;
    ComplexBuffer field;
};
```

每个 sample：

\[
U=Ae^{i\phi}
\]

---

## 13.2 第一批传播器

### Fraunhofer

用于远场教学。

### Fresnel

用于快速近轴 propagation。

### Angular Spectrum Method

核心 exact scalar solver：

```text
U(x,y)
 ↓ FFT
Ũ(fx,fy)
 ↓ × H(fx,fy,z)
Ũz(fx,fy)
 ↓ IFFT
Uz(x,y)
```

---

## 13.3 光学元件的 wave operator

Lens：

\[
U' = U \cdot e^{-ik(x^2+y^2)/(2f)}
\]

Aperture：

\[
U'=U\cdot M(x,y)
\]

Phase SLM：

\[
U'=U\cdot e^{i\phi(x,y)}
\]

Amplitude SLM：

\[
U'=U\cdot A(x,y)
\]

---

# 14. 可视化设计

每个 Screen / Probe 可切换：

- Intensity
- Log Intensity
- Phase
- Real(U)
- Imag(U)
- Angular Spectrum
- PSF
- MTF
- Polarization（后期）

3D 主视图中可选择显示：

- Ray
- Wavefront surface
- Beam envelope
- NA cone
- Focal plane
- Pupil plane
- Fourier plane

---

# 15. 空间滤波教学模块

这是自由光学实验沙箱必须重点做好的实验。

场景：

```text
Image/SLM → Lens → Fourier Plane → Aperture → Lens → Screen
```

用户拖动 Fourier-plane aperture：

```text
Large aperture → sharp image
Small aperture → blurred image
```

同时显示 angular spectrum，被裁掉部分应高亮。

这是理解 CHIMERA optical head 的关键教学桥梁。

---

# 16. SLM 模型

必须区分：

### Ideal SLM

- perfect amplitude / phase pixels；
- no dead space。

### Real SLM

参数：

- resolution；
- pixel pitch；
- fill factor；
- dead space；
- phase range；
- bit depth；
- RGB wavelength response；
- measured response LUT。

### LCD teaching model

用于演示手机/LCD 等廉价器件：

- RGB color filter；
- polarizer；
- pixel grid；
- fill factor。

---

# 17. Holography Engine

## 17.1 第一阶段：Thin Hologram

输入：

\[
O(x,y),R(x,y)
\]

记录：

\[
I=|O+R|^2
\]

然后将记录作为 transmission/phase mask 重放。

用于教学：

- 为什么需要相干；
- 为什么记录的是 interference；
- virtual/real image；
- conjugate reconstruction。

---

## 17.2 反射体积全息不能长期用 thin model 冒充

真实 Denisyuk/H2 反射全息存在：

- volume grating；
- Bragg selectivity；
- material thickness；
- index modulation；
- wavelength/angular selectivity；
- shrinkage/swelling。

不能在厘米级材料上真的构建 150–200 nm 的完整 3D voxel index map。

后续应实现：

### Volume Hologram Model

优先使用：

- local grating vector；
- Kogelnik coupled-wave theory；
- calibrated diffraction efficiency model。

这样能够模拟：

- 记录波长；
- replay wavelength；
- Bragg angle；
- emulsion shrinkage；
- RGB response。

---

# 18. H1 → H2 模块

最终教学实验：

```text
Object
  ↓
Record H1
  ↓
Conjugate Replay
  ↓
Real Image
  ↓
Place H2 plane
  ↓
Record H2
  ↓
Replay H2
```

用户应可以拖动 H2：

- 物体全部在板后；
- transplane；
- 全部突出板前。

这是项目最有辨识度的教学内容之一。

---

# 19. CHIMERA 模块

## 19.1 数据链

```text
3D Scene
  ↓
Perspective View Generator
  ↓
Hogel Rearrangement
  ↓
Angular Image
  ↓
SLM Pattern
  ↓
Optical Head
  ↓
Hogel Object Beam
  ↓
Reference Beam Interference
  ↓
Recorded Hogel
```

---

## 19.2 第一阶段 CHIMERA 模型不要直接做 Gentet 120°

建议参数：

```text
Monochrome: 532 nm
Hogel: 1 mm
FOV: ±10° ~ ±15°
Horizontal parallax only
```

只验证：

\[
(x_{SLM},y_{SLM})\rightarrow(\theta_x,\theta_y)
\]

以及多个角度能否落到同一个 hogel aperture。

---

## 19.3 CHIMERA 工程模型最终需要输出

- angular PSF；
- SLM pixel → angle map；
- RGB map；
- hogel spatial footprint；
- hogel crosstalk；
- angular crosstalk；
- available FOV；
- optical throughput；
- exposure uniformity；
- predicted reconstructed views。

---

# 20. Simulator 与真实世界之间的 Gap 管理

项目必须从架构上承认：

> simulator 永远是模型，而不是现实本身。

但可以通过“理论 + 实测”逐步逼近。

---

## 20.1 可直接相信程度较高的部分

- 几何位置；
- 理想 focal plane；
- aperture clipping；
- ideal diffraction；
- Fourier relation；
- 理想 PSF；
- 角谱传播。

---

## 20.2 必须实测校准的部分

- 激光 wavefront；
- mode hopping；
- 实际功率稳定；
- lens manufacturing error；
- SLM nonuniformity；
- pixel crosstalk；
- stage backlash；
- material exposure response；
- holographic material shrinkage；
- vibration；
- RGB diffraction efficiency。

---

# 21. Measured Component / Digital Twin

所有核心光学元件最终都允许：

```text
Ideal Model
   +
Measured Correction
```

例如 CHIMERA 打印头真实测量：

\[
F_R(x,y)=(\theta_x,\theta_y)
\]

\[
F_G(x,y)=(\theta_x,\theta_y)
\]

\[
F_B(x,y)=(\theta_x,\theta_y)
\]

Simulator 可导入 LUT。

软件最终使用：

```text
Ideal optical prediction
       ↓
Measured residual LUT
       ↓
Digital Twin Prediction
```

目标不是要求光学系统完全无畸变，而是：

> **畸变稳定、可重复、可测量、可补偿。**

不能补偿的是已经丢失的信息，例如过大的 PSF、严重 angular overlap 和 NA 不足。

---

# 22. 交互式光学实验沙箱设计

## 22.1 当前模式优先级

### Lab — 当前主产品

自由 3D Sandbox：用户摆放、旋转和连接真实空间中的光学器件，完成透射式、反射式和 RGB 全彩全息录制/重建。

### Automation — M9

用版本化 recipe 自动构建可编辑的 CHIMERA-like bench，生成 hogel/SLM/曝光任务并模拟重建。

### Learn — 延后

已有教学 catalog/panel 保留为回归资产。只有当课程改为引导用户操作同一个自由 bench，而不是在独立参数面板中完成实验时，才恢复教学产品开发。

---

## 22.2 一个非常重要的交互原则

**公式不能成为主要交互入口。**

用户应当：

- 拖透镜；
- 拖白屏；
- 改 aperture；
- 放 Fourier probe；
- 看结果。

公式作为右侧解释，而不是要求用户先会公式才能使用。

---

# 23. 性能目标

建议参考 PC：

- 6 核 CPU；
- 16 GB RAM；
- RTX 2060 / RX 6600 级 GPU。

目标：

### UI / 3D

- 1080p：稳定 60 FPS。

### Ray

- 100k rays：交互式 ≥60 FPS（优化后目标）。

### Wave Interactive

- 1024² complex field；
- 单次传播目标 < 50 ms。

### Wave Analysis

- 2048²：< 250 ms/propagation 为可接受目标。

### High Quality

- 4096²：允许离线/异步计算。

精确性能不作为前几个 milestone 的 blocking condition，但架构不能导致 GPU→CPU 每步 readback。

---

# 24. 任务调度模型

Simulation 不应阻塞主线程。

```text
Main Thread
  ├ UI
  ├ Input
  └ 3D Rendering

Simulation Queue
  ├ Ray Job
  ├ Wave Job
  ├ FFT Job
  └ Analysis Job
```

用户拖动时：

- immediate low-res preview；
- 鼠标释放后 high-res refine。

例如：

```text
Dragging    → 512²
Idle 100 ms → 1024²
Analyze     → 2048/4096²
```

---

# 25. 工程文件结构建议

```text
HoloBench/
├── app/
│   ├── main.cpp
│   ├── editor/
│   └── lessons/
├── core/
│   ├── math/
│   ├── units/
│   └── jobs/
├── optics/
│   ├── scene/
│   ├── ray/
│   ├── wave/
│   ├── materials/
│   ├── lens/
│   ├── slm/
│   ├── holography/
│   └── chimera/
├── compute/
│   ├── cpu/
│   ├── opengl/
│   ├── cuda/
│   └── fft/
├── render/
│   ├── gl/
│   ├── gizmos/
│   └── visualization/
├── calibration/
├── hardware/
│   ├── camera/
│   ├── stage/
│   ├── slm/
│   └── laser/
├── tests/
│   ├── analytic/
│   ├── regression/
│   └── golden/
├── validation/
│   └── python/
├── assets/
├── docs/
└── third_party/
```

---

# 26. 完整开发路线图（功能依赖基线）

> **说明：**本节主要表达功能依赖关系，其中原有工期是“传统业余单人开发”的粗略基线。采用 ChatGPT 主导开发后的实际目标日历，以 **26A–26F** 为准，不应把两套时间简单相加。

以下不是强制日历，而是依赖关系。

---

## Phase 0 — 项目基础（2–4 周业余开发量）

### 工作

- CMake；
- SDL3；
- OpenGL；
- ImGui Docking；
- 3D camera；
- project serialization；
- component transform；
- unit system。

### 验收

能打开一个 3D 空场景，拖放元件并保存/加载。

---

## Phase 1 — 几何光学教学核心（4–8 周）

### 元件

- Point Source
- Collimated Source
- Screen
- Thin Lens
- Mirror
- Plane Interface
- Aperture

### 功能

- Ray visualization；
- thin lens imaging；
- focal point；
- virtual image；
- white screen image。

### 验收

解析薄透镜和 Snell 测试全部通过。

此时已经可形成第一个公开 Demo。

---

## Phase 2 — Wave Core（6–12 周）

### 实现

- ComplexField2D；
- CPU FFT reference；
- GPU FFT；
- Fresnel；
- Angular Spectrum；
- aperture；
- thin-lens phase；
- intensity/phase probe。

### 验收

Airy、single slit、double slit、Gaussian beam 与解析/开源库结果通过。

---

## Phase 3 — Fourier Optics 教学（4–8 周）

实现：

- Fourier plane；
- 4-f system；
- spatial filtering；
- NA visualization；
- PSF；
- MTF；
- angular spectrum UI。

### 验收

用户无需阅读公式即可通过交互解释：

> 为什么缩小 Fourier-plane aperture 会模糊图像。

---

## Phase 4 — 真实透镜（2–4 个月）

实现：

- spherical surfaces；
- thick lens；
- lens assemblies；
- glass dispersion；
- conic；
- asphere；
- spot diagram；
- aberration visualization。

### 验收

与 Optiland/prysm 中至少 5 个 benchmark lens 对比。

---

## Phase 5 — SLM / Coherence / Interference（2–3 个月）

实现：

- amplitude SLM；
- phase SLM；
- pixel pitch；
- fill factor；
- dead space；
- plane wave / Gaussian source；
- coherence simplification；
- two-beam interference。

### 验收

能够在 simulator 中完成：

**SLM pixel position → measured angular output** 实验。

---

## Phase 6 — Holography 教学系统（2–4 个月）

实现：

- thin hologram recording；
- reconstruction；
- conjugate wave；
- real/virtual image；
- H1/H2 workflow；
- RGB basic hologram。

### 验收

一个用户可以在软件里从零搭建：

```text
Object → H1 → Conjugate Replay → Real Image → H2
```

---

## Phase 7 — Free-form Sandbox 与自动化构建

工作：

- dynamic component scene；
- component library 与 6-DoF transforms；
- branching/merging optical paths；
- transmission/reflection/RGB holography bench presets；
- CHIMERA recipe-to-bench compiler；
- hogel/exposure/reconstruction jobs；
- unified save compatibility；
- deterministic validation and performance gates。

### 软件阶段边界

不声称复现未公开的商业打印头，也不控制真实硬件；先完成可检查、可编辑、可验证的虚拟 CHIMERA 构建和重建模拟。

---

# 26A. ChatGPT 全权开发模式：开发周期、Milestone 与执行制度

本节是在“**ChatGPT 负责软件工程实现，用户主要承担方向决策、结果验收以及未来真实硬件操作**”这一前提下，对项目开发节奏重新估算。

需要明确：AI 可以显著压缩编码、重构、测试补齐、文档维护和重复性调试的时间，但无法把所有研发工作等比例压缩。以下部分仍然是项目的真实瓶颈：

- 新物理模型是否正确；
- GPU 数值算法的稳定性与采样问题；
- 大规模性能优化；
- 第三方库/驱动/显卡兼容问题；
- 教学 UX 是否真的容易理解；
- 自由沙箱、全息实验和自动化构建的交互质量；
- 真实光学器件、材料、机械与激光器的实验验证；
- CHIMERA 打印头中尚未公开的工程 know-how。

因此本文采用“**Milestone Gate**”而不是“写完若干功能就算完成”的管理方式。

---

## 26A.1 开发模式假设

软件阶段默认以下条件成立：

1. ChatGPT 可以直接访问完整代码仓库；
2. ChatGPT 可以编译、运行测试、启动程序、检查日志和性能数据；
3. 使用 Git 进行版本管理；
4. CI 能够执行至少 Windows/Linux 的核心测试；
5. 用户不需要逐行参与编码，只在以下节点参与：
   - 产品方向变化；
   - UI/交互体验验收；
   - 物理现象主观教学效果验收；
   - 真实硬件采购和搭建；
   - 重大架构决策；
6. 开发优先级严格遵守本文，不在早期为“未来可能有用”的功能扩 scope。

在这个模式下，ChatGPT 的角色不是“代码补全工具”，而是项目的主要软件工程执行者：

```text
需求 / Living Document
        ↓
Milestone 拆解
        ↓
设计 + ADR
        ↓
编码
        ↓
单元测试 / 数值验证
        ↓
运行 Demo / 性能分析
        ↓
修复
        ↓
Milestone Acceptance
        ↓
更新文档和下一阶段计划
```

---

## 26A.2 项目时间判断

在上述假设下，建议把软件项目的时间目标定为：

| 节点 | 目标时间 | 结果 |
|---|---:|---|
| 第一个可运行 3D 光学台 | **1 周左右** | SDL/OpenGL/ImGui、拖放、薄透镜、白屏 |
| 第一个“值得展示”的几何光学 Demo | **2–3 周** | 实像/虚像、光线、焦点、NA 基础 |
| 第一个波动光学 Internal Alpha | **6–8 周** | FFT、ASM、衍射、Airy、Gaussian、Probe |
| Fourier Optics Alpha | **10–12 周** | 4-f、空间滤波、角谱、Sampling Debugger |
| Fixed-pipeline physics foundation | **已完成并保留回归** | 几何+波动+真实透镜+SLM+全息求解器资产 |
| Free-form Optical Bench | **重新基线后优先完成** | 动态器件、空间光路、Screen/Probe/Plate、统一存档 |
| Holography Sandbox | **M7 后** | 透射式、反射式、RGB 全彩录制与重建 |
| CHIMERA Automation | **M8 后** | 自动建台、hogel/曝光计划、有限区域重建 |
| 工程版 Digital Twin 初版 | **12–18 个月** | 实测 LUT、硬件标定接口、真实器件模型 |

这些是**目标区间，不是承诺工期**。如果物理核心的某个模型不能通过验证，不允许为了赶时间跳过验证进入后续阶段。

### 为什么不是“ChatGPT 几周就把全部软件写完”

大模型很容易在几周内生成大量代码，但本项目真正有价值的不是代码行数，而是：

> **一个可验证、可维护、能逐步接近真实世界的物理软件系统。**

尤其 Wave Solver、采样、FFT、off-axis propagation、高 NA 和全息材料模型，错误结果经常“看起来非常漂亮”。因此 validation 是工期的一部分，而不是完成开发后的附加工作。

---

# 26B. 软件 Milestone 总表

## M0 — Repository & Engineering Foundation

**目标工期：3–7 天**

### Deliverables

- CMake 工程；
- SDL3；
- OpenGL context；
- Dear ImGui Docking；
- GL debug layer；
- logging；
- unit test framework；
- basic CI；
- project serialization skeleton；
- documentation skeleton；
- `CPU / OpenGL Compute / CUDA` backend interface 占位。

### Gate

- Windows 主开发环境可一键 build/run/test；
- Debug GL error 为 0；
- CI 可以自动跑核心测试；
- 空项目保存/加载一致。

**完成后 tag：`m0-foundation`**

---

## M1 — 3D Optical Bench + Geometric Optics

**目标工期：1–2 周**

### Deliverables

- 3D camera；
- Gizmo；
- Point/Collimated Source；
- Screen；
- Thin Lens；
- Mirror；
- Aperture；
- plane interface；
- ray tracing；
- focal indicator；
- real/virtual image visualization；
- NA cone 初版。

### Gate

- Thin lens analytic error ≤ 0.1%（CPU reference）；
- Snell test 通过；
- 用户能在 3D 中拖动透镜和白屏找到清晰实像；
- 交互保持 60 FPS。

### 产品成果

第一支公开开发视频已经可以在此阶段制作。

**完成后 tag：`m1-geometric-optics`**

---

## M2 — Wave Optics Core

**目标工期：2–4 周**

### Deliverables

- `ComplexField2D`；
- CPU reference FFT；
- GPU FFT backend；
- Fraunhofer；
- Fresnel；
- Angular Spectrum；
- Plane Wave；
- Gaussian Beam；
- aperture mask；
- thin lens phase；
- Screen/Probe intensity + phase。

### Gate

必须通过：

- FFT round trip；
- plane wave propagation；
- Gaussian beam；
- single slit；
- double slit；
- Airy disk；
- energy conservation；
- 与 waveprop/TorchOptics 至少 3 个 case 的交叉验证。

目标：1024² interactive propagation < 50 ms（参考 GPU）。

**完成后 tag：`m2-wave-core`**

---

## M3 — Fourier Optics & Sampling Debugger

**目标工期：2–3 周**

### Deliverables

- Fourier plane；
- 4-f system；
- spatial filtering；
- angular spectrum visualizer；
- PSF；
- MTF；
- Sampling Debugger；
- aliasing/wrap-around/padding warning；
- Probe 任意平面查看。

### Gate

用户无需阅读公式，可以通过操作回答：

1. Fourier plane 是什么；
2. aperture 为什么使图像变模糊；
3. NA 为什么影响分辨率；
4. 为什么采样错误会得到假结果。

数值上与标准 diffraction case 一致。

**完成后 tag：`m3-fourier-optics`**

---

## M4 — Real Lens Engineering Model

**目标工期：3–6 周**

### Deliverables

- Thick lens；
- spherical surfaces；
- conic；
- even asphere；
- lens assembly；
- Cauchy / Sellmeier；
- RGB ray tracing；
- decenter / tilt；
- spot diagram；
- prescription JSON/CSV；
- Optiland/prysm validation bridge。

### Gate

至少 5 组 benchmark lens：

- ray intersection；
- focal position；
- spot diagram；
- chromatic focal shift；

与参考软件/库的误差达到文档定义目标。

**完成后 tag：`m4-real-lens`**

---

## M5 — SLM / Coherence / Interference

**目标工期：2–4 周**

### Deliverables

- ideal amplitude SLM；
- ideal phase SLM；
- real SLM pixel grid；
- pitch / fill factor / dead space；
- LCD teaching model；
- position → angle experiment；
- coherence length 教学模型；
- two-beam interference；
- fringe visualization。

### Gate

Simulator 内可完成完整实验：

```text
Laser → SLM → Lens → Angular Probe
```

并输出：


a) SLM coordinate → angular distribution；

b) angular PSF；

c) 不同 wavelength 的 mapping。

**完成后 tag：`m5-slm-interference`**

---

## M6 — Holography Core

**目标工期：3–5 周**

### Deliverables

- object/reference complex field；
- thin hologram record；
- replay；
- conjugate reconstruction；
- real/virtual holographic image；
- RGB 基础；
- H1→H2 教学流程；
- transplane image placement；
- volume hologram framework 占位；
- Kogelnik model 第一版。

### Gate

用户能在一个 project 里完成：

```text
Object
  ↓
H1 Record
  ↓
Conjugate Replay
  ↓
Real Image
  ↓
H2 Record
  ↓
H2 Replay
```

并能把 H2 前后移动，观察 image plane 穿过基板。

**完成后 tag：`m6-holography`**

---

## M7 — Free-form 3D Optical Bench Sandbox

**优先级：当前最高，阻断 M8/M9**

### Deliverables

- 统一、动态、可保存的 3D bench scene，而不是固定的 `source → lens → screen` 结构；
- 用户从器件库拖入并摆放/旋转至少 12 类器件；
- RGB Laser、Object/Wavefront Source、Mirror、Beam Splitter/Combiner、Thin Lens、Real Lens、Aperture、Spatial Filter、SLM、Screen、Probe、Holographic Plate；
- 从实际三维几何自动求交并生成分支/合束光路；
- 每条光束显式携带 wavelength、power、coherence ID、optical path 和 branch provenance；
- Ray Mode 用于整体布局，局部 2D complex field 用于 wave analysis，禁止把整个空间 voxel 化；
- 视口 gizmo、Inspector 精确输入、选择/复制/删除、全场景 undo/redo；
- 一个普通统一 project 保存全部器件、变换、参数和来源，不再新增互不相通的 panel project。

### Gate

用户可以从空白 bench 只依靠器件库搭出包含激光、分光、反射、透镜、光阑、合束、白屏/Probe 和全息干板的有效光路。分光功率守恒，循环有显式终止诊断，移动器件会使依赖结果失效，保存/加载保持完整语义。

详细规范：[M7 Optical Bench Sandbox](docs/milestones/M7_OPTICAL_BENCH_SANDBOX.md)。

**完成后 tag：`m7-optical-bench-sandbox`**

---

## M8 — Holography Recording and Reconstruction Sandbox

### Deliverables

- 透射式薄全息和体全息的录制、物理重放与观察；
- 反射式体全息（包含 Denisyuk-style 对向入射）的录制与 Bragg 选择性重建；
- RGB 全彩全息按波长分别录制/重建，再以明确的未标定显示变换合成；
- object/reference 光必须来自 M7 bench 中真实到达干板的 coherent branches；
- 干板局部坐标、入射侧、OPD、grating vector/period、exposure、detuning、efficiency、sampling 和 stale provenance 全部可见；
- 透射、反射、全彩三套示例都是普通统一 bench project，不是特殊计算器。

### Gate

用户能从空白或普通 preset 搭建并完成：

1. transmission hologram record/reconstruct；
2. reflection hologram record/reconstruct；
3. RGB full-colour hologram record/reconstruct。

三条路径必须通过独立数值 oracle、保存兼容、性能和 renderer smoke。

详细规范：[M8 Holography Sandbox](docs/milestones/M8_HOLOGRAPHY_SANDBOX.md)。

**完成后 tag：`m8-holography-sandbox`**

---

## M9 — Automated CHIMERA Construction and Reconstruction

### Deliverables

- versioned `ChimeraRecipe` 自动生成一个完全可编辑的普通 M7 RGB 打印头 bench；
- scene/view → hogel angular samples → SLM commands 数据链；
- RGB laser / SLM / relay optics / reference path / material 的自动放置与约束报告；
- hogel-by-hogel、wavelength-by-wavelength 的虚拟 stage/SLM/laser/exposure event plan；
- 复用 M8 干板录制，不维护另一套 printer hologram physics；
- 单 hogel 与有限多 hogel 区域的方向视图/重建模拟；
- hogel pitch、FOV、NA、SLM sampling、focal length、reference angle、exposure、thickness、shrinkage 的确定性参数扫描；
- 为未来 measured SLM/camera/stage/material LUT 留下版本化接口，但本 milestone 不控制真实硬件。

### Gate

一个 canonical recipe 能确定性构建完整 RGB CHIMERA-like bench，生成单 hogel RGB 曝光计划，并重建至少两个可区分观察方向的有限多 hogel 示例。所有自动生成器件都有来源，结果可由用户在同一个 bench 中检查和修改，不存在隐藏 solver graph。

详细规范：[M9 CHIMERA Automation](docs/milestones/M9_CHIMERA_AUTOMATION.md)。

**完成后 tag：`m9-chimera-automation`**

---

# 26C. ChatGPT 驱动的工程工作流

## 26C.1 每个 Milestone 都采用同一循环

每个 milestone 开始时，ChatGPT 必须先生成：

```text
Milestone Brief
├ Goal
├ User-visible Outcome
├ Physics Assumptions
├ Architecture Changes
├ Tasks
├ Tests
├ Numerical Validation
├ Performance Budget
└ Acceptance Checklist
```

禁止直接“开始堆代码”。

每个功能的最小闭环：

```text
spec
 ↓
reference/analytic result
 ↓
CPU implementation
 ↓
tests
 ↓
GPU implementation
 ↓
CPU/GPU comparison
 ↓
UI visualization
 ↓
regression case
 ↓
documentation
```

这样能够最大程度避免 AI 很快写出大量“看起来能跑、实际上物理错误”的代码。

---

## 26C.2 Repository 中必须长期维护的 AI 上下文文件

建议增加：

```text
/AGENTS.md
/docs/PROJECT_STATE.md
/docs/ARCHITECTURE.md
/docs/PHYSICS_ASSUMPTIONS.md
/docs/VALIDATION_STATUS.md
/docs/PERFORMANCE_BUDGET.md
/docs/KNOWN_LIMITATIONS.md
/docs/adr/
/docs/milestones/
```

### `AGENTS.md`

告诉每一个新 ChatGPT coding session：

- 项目目标；
- build/test 命令；
- code style；
- 禁止事项；
- 物理验证要求；
- dependency policy；
- GPU backend policy。

### `PROJECT_STATE.md`

只保存当前真实状态：

```text
Current milestone
Completed
In progress
Known broken
Next 5 tasks
```

避免长周期项目因为聊天上下文变化而失去状态。

### `PHYSICS_ASSUMPTIONS.md`

任何近似必须显式记录，例如：

```text
Scalar field
Monochromatic
Paraxial approximation
Thin lens
No polarization
No multiple reflections
```

用户在 UI 中也应该能够看到当前 solver 使用了哪些假设。

---

## 26C.3 AI 开发的 Definition of Done

一个功能只有同时满足以下条件才算完成：

- [ ] 编译通过；
- [ ] 单元测试通过；
- [ ] 有至少一个 analytic/golden validation；
- [ ] CPU reference 与 GPU 结果一致；
- [ ] 没有新的 GL validation error；
- [ ] 有性能测量；
- [ ] UI 可观察结果；
- [ ] 已记录 model limitation；
- [ ] 项目可以保存/重新打开该实验；
- [ ] regression scene 已加入；
- [ ] 文档已同步更新。

这比“代码已经写出来”严格得多，也是长期由 ChatGPT 主导开发时最重要的质量控制手段。

---

# 26D. 前 12 周：ChatGPT 模式下的实际节奏

原始“12 周计划”保留作为功能依赖参考，但在 AI 主导模式下，建议改为以下目标节奏。

## Week 1 — Foundation Vertical Slice

完成：

- repo / CI；
- SDL3/OpenGL/ImGui；
- 3D camera；
- component；
- thin lens；
- screen；
- basic rays。

**周末成果：拖透镜和白屏找焦点。**

---

## Week 2 — Geometric Optics Alpha

- mirror；
- refraction；
- aperture；
- virtual image；
- NA cone；
- project save/load；
- analytic tests。

**成果：M1。**

---

## Week 3–5 — Wave Core

- complex field；
- FFT；
- ASM；
- Fresnel；
- Gaussian；
- Airy；
- intensity/phase probe；
- GPU backend；
- Python oracle。

**成果：M2。**

---

## Week 6–7 — Fourier Optics

- Fourier plane；
- 4-f；
- spatial filtering；
- angular spectrum；
- PSF；
- Sampling Debugger。

**成果：M3 + 第一支真正有产品辨识度的 Demo。**

---

## Week 8–9 — Real Lens Foundation

- thick lens；
- spherical/aspheric surface；
- dispersion；
- prescription；
- benchmark validation。

不要求 M4 在 Week 9 全部结束，但必须建立正确架构。

---

## Week 10 — SLM Prototype

- ideal amplitude SLM；
- pixel pitch；
- fill factor；
- position → angle visualization；
- angular PSF。

**成果：第一次在软件里模拟 CHIMERA 的核心概念。**

---

## Week 11 — Interference

- coherent source；
- two-beam interference；
- fringe spacing；
- phase visualization；
- coherence teaching controls。

---

## Week 12 — First Major Alpha

集中做：

- bug fixing；
- validation；
- performance；
- example projects；
- documentation；
- recording demo video。

### 12 周必须达到的产品体验

用户能够：

1. 在 3D 中自由搭光路；
2. 用 ray mode 理解成像；
3. 切换 wave mode；
4. 看衍射和相位；
5. 看 Fourier plane；
6. 拖 aperture 实时空间滤波；
7. 看 NA/PSF；
8. 放一个 SLM，观察位置到角度的映射；
9. 让两束激光产生干涉条纹。

做到这一点后，项目已经不是“概念 Demo”，而是一个有独立价值的 Optical Lab Alpha。

---

# 26E. 软件与真实 CHIMERA 的长期周期判断

ChatGPT 对软件开发加速明显，但对真实 CHIMERA 硬件的加速会逐渐下降。

建议按下表理解整个十年尺度路线：

| 长期节点 | 目标 | 合理日历尺度 |
|---|---|---:|
| SW-Alpha | 严肃可视化光学实验室 | 3–5 个月 |
| Optical Bench Sandbox | 可自由搭建并保存空间光路 | 当前软件最高优先级 |
| Holography Sandbox | 透射/反射/RGB 录制与重建 | M7 完成后 |
| CHIMERA Automation | 虚拟打印头、hogel 与重建 | M8 完成后 |
| Digital Twin v0 | 理论模型 + 实测 LUT | 12–18 个月 |
| CH-1 | 实物 SLM position→angle 标定台 | 1–2 年内 |
| CH-2 | 单色 1 mm hogel HPO | 1–3 年 |
| CH-3 | ~0.7 mm，接近 Zebra 空间尺度 | 2–4 年 |
| CH-4 | RGB 0.5–0.7 mm | 3–5 年 |
| CH-5 | 500 μm / 60–90° | 4–7 年 |
| CH-6 | 250 μm RGB | 5–10 年 |
| CH-7 | 250 μm / 120° full parallax | 可能 7–15+ 年 |

这不是因为代码需要十几年，而是因为越往后，主要工作从软件开发变成：

- 光学打印头设计；
- SLM/DMD 实物行为；
- 高 NA optics；
- 全彩记录材料；
- RGB 动态范围；
- 精密运动；
- 激光相干/稳定；
- 实验迭代；
- 成本与可制造性。

因此长期策略应是：

> **每一级 CH milestone 都必须产生独立可展示成果，不把所有价值押在最终 CH-7。**

---

# 26F. 建议的工程交付节奏

为了避免一个十年项目长期没有外部反馈，建议：

```text
Step 1      Free-form Optical Bench vertical slice
Step 2      Transmission holography record/reconstruct
Step 3      Reflection and RGB holography record/reconstruct
Step 4      CHIMERA recipe and single-hogel virtual exposure
Step 5      Bounded multi-hogel reconstruction and parameter sweeps
Later       Measured calibration and physical CHIMERA experiments
```

软件沙箱和 CHIMERA 研发应形成正反馈：

```text
光学实验与仿真用户
   ↓
反馈 / 收入 / 社区
   ↓
HoloBench 更成熟
   ↓
真实光学研发能力增强
   ↓
CHIMERA 实验内容
   ↓
反过来成为新的教学/工程功能
```

这样即使最终 CHIMERA 需要十年以上，项目在第一年就已经能够形成独立产品价值。

---

# 27. CHIMERA 长期开发路线

这一部分可以跨越几年甚至十年以上。

---

## CH-0 — 虚拟打印头

目标：纯 simulator。

```text
SLM → Lens → Angular Mapping → Hogel Plane
```

要求：

- 532 nm；
- FOV 20–30°；
- 1 mm virtual hogel。

---

## CH-1 — 实物 position → angle 实验

真实硬件：

- 单色相干 532 nm；
- LCD/DMD/SLM；
- lens；
- camera on rotary stage。

任务：实测：

\[
x_{SLM}\rightarrow\theta
\]

并导入 simulator。

### 验收

理论模型和 measured LUT 之间残差可视化。

这是数字孪生真正开始的节点。

---

## CH-2 — 单色大 hogel holographic stereogram

参数目标：

- 532 nm；
- 1 mm hogel；
- 20–30° HPO；
- 5×5 cm。

第一目标不是画质，而是：

> 从不同水平位置确实重建不同视图。

---

## CH-3 — 接近 Zebra 基础尺度

目标：

- 0.7 mm hogel；
- 40–60°；
- HPO；
- 单色 → RGB。

如果成功，此时已经能够与典型早期 Zebra holographic stereogram 的空间量化尺度进行直观比较。

---

## CH-4 — RGB 单 SLM 分时打印

不急于使用三个 SLM。

```text
SLM R Pattern → R Exposure
SLM G Pattern → G Exposure
SLM B Pattern → B Exposure
Move stage
```

虽然慢，但大幅降低早期光学复杂度。

目标：

- 0.5–0.7 mm；
- RGB；
- 60°。

---

## CH-5 — 500 μm / 90°

开始真正解决：

- high NA；
- angular PSF；
- RGB correction；
- hogel crosstalk；
- material dynamic range。

---

## CH-6 — 250 μm

目标：

- 250 μm hogel；
- ≥90°；
- RGB；
- high-quality reconstruction。

这是进入 CHIMERA 级空间采样的重要门槛。

---

## CH-7 — 120° Full Parallax

最终研究目标：

- 250 μm；
- 120°；
- RGB；
- full parallax；
- corrected optical head；
- measured LUT；
- stable materials；
- automated printing pipeline。

此阶段必须启用：

- L3 high-NA vector solver；
- polarization；
-真实 pupil calibration；
- optical head measured PSF；
- material calibration。

---

# 28. 原始 12 周功能依赖参考（非当前日历）

> **说明：**本节保留用于说明功能先后依赖；在 ChatGPT 全权开发模式下，当前执行日历已经由 **26D** 取代。遇到冲突时，以 26D 的 milestone/gate 为准。

如果马上开始开发，建议不要先研究 hologram。

---

## Week 1–2

- 创建 repository；
- CMake；
- SDL3；
- OpenGL context；
- ImGui docking；
- camera；
- transform gizmo；
- component base。

**成果：可拖动 3D optical bench。**

---

## Week 3–4

- Ray；
- point source；
- screen；
- thin lens；
- mirror；
- ray/lens intersection；
- ray visualization。

**成果：白屏找焦点。**

---

## Week 5

- optical probe；
- focal plane indicator；
- NA cone；
- virtual image visualization。

---

## Week 6

- ComplexField2D；
- CPU complex math；
- CPU FFT reference；
- intensity/phase texture display。

---

## Week 7–8

- OpenGL compute FFT；
- Angular Spectrum propagation；
- plane wave；
- Gaussian beam。

**成果：移动 Screen 时看到真实 diffraction/focus。**

---

## Week 9

- Aperture mask；
- circular aperture；
- Airy pattern；
- automated validation。

---

## Week 10

- Fourier lens；
- Fourier probe；
- angular spectrum view。

---

## Week 11

- 4-f optical system；
- spatial filtering lesson。

---

## Week 12

- polish；
- demo project；
- regression tests；
- performance profiling；
- 发布第一个内部 Alpha。

### 12 周验收

必须能够录一段视频演示：

1. 拖入激光器；
2. 拖入透镜；
3. 移动 screen 找焦；
4. 切到 Wave Mode；
5. 看 Airy spot；
6. 搭 4-f；
7. 缩 aperture；
8. 看图像变模糊；
9. 同时看 Fourier spectrum 被裁剪。

如果这段体验成立，项目方向就是正确的。

---

# 29. 物理验证体系

目录：

```text
tests/analytic/
```

必须包含：

- thin_lens_test
- snell_test
- fresnel_interface_test
- gaussian_beam_test
- airy_disk_test
- single_slit_test
- double_slit_test
- fresnel_propagation_test
- angular_spectrum_plane_wave_test
- fft_round_trip_test
- energy_conservation_test

另外：

```text
validation/python/
```

存放对 waveprop / Optiland / TorchOptics 的对照脚本。

每次修改核心 solver，CI 自动生成 numeric metrics。

---

# 30. Sampling Debugger（强烈建议做成核心特色）

很多波动光学 simulator 的错误其实来自采样而不是物理。

因此 UI 应实时提示：

```text
λ                 532 nm
Grid              1024 × 1024
Pixel pitch       4 μm
Physical width    4.096 mm
Nyquist angle     3.81°
Requested FOV     12°   ⚠ ALIASING
```

提示：

- aliasing；
- wrap-around；
- insufficient padding；
- aperture too close to boundary；
- evanescent component；
- insufficient angular bandwidth。

这不仅帮助用户，也防止开发者自己被错误结果欺骗。

---

# 31. 真实实验接口

长期加入 `hardware/`：

### Camera

- capture；
- exposure；
- centroid；
- PSF measurement。

### Stage

- XYZ；
- rotation；
- encoder。

### SLM/DMD

- upload pattern；
- trigger；
- sync。

### Laser

- shutter；
- power monitor。

第一原则：

**硬件控制层必须是插件，不得污染 optics solver。**

---

# 32. 真实 CHIMERA 标定工作流

最终目标工作流：

```text
Simulator predicts mapping
        ↓
Generate SLM calibration pattern
        ↓
Display on physical SLM
        ↓
Camera measures actual angle/PSF
        ↓
Fit mapping + residual
        ↓
Import into HoloBench
        ↓
Simulator becomes calibrated digital twin
        ↓
Generate compensated hogel patterns
```

RGB：

\[
F_R,F_G,F_B
\]

分别标定。

这意味着未来不必追求一个“零畸变”的昂贵打印头。

更合理的工程目标是：

> **低模糊 + 高 NA + 高稳定 + 可校准。**

---

# 33. 项目最大技术风险

## Risk 1：把项目做成 Zemax 克隆导致永远无法完成

### 对策

所有功能必须服务于“可视化理解 + 全息打印”主线。

---

## Risk 2：把几何光学结果误认为波动光学结果

### 对策

Ray/Wave 模式 UI 必须明确区分。

---

## Risk 3：数值采样产生漂亮但错误的图

### 对策

Sampling Debugger + analytic test。

---

## Risk 4：120° 高 NA 后 scalar approximation 失效

### 对策

L3 vector solver 作为明确长期 milestone，而不是偷偷继续用近轴模型。

---

## Risk 5：真实材料无法准确模拟

### 对策

Material model 必须支持 empirical LUT，而不是只依赖理论公式。

---

## Risk 6：桌面 runtime 许可证

### 对策

runtime 尽量选择 permissive license：SDL、ImGui、KissFFT、VkFFT 等；GPL 库用于开发验证时与发布二进制隔离。

---

## Risk 7：CUDA 绑定 NVIDIA

### 对策

CUDA 永远是 optional backend；核心接口以 vendor-neutral backend 抽象。

---

# 34. 开源库使用策略

## 34.1 不建议

把一个 Python optical library 直接嵌入 C++ 桌面程序作为核心 runtime。

原因：

- 发布复杂；
- 性能和 GPU interop 复杂；
- 长期接口受外部项目限制。

## 34.2 推荐

把成熟 Python 库当作：

- 算法参考；
- golden model；
- 自动 benchmark；
- 验证工具。

其中最值得优先研究：

### Optiland

Python、MIT；目前处于活跃开发，涵盖真实 ray、paraxial、polarization-aware tracing、PSF/MTF、优化和 GPU。很适合成为真实 lens 模块的验证参考。

https://github.com/optiland/optiland

### waveprop

MIT；非常贴近项目需要，包含 scalar diffraction、Angular Spectrum、Fresnel、Fraunhofer、off-axis、SLM 模型和 GPU/PyTorch。

https://github.com/ebezzam/waveprop

### TorchOptics

MIT；可微 Fourier optics。长期如果希望做“自动优化打印头/phase mask”，它尤其值得参考。

https://github.com/MatthewFilipovich/torchoptics

### prysm

MIT；PSF、MTF、Sellmeier、thin lens、interferometry 等模块丰富。

https://github.com/brandondube/prysm

### POPPY

BSD-3-Clause；成熟 physical optics propagation，适合做 diffraction 参考。

https://github.com/spacetelescope/poppy

---

# 35. 项目中期可能形成的独立价值

即使十年以后仍没有做出完整 CHIMERA，项目本身也可以是成功的。

它可以形成至少三个独立成果：

### A. 交互式科学教育与光学实验沙箱

有明确商业价值。

### B. 开源/科研级 Fourier Optics Visualizer

帮助学习者理解难以直觉化的空间频率和波前问题。

### C. Optical Digital Twin / Calibration Tool

可以成为实际 DIY 全息系统调试软件。

因此不应该把“是否最终造出 120° CHIMERA”作为整个项目唯一成败标准。

---

# 36. 长期愿景

最终 HoloBench 不只是“模拟光学”，而可以形成闭环：

```text
                 ┌───────────────┐
                 │   HoloBench   │
                 │   Simulator   │
                 └──────┬────────┘
                        │
                 predicted setup
                        ↓
             ┌────────────────────┐
             │   Physical Bench   │
             │ Laser / SLM / Lens │
             └─────────┬──────────┘
                       │
                  measurement
                       ↓
             ┌────────────────────┐
             │ Camera / Detector  │
             └─────────┬──────────┘
                       │
                    calibration
                       ↓
                 ┌───────────────┐
                 │ Digital Twin  │
                 └───────────────┘
```

然后同一套软件直接输出：

- 真实 SLM pattern；
- hogel dataset；
- stage trajectory；
- RGB exposure；
- correction LUT。

最终目标是：

> **模拟器不是 CHIMERA 项目的旁支，而是 CHIMERA 设备的软件核心之一。**

---

# 37. 最重要的项目决策（建议锁定）

1. **C++20/23。**
2. **SDL3 + OpenGL 4.6 + Dear ImGui。**
3. **Compute Shader 为默认 GPU backend，CUDA 可选。**
4. **Ray 与 Wave 使用不同 solver。**
5. **3D 用于交互，波动计算使用局部 2D complex fields。**
6. **第一年不做完整 120° vector CHIMERA。**
7. **所有物理核心必须有解析/开源库对照测试。**
8. **透镜从一开始预留真实 prescription 架构。**
9. **Measured LUT / Digital Twin 从数据结构层面预留。**
10. **虚拟 CHIMERA milestone 不依赖真实硬件完成。**
11. **最终 CHIMERA 通过逐级 hardware milestone 实现，不直接跳到 250 μm / 120°。**

---

# 38. 下一步

项目启动后的第一件事不是写 FFT，也不是研究 CHIMERA 论文。

建议立即建立最小 vertical slice：

```text
SDL3
 +
OpenGL 3D View
 +
ImGui Inspector
 +
Laser Source
 +
Thin Lens
 +
Movable Screen
 +
Ray Visualization
```

先实现一个最基本但体验完整的功能：

> **用户在 3D 场景里拖动透镜和白屏，立即看到真实/虚像与焦平面变化。**

然后再把同一个场景升级为 Wave Solver。

这会验证整个项目最重要的 UX 假设：

> “复杂光学能不能通过可视化交互真正变得容易理解？”

如果答案是肯定的，之后的 Fourier optics、holography 和 CHIMERA 都是在这个基础上自然扩展。

---

# 参考资料

1. Yves Gentet, Philippe Gentet, **CHIMERA, a new holoprinter technology combining low-power continuous lasers and fast printing**, Applied Optics 58, G226-G230 (2019).  
   https://opg.optica.org/ao/abstract.cfm?uri=ao-58-34-G226

2. CHIMERA 后续应用，包含 250 μm hogel、60 Hz、三 SLM、120° full-color optical head 和 640/532/457 nm 激光描述。  
   https://pmc.ncbi.nlm.nih.gov/articles/PMC12424619/

3. waveprop — scalar diffraction / Angular Spectrum / Fresnel / SLM。MIT。  
   https://github.com/ebezzam/waveprop

4. TorchOptics — differentiable Fourier optics。MIT。  
   https://github.com/MatthewFilipovich/torchoptics

5. Optiland — open-source optical design framework。MIT。  
   https://github.com/optiland/optiland

6. prysm — physical optics / PSF / MTF / ray tools。MIT。  
   https://github.com/brandondube/prysm

7. POPPY — physical optics propagation。BSD-3-Clause。  
   https://github.com/spacetelescope/poppy

8. LightPipes — wave optics toolkit。BSD-3-Clause。  
   https://github.com/opticspy/lightpipes

9. SDL3。  
   https://github.com/libsdl-org/SDL

10. Dear ImGui。  
    https://github.com/ocornut/imgui

11. KissFFT — BSD-3-Clause。  
    https://github.com/mborgerding/kissfft

12. VkFFT — MIT，多 GPU API FFT backend。  
    https://github.com/DTolm/VkFFT

---

## 文档维护规则

这份文档应作为 living document。

建议每完成一个大版本更新：

- `Current Capability`
- `Known Physical Limitations`
- `Validation Status`
- `Next Milestone`

四个表。

尤其任何看起来“很漂亮”的新物理效果，在加入教学课程之前必须先进入 validation suite。
