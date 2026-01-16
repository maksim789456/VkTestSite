#pragma once

#include <future>

#include <openxr/openxr.hpp>
#include <openxr/openxr_reflection.h>
#include "spdlog/spdlog.h"

#define XR_ENUM_CASE_STR(name, val) case name: return #name;

constexpr const char *xrResultToStr(XrResult e) {
  switch (e) {
    XR_LIST_ENUM_XrResult(XR_ENUM_CASE_STR);
    default: return "Unknown";
  }
}

constexpr const char *xrObjTypeToStr(XrObjectType e) {
  switch (e) {
    XR_LIST_ENUM_XrObjectType(XR_ENUM_CASE_STR);
    default: return "Unknown";
  }
}


XRAPI_ATTR XrBool32 static XRAPI_CALL OpenXRMessageCallbackFunction(
  XrDebugUtilsMessageSeverityFlagsEXT messageSeverity,
  XrDebugUtilsMessageTypeFlagsEXT messageTypes,
  const XrDebugUtilsMessengerCallbackDataEXT *callbackData,
  void *userData
) {
  const auto messageSeverityExt = static_cast<xr::DebugUtilsMessageSeverityFlagBitsEXT>(messageSeverity);
  spdlog::level::level_enum level;
  switch (messageSeverityExt) {
    case xr::DebugUtilsMessageSeverityFlagBitsEXT::Verbose:
      level = spdlog::level::debug;
      break;
    case xr::DebugUtilsMessageSeverityFlagBitsEXT::Info:
      level = spdlog::level::info;
      break;
    case xr::DebugUtilsMessageSeverityFlagBitsEXT::Warning:
      level = spdlog::level::warn;
      break;
    case xr::DebugUtilsMessageSeverityFlagBitsEXT::Error:
      level = spdlog::level::err;
      break;
    default:
      level = spdlog::level::info;
      break;
  }

  const auto messageTypeExt = static_cast<xr::DebugUtilsMessageTypeFlagBitsEXT>(messageTypes);
  std::string messageTypeStr = "";
  switch (messageTypeExt) {
    default:
    case xr::DebugUtilsMessageTypeFlagBitsEXT::General:
      messageTypeStr = "GEN";
      break;
    case xr::DebugUtilsMessageTypeFlagBitsEXT::Validation:
      messageTypeStr = "SPEC";
      break;
    case xr::DebugUtilsMessageTypeFlagBitsEXT::Performance:
      messageTypeStr = "PERF";
      break;
    case xr::DebugUtilsMessageTypeFlagBitsEXT::Conformance:
      messageTypeStr = "CONF";
      break;
  }

  std::ostringstream oss;
  oss << messageTypeStr
      << " | ID: " << (callbackData->messageId ? callbackData->messageId : "-1")
      << " | Message: " << (callbackData->message ? callbackData->message : "no_message");

  if (callbackData->sessionLabelCount > 0) {
    oss << " | SessionLabels: ";
    for (uint32_t i = 0; i < callbackData->sessionLabelCount; i++) {
      if (i > 0) oss << ", ";
      oss << callbackData->sessionLabels[i].labelName;
    }
  }

  if (callbackData->objectCount > 0) {
    oss << " | Objects: ";
    for (uint32_t i = 0; i < callbackData->objectCount; i++) {
      if (i > 0) oss << "; ";
      oss << "{" << xrObjTypeToStr(callbackData->objects[i].objectType)
          << " handle=0x" << std::hex << callbackData->objects[i].objectHandle << std::dec;
      if (callbackData->objects[i].objectName) {
        oss << " name=" << callbackData->objects[i].objectName;
      }
      oss << "}";
    }
  }

  spdlog::log(level, oss.str());

  return XR_FALSE;
}

template<typename ResultItemType, typename Dispatch>
OPENXR_HPP_INLINE xr::ResultValue<std::vector<ResultItemType> >
enumerateSwapchainImagesToVector(xr::Swapchain swapchain, Dispatch &&d) {
  std::vector<ResultItemType> images;
  uint32_t imageCountOutput = 0;
  uint32_t imageCapacityInput = 0;

  auto result = static_cast<xr::Result>(
    d.xrEnumerateSwapchainImages(swapchain, imageCapacityInput, &imageCountOutput, nullptr));
  if (!unqualifiedSuccess(result) || imageCountOutput == 0) {
    OPENXR_HPP_ASSERT(succeeded(result));

    return {result, std::move(images)};
  }
  do {
    images.resize(imageCountOutput);
    imageCapacityInput = static_cast<uint32_t>(images.size());
    result = static_cast<xr::Result>(
      d.xrEnumerateSwapchainImages(swapchain, imageCapacityInput, &imageCountOutput,
                                   reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data())));
  } while (result == xr::Result::ErrorSizeInsufficient);
  if (succeeded(result)) {
    OPENXR_HPP_ASSERT(imageCountOutput <= images.size());
    images.resize(imageCountOutput);
  } else
    images.clear();

  OPENXR_HPP_ASSERT(succeeded(result));

  return {result, std::move(images)};
}
