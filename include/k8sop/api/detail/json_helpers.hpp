#pragma once

// Helpers for writing to_json/from_json. Include only from .cpp files: this pulls in the full
// nlohmann/json.hpp, which public headers avoid.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "k8sop/api/detail/enum_table.hpp"

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

// Throws the same exception type nlohmann uses for type mismatches, so callers only ever have to
// handle nlohmann::json::exception.
[[noreturn]] inline void throw_type_error(const nlohmann::json& j, const std::string& message) {
  throw nlohmann::json::type_error::create(302, message, &j);
}

// Reads a JSON string into an enum via its table. Throws type_error for unknown names.
template <typename E, std::size_t N>
E read_enum(const nlohmann::json& j, const EnumTable<E, N>& table, std::string_view type_name) {
  const auto name = j.get<std::string>();
  if (auto value = enum_from_string(table, name)) {
    return *value;
  }
  throw_type_error(j, std::string("unknown ") + std::string(type_name) + " \"" + name + "\"");
}

}  // namespace k8sop::api::detail
