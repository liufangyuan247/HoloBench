#include "app/lessons/Localization.hpp"

#include <stdexcept>
#include <utility>

#include "app/lessons/LessonCatalog.hpp"

namespace holobench::app::lessons {
namespace {

void addCourse(
    std::vector<LocalizedMessage>& messages,
    std::string id,
    std::string englishTitle,
    std::string englishObjective,
    std::string chineseTitle,
    std::string chineseObjective) {
    const std::string prefix = "lesson." + id;
    messages.push_back({LessonLocale::English, prefix + ".title", std::move(englishTitle)});
    messages.push_back({LessonLocale::English, prefix + ".objective", std::move(englishObjective)});
    messages.push_back({LessonLocale::SimplifiedChinese, prefix + ".title", std::move(chineseTitle)});
    messages.push_back({LessonLocale::SimplifiedChinese, prefix + ".objective", std::move(chineseObjective)});
}

void addStep(
    std::vector<LocalizedMessage>& messages,
    std::string lessonId,
    std::string stepId,
    std::string englishTitle,
    std::string englishInstruction,
    std::string englishContext,
    std::string chineseTitle,
    std::string chineseInstruction,
    std::string chineseContext) {
    const std::string prefix = "lesson." + lessonId + ".step." + stepId;
    messages.push_back({LessonLocale::English, prefix + ".title", std::move(englishTitle)});
    messages.push_back({LessonLocale::English, prefix + ".instruction", std::move(englishInstruction)});
    messages.push_back({LessonLocale::English, prefix + ".context", std::move(englishContext)});
    messages.push_back({LessonLocale::SimplifiedChinese, prefix + ".title", std::move(chineseTitle)});
    messages.push_back({LessonLocale::SimplifiedChinese, prefix + ".instruction", std::move(chineseInstruction)});
    messages.push_back({LessonLocale::SimplifiedChinese, prefix + ".context", std::move(chineseContext)});
}

} // namespace

LocalizationCatalog::LocalizationCatalog(std::vector<LocalizedMessage> messages) {
    for (auto& message : messages) {
        if (!isStableLessonKey(message.key) || message.text.empty()) {
            throw std::invalid_argument("localized messages require a stable key and non-empty text");
        }
        const auto mapKey = std::make_pair(message.locale, std::move(message.key));
        if (!messages_.emplace(mapKey, std::move(message.text)).second) {
            throw std::invalid_argument("duplicate localized message key");
        }
    }
}

bool LocalizationCatalog::contains(
    LessonLocale locale,
    std::string_view key) const noexcept {
    return messages_.contains({locale, std::string(key)});
}

const std::string& LocalizationCatalog::text(
    LessonLocale locale,
    std::string_view key) const {
    const auto found = messages_.find({locale, std::string(key)});
    if (found != messages_.end()) {
        return found->second;
    }
    const auto fallback = messages_.find({LessonLocale::English, std::string(key)});
    if (fallback != messages_.end()) {
        return fallback->second;
    }
    throw std::invalid_argument("missing localized lesson message");
}

std::vector<std::string_view> LocalizationCatalog::messages(
    LessonLocale locale) const {
    std::vector<std::string_view> values;
    for (const auto& [key, value] : messages_) {
        if (key.first == locale) {
            values.emplace_back(value);
        }
    }
    return values;
}

std::string_view lessonLocaleCode(LessonLocale locale) noexcept {
    switch (locale) {
    case LessonLocale::English:
        return "en";
    case LessonLocale::SimplifiedChinese:
        return "zh-Hans";
    }
    return "en";
}

LocalizationCatalog makeDefaultLessonLocalization() {
    std::vector<LocalizedMessage> messages;
    messages.reserve(128U);
    addCourse(messages, "reflection_refraction",
        "Reflection / Refraction",
        "Change incidence angle and refractive index, then relate the outgoing rays to reflection and Snell's laws.",
        "反射与折射",
        "改变入射角和折射率，并把出射光线与反射定律、斯涅尔定律联系起来。");
    addCourse(messages, "thin_lens",
        "Thin Lens",
        "Move a screen to the image plane predicted by the same paraxial thin-lens solver used in Lab mode.",
        "薄透镜",
        "移动屏幕到由实验室模式同一近轴薄透镜求解器预测的像平面。");
    addCourse(messages, "real_virtual_images", "Real / Virtual Images",
        "Compare converging real images with divergent rays and their virtual backward extensions.",
        "实像与虚像", "比较会聚实像、发散光线及其虚拟反向延长线。");
    addCourse(messages, "diffraction", "Diffraction",
        "Relate aperture dimensions to the measured far-field diffraction pattern.",
        "衍射", "把孔径尺寸与测得的远场衍射图样联系起来。");
    addCourse(messages, "fourier_plane", "Fourier Plane",
        "Use a 4-f relay to identify spatial-frequency content in the Fourier plane.",
        "傅里叶面", "用 4-f 系统识别傅里叶面中的空间频率内容。");
    addCourse(messages, "spatial_filtering", "Spatial Filtering",
        "Filter selected spatial frequencies and explain the resulting image change.",
        "空间滤波", "筛选指定空间频率并解释图像变化。");
    addCourse(messages, "na_psf", "NA / PSF",
        "Change numerical aperture and connect it to point-spread width and resolution.",
        "数值孔径与点扩散函数", "改变数值孔径，并把它与点扩散宽度和分辨率联系起来。");
    addCourse(messages, "coherence_interference", "Coherence / Interference",
        "Vary optical path difference and observe its effect on fringe visibility.",
        "相干与干涉", "改变光程差并观察条纹可见度的变化。");
    addCourse(messages, "holography", "Holography",
        "Record and replay a thin hologram while keeping zero and conjugate orders visible.",
        "全息术", "记录并重放薄全息图，同时保留零级和共轭级次的可见性。");
    addCourse(messages, "h1_h2_advanced", "H1/H2 Advanced",
        "Transfer a real image from H1 to H2 and inspect signed transplane placement.",
        "H1/H2 进阶", "把 H1 的实像转移到 H2，并检查带符号的跨平面位置。");

    addStep(messages, "reflection_refraction", "inspect_interface",
        "Inspect the interface", "Load the air-to-glass interface and identify its surface normal.",
        "Angles are measured from the normal, not from the surface.",
        "观察界面", "载入空气到玻璃界面，并识别其表面法线。",
        "角度从法线量起，而不是从表面量起。");
    addStep(messages, "reflection_refraction", "change_incidence",
        "Change incidence", "Move the incidence-angle control by at least 5 degrees.",
        "The reflected angle follows the incident angle; the transmitted angle also depends on both refractive indices.",
        "改变入射角", "把入射角控制量至少改变 5 度。",
        "反射角跟随入射角；透射角还取决于两侧折射率。");
    addStep(messages, "reflection_refraction", "observe_snell",
        "Verify the laws", "Compare the measured angles and confirm the observation while the ray is refracted.",
        "This scalar ray model preserves power and does not yet split Fresnel reflection/transmission amplitudes.",
        "验证定律", "比较测得角度，并在光线发生折射时确认观察结果。",
        "该标量光线模型保持功率，尚未按菲涅耳系数分离反射和透射振幅。");
    addStep(messages, "thin_lens", "place_lens",
        "Load the bench", "Load the thin-lens template with the screen deliberately away from focus.",
        "The model is ideal, zero-thickness, monochromatic, and paraxial.",
        "载入光具座", "载入薄透镜模板；屏幕会被故意放在离焦位置。",
        "该模型为理想、零厚度、单色、近轴模型。");
    addStep(messages, "thin_lens", "move_screen",
        "Move the screen", "Drag the orange screen handle by at least 5 mm along the optical axis.",
        "Moving the screen changes observation position, not the lens prediction itself.",
        "移动屏幕", "沿光轴拖动橙色屏幕手柄至少 5 毫米。",
        "移动屏幕改变的是观察位置，而不是透镜本身的成像预测。");
    addStep(messages, "thin_lens", "compare_focus",
        "Find the image plane", "Move the screen to within 1 mm of the predicted real-image plane.",
        "The displayed target comes from 1/f = 1/u + 1/v using the shared Lab solver.",
        "找到像平面", "把屏幕移动到预测实像平面 1 毫米以内。",
        "显示的目标位置来自共用实验室求解器中的 1/f = 1/u + 1/v。");
    addStep(messages, "real_virtual_images", "form_real_image",
        "Inspect a real image", "Load the converging-lens scene and identify the real image behind the lens.",
        "A real image is where the transmitted rays physically converge; the screen can intercept it.",
        "观察实像", "载入会聚透镜场景，并识别透镜后方的实像。",
        "实像位于透射光线实际会聚的位置，可以被屏幕承接。");
    addStep(messages, "real_virtual_images", "cross_focal_plane",
        "Cross the focal plane", "In Inspector, reduce Object Distance u below the positive focal length f.",
        "The shared thin-lens solver changes from positive image distance to a virtual image on the object side.",
        "跨过焦平面", "在检查器中把物距 u 减小到正焦距 f 以下。",
        "共用薄透镜求解器会从正像距切换到物方虚像。");
    addStep(messages, "real_virtual_images", "classify_image",
        "Classify the image", "Use the ray direction and signed image distance to classify the current image.",
        "Dashed backward extensions are a visualization of where divergent rays appear to originate, not physical reverse propagation.",
        "判断像的类型", "根据光线方向和带符号像距判断当前像的类型。",
        "虚线反向延长线表示发散光线看似来自何处，并不是实际的反向传播。");
    addStep(messages, "diffraction", "select_aperture",
        "Load a slit aperture", "Load the centred rectangular-aperture wave experiment and inspect its detector pattern.",
        "The detector uses the shared scalar, monochromatic, coherent angular-spectrum solver with periodic FFT boundaries.",
        "载入狭缝孔径", "载入居中的矩形孔径波动实验，并观察探测器图样。",
        "探测器使用共用的标量、单色、相干角谱求解器，并采用周期 FFT 边界。");
    addStep(messages, "diffraction", "change_width",
        "Narrow the aperture", "In Wave Detector, reduce Rectangular Half width by at least 25%, then Apply & Recompute.",
        "A narrower aperture admits less transverse extent and spreads the propagated pattern more strongly.",
        "缩窄孔径", "在波动探测器中把矩形半宽至少减小 25%，然后点击应用并重新计算。",
        "更窄的孔径限制了横向范围，使传播图样扩展得更明显。");
    addStep(messages, "diffraction", "compare_pattern",
        "Compare the pattern", "Confirm after the measured horizontal half-maximum width grows by at least 10%.",
        "This finite sampled result is evidence from the propagated field, not a claim of an exact Fraunhofer limit; sampling and window warnings still apply.",
        "比较图样", "当测得的水平方向半高全宽至少增大 10% 后确认。",
        "这是有限采样传播场给出的证据，并不宣称处于精确夫琅禾费极限；采样和窗口警告仍然有效。");
    addStep(messages, "fourier_plane", "load_4f_template",
        "Load the 4-f source", "Load the double-slit field into the shared Wave Detector and Sampling Debugger.",
        "The ideal scalar 4-f relay uses physical focal-plane coordinates and the same centred FFT conventions as Lab.",
        "载入 4-f 光源", "把双缝场载入共用的波动探测器和采样调试器。",
        "理想标量 4-f 中继使用物理焦平面坐标，并与实验室采用相同的居中 FFT 约定。");
    addStep(messages, "fourier_plane", "place_probe",
        "Move the plane probe", "Set a non-zero probe offset and refresh the Sampling Debugger.",
        "The probe follows the same angular-spectrum field; it does not create a second propagation model.",
        "移动平面探针", "设置非零探针偏移量，然后刷新采样调试器。",
        "探针沿用同一个角谱场，不会创建第二套传播模型。");
    addStep(messages, "fourier_plane", "identify_spectrum",
        "Identify the spectrum", "Choose which 4-f plane maps position to spatial frequency.",
        "The Fourier-plane centre is DC; increasing radius represents finer spatial frequency, subject to finite sampling and periodic boundaries.",
        "识别频谱", "选择 4-f 系统中把位置映射为空间频率的平面。",
        "傅里叶平面中心是直流分量，半径越大表示空间频率越高；结果仍受有限采样和周期边界约束。");
    addStep(messages, "spatial_filtering", "inspect_spectrum",
        "Inspect the unfiltered image", "Record the pass-all 4-f image and its measured detail metric.",
        "Every displayed log-intensity plane is peak-normalized independently; use numerical transmission for power comparisons.",
        "观察未滤波图像", "记录全通 4-f 图像及其测得的细节指标。",
        "各对数强度图会分别按峰值归一化；比较功率时应使用数值透射率。");
    addStep(messages, "spatial_filtering", "apply_filter",
        "Apply a low-pass filter", "Select the circular low-pass filter and refresh the Sampling Debugger.",
        "Blocking outer Fourier-plane samples removes fine spatial detail in this coherent scalar model.",
        "应用低通滤波器", "选择圆形低通滤波器，然后刷新采样调试器。",
        "在这个相干标量模型中，阻挡傅里叶平面外侧采样会去除精细空间细节。");
    addStep(messages, "spatial_filtering", "explain_image",
        "Explain the image", "Classify the measured image change after the low-pass filter.",
        "A lower normalized gradient metric is evidence of smoothing; it is distinct from display brightness normalization.",
        "解释图像变化", "判断低通滤波后测得的图像变化。",
        "较低的归一化梯度指标说明图像变平滑，这与显示亮度归一化不同。");
    addStep(messages, "na_psf", "inspect_aperture",
        "Inspect the pupil", "Record the circular-pupil paraxial NA and Airy first-dark radius.",
        "The shared PSF is an ideal scalar paraxial circular-pupil result; the displayed MTF is incoherent intensity MTF.",
        "观察光瞳", "记录圆形光瞳的近轴 NA 和艾里斑第一暗环半径。",
        "共用 PSF 是理想标量近轴圆形光瞳结果；显示的 MTF 是非相干强度 MTF。");
    addStep(messages, "na_psf", "change_na",
        "Increase NA", "Increase the pupil radius by at least 25% and refresh the Sampling Debugger.",
        "At fixed wavelength and focal length, the paraxial NA rises with pupil radius.",
        "提高 NA", "把光瞳半径至少增大 25%，然后刷新采样调试器。",
        "在波长和焦距固定时，近轴 NA 随光瞳半径增大而提高。");
    addStep(messages, "na_psf", "compare_psf",
        "Compare the PSF", "Classify the measured first-dark-radius change after increasing NA.",
        "The ideal Airy first-dark radius scales approximately as 1.22 lambda f / D inside the stated scalar paraxial model.",
        "比较 PSF", "判断提高 NA 后测得的第一暗环半径变化。",
        "在所声明的标量近轴模型内，理想艾里斑第一暗环半径近似按 1.22 lambda f / D 缩放。");
    addStep(messages, "coherence_interference", "overlap_beams",
        "Overlap object and reference", "Load the shared SLM interference experiment at zero optical path difference.",
        "The result uses scalar time-averaged mutual coherence and does not model polarization unless the separate LCD teaching approximation is selected.",
        "叠加物光与参考光", "载入光程差为零的共用 SLM 干涉实验。",
        "结果采用标量时间平均互相干模型；除非另选 LCD 教学近似，否则不建模偏振。");
    addStep(messages, "coherence_interference", "change_path_difference",
        "Change path difference", "Increase optical path difference to at least the stated 1/e coherence length, then Apply.",
        "For the Gaussian envelope, the magnitude of the complex degree of coherence falls with path difference.",
        "改变光程差", "把光程差增大到至少给定的 1/e 相干长度，然后应用。",
        "对于高斯包络，复相干度的模会随光程差增加而下降。");
    addStep(messages, "coherence_interference", "compare_visibility",
        "Compare visibility", "Classify the measured fringe-visibility change.",
        "Visibility is measured from the shared interference extrema; reduced coherence suppresses only the cross term, not the individual beam intensities.",
        "比较可见度", "判断测得的条纹可见度变化。",
        "可见度由共用干涉结果的极值测得；相干性下降只抑制交叉项，不改变两束光各自的强度。");
    addStep(messages, "holography", "record_hologram",
        "Record H1", "Apply the packaged thin-hologram experiment and inspect the H1 exposure.",
        "The shared Lab records object and coherent reference intensity in an unclipped linear thin-amplitude response.",
        "记录 H1", "应用随包提供的薄全息实验，并观察 H1 曝光。",
        "共用实验室把物光与相干参考光的强度记录在线性、未截断的薄振幅响应中。");
    addStep(messages, "holography", "replay_hologram",
        "Replay the real image", "Select the H1 isolated real-image plane in Holography Lab.",
        "The isolated order is an explicitly labelled analytic decomposition used for inspection; physical full replay retains all orders.",
        "重放实像", "在全息实验室中选择 H1 分离实像平面。",
        "分离级次是用于观察并明确标注的解析分解；物理完整重放仍保留所有级次。");
    addStep(messages, "holography", "identify_orders",
        "Identify replay orders", "Classify which orders remain in the physical full replay.",
        "A linear thin hologram produces a zero order, the desired image-bearing order, and a conjugate/twin order; sampling diagnostics report their carrier placement.",
        "识别重放级次", "判断物理完整重放中保留了哪些级次。",
        "线性薄全息图会产生零级、承载目标像的级次以及共轭（孪生）级；采样诊断会报告其载频位置。");
    addStep(messages, "h1_h2_advanced", "record_h1",
        "Record the H1 image", "Apply the packaged H1/H2 transfer and inspect the shared H1 result.",
        "H1 forms a real image 10 mm after its plate using the same RGB scalar reconstruction used by Lab.",
        "记录 H1 像", "应用随包提供的 H1/H2 转移实验，并观察共用 H1 结果。",
        "H1 使用与实验室相同的 RGB 标量重建，在其记录板后 10 毫米形成实像。");
    addStep(messages, "h1_h2_advanced", "position_h2",
        "Position H2", "Move H2 from 8 mm to the H1 real-image plane at 10 mm, then Apply.",
        "Only H2 axial position changes; the signed image distance is z(H1 image) minus z(H2).",
        "放置 H2", "把 H2 从 8 毫米移动到 10 毫米处的 H1 实像平面，然后应用。",
        "只改变 H2 轴向位置；带符号像距等于 z(H1 像) 减去 z(H2)。");
    addStep(messages, "h1_h2_advanced", "observe_transplane",
        "Confirm transplane placement", "Classify the H1 image when its signed distance from H2 is within the stated tolerance of zero.",
        "Transplane is an explicit zero-distance case, distinct from an image on either the negative or positive side of H2.",
        "确认跨平面位置", "当 H1 像相对 H2 的带符号距离在给定零值容差内时判断其位置。",
        "跨平面是明确的零距离情况，不同于位于 H2 负侧或正侧的像。");
    return LocalizationCatalog(std::move(messages));
}

} // namespace holobench::app::lessons
