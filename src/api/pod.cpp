#include "k8sop/api/pod.hpp"

#include <array>
#include <utility>

#include "k8sop/api/detail/enum_table.hpp"
#include "k8sop/api/detail/json_helpers.hpp"

namespace k8sop::api {

using detail::read_optional;
using detail::write_if_set;
using nlohmann::json;

namespace {

constexpr detail::EnumTable<PodPhase, 5> kPodPhases{{{PodPhase::Pending, "Pending"},
                                                     {PodPhase::Running, "Running"},
                                                     {PodPhase::Succeeded, "Succeeded"},
                                                     {PodPhase::Failed, "Failed"},
                                                     {PodPhase::Unknown, "Unknown"}}};

PodPhase parse_pod_phase(std::string_view s) {
  return detail::enum_from_string(kPodPhases, s).value_or(PodPhase::Unknown);
}

}  // namespace

std::string_view to_string(PodPhase phase) { return detail::enum_to_string(kPodPhases, phase); }

void to_json(json& j, const EnvVar& env) {
  j = json::object();
  write_if_set(j, "name", env.name);
  write_if_set(j, "value", env.value);
}

void from_json(const json& j, EnvVar& env) {
  j.at("name").get_to(env.name);
  read_optional(j, "value", env.value);
}

void to_json(json& j, const Container& container) {
  j = json::object();
  write_if_set(j, "name", container.name);
  write_if_set(j, "image", container.image);
  write_if_set(j, "command", container.command);
  write_if_set(j, "args", container.args);
  write_if_set(j, "env", container.env);
}

void from_json(const json& j, Container& container) {
  read_optional(j, "name", container.name);
  read_optional(j, "image", container.image);
  read_optional(j, "command", container.command);
  read_optional(j, "args", container.args);
  read_optional(j, "env", container.env);
}

void to_json(json& j, const PodSpec& spec) {
  j = json::object();
  write_if_set(j, "containers", spec.containers);
  write_if_set(j, "restartPolicy", spec.restart_policy);
}

void from_json(const json& j, PodSpec& spec) {
  read_optional(j, "containers", spec.containers);
  read_optional(j, "restartPolicy", spec.restart_policy);
}

void to_json(json& j, const ContainerStateTerminated& terminated) {
  j = {{"exitCode", terminated.exit_code}};  // always written: 0 is a meaningful exit code
  write_if_set(j, "reason", terminated.reason);
  write_if_set(j, "message", terminated.message);
}

void from_json(const json& j, ContainerStateTerminated& terminated) {
  read_optional(j, "exitCode", terminated.exit_code);
  read_optional(j, "reason", terminated.reason);
  read_optional(j, "message", terminated.message);
}

// TODO(you): "name" is a plain field; "terminated" lives at JSON path state.terminated.
void to_json(json& j, const ContainerStatus& status) {
  j = {{"name", status.name}};
  if (status.terminated) {
    j["state"]["terminated"] = *status.terminated;
  }
}

void from_json(const json& j, ContainerStatus& status) {
  read_optional(j, "name", status.name);
  if (auto state = j.find("state"); state != j.end()) {
    if (auto term = state->find("terminated"); term != state->end()) {
      status.terminated = term->get<ContainerStateTerminated>();
    }
  }
}

void to_json(json& j, const PodStatus& status) {
  j = {{"phase", to_string(status.phase)}};
  write_if_set(j, "containerStatuses", status.container_statuses);
}

void from_json(const json& j, PodStatus& status) {
  if (auto it = j.find("phase"); it != j.end()) {
    status.phase = parse_pod_phase(it->get<std::string>());
  }
  read_optional(j, "containerStatuses", status.container_statuses);
}

void to_json(json& j, const Pod& pod) {
  j = {{"apiVersion", "v1"}, {"kind", "Pod"}};
  write_if_set(j, "metadata", pod.metadata);
  write_if_set(j, "spec", pod.spec);
  write_if_set(j, "status", pod.status);  // skipped when default, so creates never send status
}

void from_json(const json& j, Pod& pod) {
  read_optional(j, "metadata", pod.metadata);
  read_optional(j, "spec", pod.spec);
  read_optional(j, "status", pod.status);
}

}  // namespace k8sop::api
