#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

namespace k8sop::api::detail {

// A compile-time enum <-> name mapping, written once per enum and searched in both directions:
//
//   constexpr EnumTable<PodPhase, 5> kPodPhases{{{PodPhase::Pending, "Pending"}, ...}};
template <typename E, std::size_t N>
using EnumTable = std::array<std::pair<E, std::string_view>, N>;

template <typename E, std::size_t N>
constexpr std::string_view enum_to_string(const EnumTable<E, N>& table, E value) noexcept {
  for (const auto& [v, n] : table) {
    if (v == value) {
      return n;
    }
  }
  return {};
}

template <typename E, std::size_t N>
constexpr std::optional<E> enum_from_string(const EnumTable<E, N>& table,
                                            std::string_view name) noexcept {
  for (const auto& [v, n] : table) {
    if (n == name) {
      return v;
    }
  }
  return std::nullopt;
}

}  // namespace k8sop::api::detail
