# 展示间、光路布局与二维透镜设计

本说明包含 2026-09-05 的单光束 Denisyuk 和 CHIMERA hogel 重建更新。推荐运行优化构建：

```powershell
cmake --preset app-ci
cmake --build --preset app-ci
./out/build/app-ci/HoloBench.exe
```

## 单光束 Denisyuk

1. 在 Bench 预设中选择 **Single-beam Denisyuk**，得到一个激光源、可透射干板、需要照明的物体和探针。
2. 在共享 3D Bench 中调整光路，让光穿过干板并击中物体正面。可放入 Screen 挡住照明，验证返回物光消失。
3. 选中干板，点击原有 **Record** 操作，再打开 **Hologram showroom**。示例使用 2 mm 干板和小物体，便于进行有限采样的波场计算。
4. 初始对焦参考记录时的物体距离；也可以在 **Observation settings** 中调整。

自行搭建时，在物体 Inspector 勾选 **Requires placed illumination**，并将干板的 **Recording transmission** 设为 0 到 1 之间的正数。旧 **Reflection / Denisyuk** 预设保留独立物光源近似，兼容已有实验。

目前以路由的中心光线判断照明是否成立，并继承波长、相干标识、光程和名义反射功率；物体表面仍使用标量分层漫反射模型，没有逐表面的照明分布、部分遮挡、多次散射或真实乳剂处理过程。

## CHIMERA 单个 hogel

1. 创建 **CHIMERA Bench**，展开 **CHIMERA controls**。
2. **Hogel size / grid** 设置毫米单位的间距/记录窗口和 X/Y 数量，点击 **Apply hogel geometry**。界面显示干板尺寸，其他元件的位置和调整会保留。
3. 点击 **Generate Dataset + Exposure Plan** 生成测试数据；自己的视图使用 **Load Real View Manifest**。
4. 选择 **Hogel X / Y**，保持 **Retain showroom fields** 勾选，点击 **Expose selected RGB hogel**。曝光在后台计算；期间修改 Bench 会使结果被丢弃，需重新曝光。**Cancel exposure** 在当前场计算结束后取消，不会发布不完整的 hogel。
5. 完成后点击 **Open selected hogel in showroom**，或选中 CHIMERA 干板后点击顶部 **Hologram showroom**。
6. 展示间可切换三个已记录波长。**Save recording** 会把所有通道一起存入 `.holo.json`，重开时不需要原始视图或 Bench。

默认 hogel 是 1 mm。尺寸定义曝光步距和方形记录窗口，不等于已校准的实际聚焦光斑。尺寸或夹角过大、需要超过每轴 2048 点时，会拒绝重建级曝光并说明原因。中心载波采样检查不能替代完整光场收敛验证。

保存 Bench 会同时保留当前 CHIMERA Recipe，因此 hogel 尺寸和网格可恢复。也可用 **Save current Recipe JSON** 单独导出。关闭 **Retain showroom fields** 得到的是较快的诊断预览；它和不带复场的旧检查点需要重新曝光才能进入展示间。

单个 hogel 包含局部角度信息，重建可能表现为衍射亮斑或角度响应，不能等同于完整多 hogel 物体图像。当前生成光路仍使用单片理想傅里叶透镜，不能代表真实 CHIMERA 打印机的所有细节。

## 查看已记录干板

1. 在共享 Bench 上完成单色反射记录，保持该记录有效并选中干板。
2. 点击工作台上方 **Hologram showroom**。程序将已经保留的物光/参考光复场固化为独立记录，再进入黑背景展示间。
3. 左键拖动旋转干板；灯与观察者默认固定。右键拖动移动观察者，滚轮调整距离。
4. **Observation settings** 中调整焦点、瞳孔、波长、精确姿态和显示曝光。显示尺度在首次非零图像后锁定，灯灭或布拉格失配时不会自动提亮。
5. **Recorded plate file → Save recording** 保存 `.holo.json`。之后可直接 Open，无需原 Bench 或原始视图文件。**Back to Bench** 返回实验。

