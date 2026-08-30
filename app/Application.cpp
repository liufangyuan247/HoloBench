#include "app/Application.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

#include <array>

#include "render/gl/GlDebug.hpp"

namespace holobench::app {

Application::~Application() {
    shutdown();
}

bool Application::initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    constexpr auto windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow("HoloBench — M0 Foundation", 1440, 900, windowFlags);
    if (window_ == nullptr) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        shutdown();
        return false;
    }

    glContext_ = SDL_GL_CreateContext(window_);
    if (glContext_ == nullptr || !SDL_GL_MakeCurrent(window_, glContext_)) {
        SDL_Log("OpenGL context creation failed: %s", SDL_GetError());
        shutdown();
        return false;
    }
    SDL_GL_SetSwapInterval(1);

    render::gl::installDebugCallback();
    constexpr std::array<GLenum, 4> glProperties {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION};
    constexpr std::array<const char*, 4> glPropertyNames {"vendor", "renderer", "version", "GLSL"};
    for (std::size_t index = 0; index < glProperties.size(); ++index) {
        const auto* value = reinterpret_cast<const char*>(glGetString(glProperties[index]));
        SDL_Log("OpenGL %s: %s", glPropertyNames[index], value != nullptr ? value : "unavailable");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiContextCreated_ = true;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    imguiSdlInitialized_ = ImGui_ImplSDL3_InitForOpenGL(window_, glContext_);
    imguiGlInitialized_ = imguiSdlInitialized_ && ImGui_ImplOpenGL3_Init("#version 460 core");
    if (!imguiGlInitialized_) {
        SDL_Log("Dear ImGui backend initialization failed");
        shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

void Application::shutdown() noexcept {
    if (imguiGlInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        imguiGlInitialized_ = false;
    }
    if (imguiSdlInitialized_) {
        ImGui_ImplSDL3_Shutdown();
        imguiSdlInitialized_ = false;
    }
    if (imguiContextCreated_) {
        ImGui::DestroyContext();
        imguiContextCreated_ = false;
    }
    initialized_ = false;
    if (glContext_ != nullptr) {
        SDL_GL_DestroyContext(glContext_);
        glContext_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void Application::drawWorkspace() {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    ImGui::Begin("Optical Bench");
    ImGui::TextUnformatted("M0 engineering foundation is running.");
    ImGui::Separator();
    ImGui::TextDisabled("The 3D scene and optical components arrive in M1.");
    ImGui::End();

    ImGui::Begin("Inspector");
    ImGui::TextUnformatted("No component selected");
    ImGui::End();

    ImGui::Begin("Validation");
    ImGui::BulletText("Project format: v1");
    ImGui::BulletText("Physics solver: not implemented");
    ImGui::BulletText("Results: no unvalidated optical claims");
    ImGui::End();
}

int Application::run(int smokeFrameLimit) {
    if (!initialize()) {
        return 1;
    }

    bool running = true;
    int renderedFrames = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        drawWorkspace();
        ImGui::Render();

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.035F, 0.045F, 0.060F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window_);

        ++renderedFrames;
        if (smokeFrameLimit > 0 && renderedFrames >= smokeFrameLimit) {
            running = false;
        }
    }

    return render::gl::errorCount() == 0 ? 0 : 2;
}

} // namespace holobench::app
