#pragma once

#ifndef TRACYLOGSINK_H
#define TRACYLOGSINK_H

#include <tracy/Tracy.hpp>
#include "spdlog/sinks/base_sink.h"

template<typename Mutex>
class tracy_sink final : public spdlog::sinks::base_sink<Mutex> {
protected:
  void sink_it_(const spdlog::details::log_msg &msg) override {
    spdlog::memory_buf_t formatted;
    this->formatter_->format(msg, formatted);

    auto str = fmt::to_string(formatted);
    auto color = level_to_color(msg.level);
    TracyMessageC(str.c_str(), str.size(), color);
  }

  void flush_() override {
  }
private:
  static uint32_t level_to_color(const spdlog::level::level_enum lvl)
  {
    switch (lvl)
    {
      case spdlog::level::trace:    return 0xFFFFFFFF; // white
      case spdlog::level::debug:    return 0xFF00FFFF; // cyan
      case spdlog::level::info:     return 0xFF00FF00; // green
      case spdlog::level::warn:     return 0xFFFFFF00; // yellow
      case spdlog::level::err:      return 0xFFFF0000; // red
      case spdlog::level::critical: return 0xFFFF4444; // bright red
      default:                      return 0xFFFFFFFF; // fallback white
    }
  }
};

#include "spdlog/details/null_mutex.h"
#include <mutex>

using tracy_sink_mt = tracy_sink<std::mutex>;
using tracy_sink_st = tracy_sink<spdlog::details::null_mutex>;

#endif //TRACYLOGSINK_H
