#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace k8sop::api {

// Everything that can go wrong turning an HTTP body into a typed object.
struct ParseError {
  enum class Kind : std::uint8_t {
    Syntax,   // not valid JSON at all, e.g. a truncated body
    Type,     // valid JSON, wrong type somewhere: a string where a number belongs, unknown enum
    Missing,  // a required field is absent
    Other,    // any other nlohmann::json::exception
  };

  Kind kind = Kind::Other;
  std::string message;   // the exception's what(), for logs
  std::size_t byte = 0;  // Syntax only: offset into the body where parsing failed

  bool operator==(const ParseError&) const = default;
};

// The only place in the codebase where JSON exceptions are caught. Everything above this layer
// gets a value or a ParseError, never an exception.
//
// Defined in parse.cpp and explicitly instantiated there for the API types listed at the bottom
// of that file, so this header doesn't need the full nlohmann/json.hpp. Calling it with any other
// T compiles but fails to link.
template <typename T>
[[nodiscard]] std::expected<T, ParseError> parse(std::string_view body) noexcept;

// The inverse: an API object as a compact JSON string, ready to send.
template <typename T>
[[nodiscard]] std::string serialize(const T& object);

}  // namespace k8sop::api
