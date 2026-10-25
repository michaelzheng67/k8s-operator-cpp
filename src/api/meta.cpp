#include "k8sop/api/meta.hpp"

#include "k8sop/api/detail/json_helpers.hpp"

namespace k8sop::api {

using detail::read_optional;
using detail::write_if_set;

void to_json(nlohmann::json& j, const OwnerReference& ref) {
  j["apiVersion"] = ref.api_version;
  j["kind"] = ref.kind;
  j["name"] = ref.name;
  j["uid"] = ref.uid;
  if (ref.controller) {
    j["controller"] = ref.controller;
  }

  if (ref.block_owner_deletion) {
    j["blockOwnerDeletion"] = ref.block_owner_deletion;
  }
}

void from_json(const nlohmann::json& j, OwnerReference& ref) {
  j.at("apiVersion").get_to(ref.api_version);
  j.at("kind").get_to(ref.kind);
  j.at("name").get_to(ref.name);
  j.at("uid").get_to(ref.uid);

  read_optional(j, "controller", ref.controller);
  read_optional(j, "blockOwnerDeletion", ref.block_owner_deletion);
}

void to_json(nlohmann::json& j, const ObjectMeta& meta) {
  j = nlohmann::json::object();
  write_if_set(j, "name", meta.name);
  write_if_set(j, "namespace", meta.namespace_);
  write_if_set(j, "uid", meta.uid);
  write_if_set(j, "resourceVersion", meta.resource_version);
  write_if_set(j, "generation", meta.generation);
  write_if_set(j, "creationTimestamp", meta.creation_timestamp);
  write_if_set(j, "deletionTimestamp", meta.deletion_timestamp);
  write_if_set(j, "labels", meta.labels);
  write_if_set(j, "annotations", meta.annotations);
  write_if_set(j, "ownerReferences", meta.owner_references);
  write_if_set(j, "finalizers", meta.finalizers);
}

void from_json(const nlohmann::json& j, ObjectMeta& meta) {
  read_optional(j, "name", meta.name);
  read_optional(j, "namespace", meta.namespace_);
  read_optional(j, "uid", meta.uid);
  read_optional(j, "resourceVersion", meta.resource_version);
  read_optional(j, "generation", meta.generation);
  read_optional(j, "creationTimestamp", meta.creation_timestamp);
  read_optional(j, "deletionTimestamp", meta.deletion_timestamp);
  read_optional(j, "labels", meta.labels);
  read_optional(j, "annotations", meta.annotations);
  read_optional(j, "ownerReferences", meta.owner_references);
  read_optional(j, "finalizers", meta.finalizers);
}

}  // namespace k8sop::api
