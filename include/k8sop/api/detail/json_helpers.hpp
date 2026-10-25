#pragma once

// Helpers for writing to_json/from_json. Include only from .cpp files: this pulls in the full
// nlohmann/json.hpp, which public headers avoid.

#include <optional>

#include <nlohmann/json.hpp>

namespace k8sop::api::detail {

template <typename T>
void read_optional(const nlohmann::json& j, const char* key, T& out) {
  if (auto it = j.find(key); it != j.end()) {
    it->get_to(out);
  }
}

template <typename T>
void read_optional(const nlohmann::json& j, const char* key, std::optional<T>& out) {
  if (auto it = j.find(key); it != j.end()) {
    out = it->get<T>();
  }
}

// Writes `value` under `key` unless it is empty (containers, strings) or zero/false (scalars).
template <typename T>
void write_if_set(nlohmann::json& j, const char* key, const T& value) {
  if constexpr (requires { value.empty(); }) {
    if (!value.empty()) {
      j[key] = value;
    }
  } else {
    if (value != T{}) {
      j[key] = value;
    }
  }
}

template <typename T>
void write_if_set(nlohmann::json& j, const char* key, const std::optional<T>& value) {
  if (value) {
    j[key] = *value;
  }
}

}  // namespace k8sop::api::detail
