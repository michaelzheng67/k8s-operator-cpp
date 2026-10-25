#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "k8sop/api/meta.hpp"

namespace k8sop::api {

struct EnvVar {
  std::string name;
  std::string value;

  bool operator==(const EnvVar&) const = default;
};

struct Container {
  std::string name;
  std::string image;
  std::vector<std::string> command;
  std::vector<std::string> args;
  std::vector<EnvVar> env;

  bool operator==(const Container&) const = default;
};

struct PodSpec {
  std::vector<Container> containers;
  std::string restart_policy;

  bool operator==(const PodSpec&) const = default;
};

enum class PodPhase { Pending, Running, Succeeded, Failed, Unknown };

std::string_view to_string(PodPhase);

struct ContainerStateTerminated {
  std::int32_t exit_code{0};
  std::string reason;
  std::string message;

  bool operator==(const ContainerStateTerminated&) const = default;
};

struct ContainerStatus {
  std::string name;
  std::optional<ContainerStateTerminated> terminated;

  bool operator==(const ContainerStatus&) const = default;
};

struct PodStatus {
  PodPhase phase = PodPhase::Pending;
  std::vector<ContainerStatus> container_statuses;

  bool operator==(const PodStatus&) const = default;
};

struct Pod {
  ObjectMeta metadata;
  PodSpec spec;
  PodStatus status;

  bool operator==(const Pod&) const = default;
};

void to_json(nlohmann::json& j, const EnvVar& env);
void from_json(const nlohmann::json& j, EnvVar& env);
void to_json(nlohmann::json& j, const Container& container);
void from_json(const nlohmann::json& j, Container& container);
void to_json(nlohmann::json& j, const PodSpec& spec);
void from_json(const nlohmann::json& j, PodSpec& spec);
void to_json(nlohmann::json& j, const ContainerStateTerminated& terminated);
void from_json(const nlohmann::json& j, ContainerStateTerminated& terminated);
// JSON nests the terminated state as {"state": {"terminated": {...}}}.
void to_json(nlohmann::json& j, const ContainerStatus& status);
void from_json(const nlohmann::json& j, ContainerStatus& status);
void to_json(nlohmann::json& j, const PodStatus& status);
void from_json(const nlohmann::json& j, PodStatus& status);
// Always writes "apiVersion": "v1" and "kind": "Pod".
void to_json(nlohmann::json& j, const Pod& pod);
void from_json(const nlohmann::json& j, Pod& pod);

}  // namespace k8sop::api
