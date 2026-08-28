#include <filesystem>

#include "app/Application.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "platform/Paths.h"

namespace {

constexpr const char* kDefaultConfigPath = "config/app.toml";

void logResolvedConfig(const core::Config& config) {
    spdlog::info("window.width          = {}", config.window.width);
    spdlog::info("window.height         = {}", config.window.height);
    spdlog::info("window.title          = {}", config.window.title);
    spdlog::info("window.vsync          = {}", config.window.vsync);
    spdlog::info("renderer.validation   = {}", config.renderer.validation);
    spdlog::info("renderer.device       = {}",
                 config.renderer.preferredDevice.empty() ? "<auto>" : config.renderer.preferredDevice);
    spdlog::info("renderer.frames       = {}", config.renderer.framesInFlight);
    spdlog::info("renderer.clear_color  = [{}, {}, {}, {}]", config.renderer.clearColor[0],
                 config.renderer.clearColor[1], config.renderer.clearColor[2],
                 config.renderer.clearColor[3]);
    spdlog::info("scene.path            = {}",
                 config.scene.path.empty() ? "<unset>" : config.scene.path);
}

}

int main(int argc, char** argv) {
    core::initializeLogger();

    const auto configPath = platform::resolveResource(argc > 1 ? argv[1] : kDefaultConfigPath);
    if (!configPath) {
        spdlog::error("{}", configPath.error());
        return 1;
    }

    auto config = core::loadConfig(configPath.value());
    if (!config) {
        spdlog::error("{}", config.error());
        return 1;
    }

    spdlog::info("loaded {}", configPath.value().string());
    logResolvedConfig(config.value());

    auto application = app::Application::create(config.value());
    if (!application) {
        spdlog::error("{}", application.error());
        return 1;
    }

    auto outcome = application.take().run();
    if (!outcome) {
        spdlog::error("{}", outcome.error());
        return 1;
    }
    return 0;
}
