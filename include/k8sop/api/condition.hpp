#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace k8sop::api {

// status.conditions[]: the standard Kubernetes way to report state that `kubectl describe` and
// tooling understand, e.g. {type: Degraded, status: "True", reason: SharpeBelowThreshold}.
struct Condition {
  std::string type;                                 // "type"     required
  std::string status;                               // "status"   required: True | False | Unknown
  std::string reason;                               // "reason"   CamelCase machine-readable
  std::string message;                              // "message"  human-readable
  std::optional<std::string> last_transition_time;  // "lastTransitionTime", RFC 3339
  std::int64_t observed_generation = 0;             // "observedGeneration"

  bool operator==(const Condition&) const = default;
};

void to_json(nlohmann::json& j, const Condition& condition);
void from_json(const nlohmann::json& j, Condition& condition);

}  // namespace k8sop::api
