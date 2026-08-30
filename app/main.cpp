#include "app/Application.hpp"

#include <charconv>
#include <string_view>

int main(int argc, char** argv) {
    int smokeFrameLimit = 0;
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) == "--smoke-frames") {
            const std::string_view value(argv[index + 1]);
            const auto result = std::from_chars(value.data(), value.data() + value.size(), smokeFrameLimit);
            if (result.ec != std::errc {} || smokeFrameLimit <= 0) {
                return 64;
            }
            ++index;
        }
    }

    holobench::app::Application application;
    return application.run(smokeFrameLimit);
}
