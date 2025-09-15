#include "VkTestSiteApp.h"
#include "TracyLogSink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

int main() {
  const auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  const auto tracy_sink = std::make_shared<tracy_sink_mt>();

  std::vector<spdlog::sink_ptr> sinks{console_sink, tracy_sink};

  const auto logger = std::make_shared<spdlog::logger>("def_logger", sinks.begin(), sinks.end());
  spdlog::set_default_logger(logger);

  VkTestSiteApp app{};

  try {
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
