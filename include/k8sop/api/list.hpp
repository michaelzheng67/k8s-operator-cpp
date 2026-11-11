#pragma once

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace k8sop::api {

// metadata of a list response.
struct ListMeta {
  std::string resource_version;  // "resourceVersion": where a watch should start (Milestone 3)
  std::string continue_token;    // "continue": set when the server paginated the result

  bool operator==(const ListMeta&) const = default;
};

// The body of a LIST request: {"kind": "PodList", "metadata": {...}, "items": [...]}.
template <typename T>
struct List {
  ListMeta metadata;
  std::vector<T> items;

  bool operator==(const List&) const = default;
};

void from_json(const nlohmann::json& j, ListMeta& meta);

// Defined in parse.cpp: only parse<T> ever reads a list.
template <typename T>
void from_json(const nlohmann::json& j, List<T>& list);

}  // namespace k8sop::api