RGB 反射干板会保留全部通道，可在展示间逐一选择。当前画面是单色光的灰度强度显示，尚未合成白光彩色图像。没有可用记录时仍可进入展示间打开文件，旧的单通道文件仍可读取。

**Two-depth reference** 是明确标注的解析校验样片：两个相干点位于板后 40/80 mm，使用同一重建与观察求解器。它用于试验焦点和视差，不替换用户的记录，也不代表实测材料。它的衍射光斑和干涉结构是有限孔径计算结果。

### 结果范围

- 展示的是实际采样窗口，轮廓不代表未采样的整张大干板。窗口太小可能只保留有限视域或无法解析原物体。
- 干板表面纹理与眼睛看到的像不同：当前观察图像经过板面出射场、瞳孔和聚焦成像计算。
- 使用标量、等效对称 TE 体光栅及近轴相机。角度、位移或载波超出采样范围时给出说明，并清除旧图像；不会继续显示原姿态的答案。
- “Boundary energy” 表示有限窗口仍需做更大记录窗口的收敛检查。第二阶段加大瞳孔计算窗口，不能恢复记录时丢失的数据。
- 尚不支持通过 CHIMERA 精简批处理检查点恢复已丢弃的复场、连续白光、多 hogel 全局相干重建或任意角度的真实材料响应。

## 改善光路搭建

工作台顶栏的 **Add components**、**CHIMERA controls** 和 **Analysis tools** 可按需展开，默认减少对视口的占用。**2D lens design** 单独打开透镜设计窗口。

新生成的 CHIMERA Bench 光轴在桌面上方 100 mm，支架与光学坐标关联。所有光学元件一起平移，光学距离不变；旧项目不会在加载时被移动。

**Check layout** 列出相交的已安装底座。它使用有向实体包络，并与显示底座共用尺寸；结果不代表外壳、通光孔、全行程或完整光束包络已全部检查。生成器仍使用一片理想傅里叶透镜，已明确提示这不是双透镜 4f 中继。

## 设计二维透镜与透镜组

1. 点击 **2D lens design**，进入 **2D Lens Design** 页。
2. 剖面横轴为 Z、纵轴为 Y。点选表面曲线，横向拖动改变边缘矢高及曲率；Shift 拖动改变前一段厚度/空气间隔，并一起移动下游表面。
3. 可直接输入边缘矢高、口径和间隔，单位为毫米。**Append N-BK7 lens / 10 mm air gap** 添加镜片，随后调整组间距；可撤去最后一个镜片。
4. **Prescription Editor** 中继续编辑圆锥常数、偶次非球面项、玻璃材料以及高级姿态。二维直接编辑要求共轴，不能将偏心或倾斜处方误当作共轴剖面。
5. **Refresh Real-Lens Analysis** 用这份处方重新追迹，查看光线、光斑及色差。输入被拒绝时原处方保持不变；未刷新结果仍标注为旧分析。
6. 修改后在 Prescription Editor 指定新的不可变 ID，Save JSON，再 Load JSON。回到 Bench，选中 **Real Lens Assembly**，通过现有已验证处方资产入口绑定该文件。项目仍保存处方引用和 SHA-256 校验。

剖面曲线直接来自求解器的曲面方程；它不是独立的装饰图。表面间隙检查采用 257 个径向位置，不能替代任意高阶非球面的连续干涉证明或加工公差检查。当前三维透镜组外壳仍是通用显示外壳。

## 本地验证入口

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./out/build/app-ci/HoloBench.exe --showroom-smoke
./out/build/app-ci/HoloBench.exe --gl-smoke
```

专用展示间检查使用真正的放置干板记录，驱动鼠标旋转和返回；还展示解析校验样片及二维透镜剖面。回读图写在当前目录的 `out/showroom-recorded-smoke.bmp`、`out/showroom-smoke.bmp` 和 `out/lens-profile-smoke.bmp`，属于本地验证产物，不提交到仓库。
