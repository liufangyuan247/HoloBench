# HoloBench 光学实验台操作指南

HoloBench 的主产品界面是 `Optical Bench` 中的自由 3D 实验台。底部的
Wave、SLM、Holography 等固定面板是数值参考与诊断资产，不是完成实验的
必经入口。

## 启动

```powershell
cmake --preset dev
cmake --build --preset dev
./out/build/dev/HoloBench.exe
```

启动后使用中央 `Optical Bench`。右键拖动会让实验台跟随鼠标方向旋转，
中键拖动平移视角，滚轮缩放。选中器件后按 `F`（或点击 Inspector 中的
`Focus (F)`）会把该器件置于视图中心并按其尺寸取景；按住 `Shift` 再用
`W/A/S/D` 可沿当前视角前后左右漫游。首次布局中，`Inspector` 位于右侧，
诊断窗口位于底部。

## 自由搭建实验台

1. 点击 `Empty Bench` 清空实验台。
2. 在顶部 `Components` 器件架搜索器件。单击会把器件放到当前视图中心；
   从按钮拖到视口会按鼠标射线落到光学平台上。
3. 单击视口中的器件进行选择。按 `W` 使用世界坐标平移，按 `E` 使用器件
   局部坐标旋转，然后拖动红、绿、蓝 X/Y/Z 手柄。
4. 用顶部对准栏执行 `Aim +Z`、共轴、同高、沿目标轴定距或
   `Snap to beam`。这些操作修改的都是普通器件位姿，可以撤销、保存和再次编辑。
5. 需要精确数值时在 `Inspector` 编辑 SI 参数、位姿和光学属性。复制、删除、
   `Ctrl+Z`、`Ctrl+Y` 都作用于同一个 Bench 历史。

全局路径由实际三维几何射线求交产生；只有在透镜、光阑、SLM、干板和
Screen/Probe 等局部平面上才建立二维复光场。不同波长始终独立，不会发生
跨波长干涉。

## 用虚拟平面查看空间光场

`Screen / Detector` 是会截获光路的实体白屏；`Field Probe` 是透明、非阻挡的
虚拟观察平面。要移动探查双缝、单缝或圆孔后的空间光场：

1. 从 `Components` 器件架把 `Field Probe` 放到光阑下游，并用移动/旋转手柄
   设置它的空间位置和朝向。
2. 选中该 Probe。3D 视口上方会出现 `Virtual field plane`，保持
   `Observe light field` 勾选。
3. 当前局部强度会直接贴在虚拟平面上；移动或旋转 Probe 即可扫描不同截面。
   拖动时使用最多 256×256 的有界预览，释放后按 Probe 的采样设置更新，
   每轴最多 512。
4. Probe 不会终止全局射线路由，因此可在同一束光上放置或移动多个观察位置；
   需要模拟真实白屏截光时则使用 `Screen / Detector`。

该功能不会体素化整个实验室。HoloBench 仍用射线确定全局光路，只在选中的
Probe 平面上建立局部二维复光场，所以可交互移动，同时保留干涉和衍射相位。
对于已录制全息图的重建，应在干板操作栏把目标 Screen/Probe 选为观察面，
再点击 `Reconstruct`。

## 透射式全息录制与重建

最快的可编辑起点是顶部 `Transmission`：

1. 载入后会自动选中 `plate-h1`，但所有激光器、物光、透镜、分光器、干板和
   Screen 都仍是普通可移动器件。
2. 查看 `Plate experiment` 行。它应显示可用的 object/reference 分支和
   transmission 配对；`Incident details` 可检查波长、相干 ID、光程差和夹角。
3. 模式选择 `Thin transmission`（或在配对唯一时使用 Auto），点击 `Record`。
4. 在观察面下拉框选择已放置的 Screen/Probe，选择 ordinary 或 conjugate
   reference，然后点击 `Reconstruct`。
5. 重建图像会贴到实际放置的观察面。此后移动任一相关器件会把结果标为
   `STALE` 并隐藏旧图像，重新 `Record`/`Reconstruct` 即可。

## 反射式 / Denisyuk 全息

点击顶部 `Reflection / Denisyuk`，或从空 Bench 自行让相干物光与参考光从
干板相对两侧入射：

1. 选择干板，确认 `Plate experiment` 中出现 reflection 配对。
2. 选择 `Reflection / Denisyuk`，点击 `Record`。记录使用体全息模型，光栅
   来自实际到达干板的两条分支。
3. 选择反射侧的 Probe/Screen，点击 `Reconstruct`。体全息重放使用已记录的
   reference 分支，并报告 Bragg 失谐和衍射效率。

## RGB 全彩全息

点击顶部 `RGB Full-colour`：

1. 选择干板，确认状态显示 `RGB ready`；三个物光/参考光对必须分别属于
   R/G/B，并且同一对具有相同波长和相干身份。
2. 选择 `RGB full-colour` 并点击 `Record`。系统分别记录三个波长，不会把
   不同颜色当作相干场相加。
3. 选择 Screen/Probe 并点击 `Reconstruct`。三通道分别重建，最后只在显示层
   合成强度；未加载实测色彩标定时，结果明确标记为相对预览。

## CHIMERA 自动建台、曝光和重建

点击顶部 `CHIMERA` 会生成一个包含 23 个普通器件的可编辑 RGB 反射式
虚拟打印台。推荐的最短工作流是：

1. 点击 `Generate Dataset + Exposure Plan`，生成确定性多视角/hogel 数据和
   逐 hogel、逐 RGB 通道曝光计划。也可以在 Inspector 填写严格的真实视图
   manifest 路径后点击 `Load Real View Manifest`。
2. 设置 `Hogel X/Y`，点击 `Expose selected RGB hogel`。该动作会通过已放置的
   SLM、物光和参考光路径执行三次独立的 M8 体全息记录。
3. 选择视角与 `Observation`，点击 `Reconstruct View to Probe`。有限孔径、
   各波长 Airy 响应和相机光谱响应会生成 Probe 上的定向重建预览。
4. 批处理时点击 `New Print Batch`，设置每次 `Slice` 的 hogel 数并反复点击
   `Run + Checkpoint`。暂停只发生在完整 RGB hogel 边界；Inspector 中可保存、
   加载并校验断点文件。
5. 设置 `Region hogels`（1--64）并点击 `Reconstruct Bounded Batch Region`，
   可对已曝光区域做有界多 hogel 重建。
6. Inspector 的 `Transparent CHIMERA Parameter Sweep` 会保留每个候选的约束、
   效率、串扰和产物大小；最佳候选可重新构建为普通可编辑 Bench。

CHIMERA 自动化结果绑定 project ID 和精确 scene revision。只要移动或修改普通
器件，数据集、曝光和重建就会变旧，必须重新生成；自动化不会在背后维护一套
与 3D Bench 不一致的隐藏光路。

## 当前物理边界

- 全局布局使用射线，局部光学面使用标量二维复场；实验室空间不会体素化。
- 薄透镜、标量/近轴传播和 Kogelnik 体光栅都有适用范围；高 NA 矢量场、偏振、
  未标定像差和真实材料历史不在当前虚拟沙箱的精度声明内。
- 内置 CHIMERA 相机响应是相对预览。可导入实测 SLM、材料和相机 LUT，但软件
  不声称复现未公开的商业 CHIMERA 打印头，也不直接控制真实硬件。
- GPU 适配只允许依据运行时能力与数值探针，不依据厂商、型号或驱动字符串。

更完整的数值假设见 [PHYSICS_ASSUMPTIONS.md](PHYSICS_ASSUMPTIONS.md)，当前已知
限制见 [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)。
