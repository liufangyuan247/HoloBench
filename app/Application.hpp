#pragma once

struct SDL_GLContextState;
struct SDL_Window;

namespace holobench::app {

class Application final {
public:
    Application() = default;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(int smokeFrameLimit = 0);

private:
    bool initialize();
    void shutdown() noexcept;
    void drawWorkspace();

    SDL_Window* window_ = nullptr;
    SDL_GLContextState* glContext_ = nullptr;
    bool initialized_ = false;
    bool imguiContextCreated_ = false;
    bool imguiSdlInitialized_ = false;
    bool imguiGlInitialized_ = false;
};

} // namespace holobench::app
