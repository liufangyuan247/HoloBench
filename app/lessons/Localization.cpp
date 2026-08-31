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
    return LocalizationCatalog(std::move(messages));
}

} // namespace holobench::app::lessons
