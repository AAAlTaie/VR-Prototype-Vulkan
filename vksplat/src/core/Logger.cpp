#include "core/Logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace core {

void initializeLogger() {
    auto logger = spdlog::stderr_color_mt("vksplat");
    logger->set_pattern("[%T.%e] [%^%l%$] %v");
    logger->set_level(spdlog::level::trace);
    spdlog::set_default_logger(std::move(logger));
}

}
