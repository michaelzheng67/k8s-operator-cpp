#pragma once

#include <cctype>
#include <string>
#include <string_view>

#include "k8sop/api/backtest_job.hpp"
#include "k8sop/api/backtest_schedule.hpp"
#include "k8sop/api/pod.hpp"

namespace k8sop::client {

template <typename T>
struct ResourceTraits;

template <typename T>
concept Resource = requires {
  ResourceTraits<T>::group;
  ResourceTraits<T>::version;
  ResourceTraits<T>::plural;
};

template <>
struct ResourceTraits<api::Pod> {
  static constexpr std::string_view group = "";
  static constexpr std::string_view version = "v1";
  static constexpr std::string_view plural = "pods";
};

template <>
struct ResourceTraits<api::BacktestJob> {
  static constexpr std::string_view group = "quant.example.com";
  static constexpr std::string_view version = "v1alpha1";
  static constexpr std::string_view plural = "backtestjobs";
};

template <>
struct ResourceTraits<api::BacktestSchedule> {
  static constexpr std::string_view group = "quant.example.com";
  static constexpr std::string_view version = "v1alpha1";
  static constexpr std::string_view plural = "backtestschedules";
};

template <Resource T>
std::string resource_path(std::string_view ns, std::string_view name = "") {
  using Traits = ResourceTraits<T>;
  std::string path = Traits::group.empty() ? "/api/" : "/apis/";
  if (!Traits::group.empty()) {
    path += Traits::group;
    path += '/';
  }
  path += Traits::version;
  path += "/namespaces/";
  path += ns;
  path += '/';
  path += Traits::plural;
  if (!name.empty()) {
    path += '/';
    path += name;
  }
  return path;
}

inline std::string url_encode(std::string_view in) {
  constexpr std::string_view kHex = "0123456789ABCDEF";
  constexpr std::string_view kUnreserved = "-._~";
  std::string out;
  for (const auto ch : in) {
    const auto c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) || kUnreserved.contains(ch)) {
      out += ch;
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0xF];
    }
  }
  return out;
}

}  // namespace k8sop::client
