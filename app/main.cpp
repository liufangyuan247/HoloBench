#include "app/Application.hpp"

#include <charconv>
#include <cstdint>
#include <string_view>

namespace {

bool parsePositiveInt(std::string_view value, int maxVal, int& out) {
    if (value.empty()) {
        return false;
    }
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc {} || result.ptr != value.data() + value.size()) {
        return false;
    }
    if (parsed <= 0 || parsed > maxVal) {
        return false;
    }
    out = parsed;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    holobench::app::RunOptions options;
    bool seenSmoke = false;
    bool seenBenchmark = false;
    bool seenRayCount = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        if (arg == "--smoke-frames") {
            if (seenSmoke || (index + 1 >= argc)) {
                return 64;
            }
            seenSmoke = true;
            ++index;
            if (!parsePositiveInt(argv[index], 1'000'000, options.smokeFrameLimit)) {
                return 64;
            }
        } else if (arg == "--benchmark-frames") {
            if (seenBenchmark || (index + 1 >= argc)) {
                return 64;
            }
            seenBenchmark = true;
            ++index;
            if (!parsePositiveInt(argv[index], 1'000'000, options.benchmarkFrames)) {
                return 64;
            }
        } else if (arg == "--ray-count") {
            if (seenRayCount || (index + 1 >= argc)) {
                return 64;
            }
            seenRayCount = true;
            ++index;
            if (!parsePositiveInt(argv[index], 100'000, options.initialRayCount)) {
                return 64;
            }
        } else {
            return 64;
        }
    }

    if (seenSmoke && seenBenchmark) {
        return 64;
    }

    holobench::app::Application application;
    return application.run(options);
}
