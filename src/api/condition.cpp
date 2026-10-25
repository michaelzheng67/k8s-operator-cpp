#include "k8sop/api/condition.hpp"

#include "k8sop/api/detail/json_helpers.hpp"

namespace k8sop::api {

using detail::read_optional;
using detail::write_if_set;
using nlohmann::json;

void to_json(json& j, const Condition& condition) {
  j = {{"type", condition.type}, {"status", condition.status}};
  write_if_set(j, "reason", condition.reason);
  write_if_set(j, "message", condition.message);
  write_if_set(j, "lastTransitionTime", condition.last_transition_time);
  write_if_set(j, "observedGeneration", condition.observed_generation);
}

void from_json(const json& j, Condition& condition) {
  j.at("type").get_to(condition.type);
  j.at("status").get_to(condition.status);
  read_optional(j, "reason", condition.reason);
  read_optional(j, "message", condition.message);
  read_optional(j, "lastTransitionTime", condition.last_transition_time);
  read_optional(j, "observedGeneration", condition.observed_generation);
}

}  // namespace k8sop::api
