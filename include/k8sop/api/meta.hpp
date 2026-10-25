#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace k8sop::api {

// metadata.ownerReferences[]: links a child object (e.g. a Pod) to the object that owns it.
// Kubernetes garbage-collects children when the owner is deleted.
struct OwnerReference {
  std::string api_version;            // "apiVersion"         required
  std::string kind;                   // "kind"               required
  std::string name;                   // "name"               required
  std::string uid;                    // "uid"                required
  bool controller = false;            // "controller"         optional
  bool block_owner_deletion = false;  // "blockOwnerDeletion" optional

  bool operator==(const OwnerReference&) const = default;
};

// metadata: the fields every Kubernetes object has. Only the subset the operator uses.
struct ObjectMeta {
  std::string name;                                // "name"
  std::string namespace_;                          // "namespace"  (`namespace` is a C++ keyword)
  std::string uid;                                 // "uid"
  std::string resource_version;                    // "resourceVersion"
  std::int64_t generation = 0;                     // "generation"
  std::optional<std::string> creation_timestamp;   // "creationTimestamp", RFC 3339
  std::optional<std::string> deletion_timestamp;   // "deletionTimestamp", set once deletion starts
  std::map<std::string, std::string> labels;       // "labels"
  std::map<std::string, std::string> annotations;  // "annotations"
  std::vector<OwnerReference> owner_references;    // "ownerReferences"
  std::vector<std::string> finalizers;             // "finalizers"

  bool operator==(const ObjectMeta&) const = default;
};

// nlohmann/json finds these through argument-dependent lookup (ADL): they must live in the
// same namespace as the type they convert.
//
// Rules (match how the Kubernetes API itself behaves):
//  - from_json: every ObjectMeta field may be missing; a missing field leaves the default.
//    OwnerReference's four string fields are required; a missing one must throw.
//    Unknown keys (e.g. "managedFields") are ignored. A key with the wrong type must throw.
//  - to_json: omit fields that are empty / zero / false / nullopt ("omitempty").
void to_json(nlohmann::json& j, const OwnerReference& ref);
void from_json(const nlohmann::json& j, OwnerReference& ref);
void to_json(nlohmann::json& j, const ObjectMeta& meta);
void from_json(const nlohmann::json& j, ObjectMeta& meta);

}  // namespace k8sop::api
