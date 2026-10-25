#include "k8sop/api/meta.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace k8sop::api {
namespace {

using nlohmann::json;

// Shaped like the metadata of a real Pod from `kubectl get pod -o json`, including fields we
// don't model (managedFields, generateName) that must be ignored.
constexpr const char* kPodMetadata = R"json({
  "name": "momentum-sweep-t0-s1-a0",
  "generateName": "momentum-sweep-",
  "namespace": "backtests",
  "uid": "6f1c2a0e-8a1b-4c55-9d1e-2b7f3c9a4e10",
  "resourceVersion": "48213",
  "generation": 1,
  "creationTimestamp": "2026-10-24T14:03:11Z",
  "labels": {
    "quant.example.com/job": "momentum-sweep",
    "quant.example.com/trial": "0",
    "quant.example.com/shard": "1"
  },
  "annotations": {
    "quant.example.com/params": "{\"lookback\":20}"
  },
  "ownerReferences": [
    {
      "apiVersion": "quant.example.com/v1alpha1",
      "kind": "BacktestJob",
      "name": "momentum-sweep",
      "uid": "0b9d7e44-3f2a-4d1b-8c6e-5a4f2e1d0c9b",
      "controller": true,
      "blockOwnerDeletion": true
    }
  ],
  "finalizers": ["quant.example.com/cleanup"],
  "managedFields": [{"manager": "k8s-operator-cpp", "operation": "Update"}]
})json";

TEST(ObjectMeta, ParsesRealisticMetadata) {
  const auto meta = json::parse(kPodMetadata).get<ObjectMeta>();

  EXPECT_EQ(meta.name, "momentum-sweep-t0-s1-a0");
  EXPECT_EQ(meta.namespace_, "backtests");
  EXPECT_EQ(meta.uid, "6f1c2a0e-8a1b-4c55-9d1e-2b7f3c9a4e10");
  EXPECT_EQ(meta.resource_version, "48213");
  EXPECT_EQ(meta.generation, 1);
  EXPECT_EQ(meta.creation_timestamp, "2026-10-24T14:03:11Z");
  EXPECT_FALSE(meta.deletion_timestamp.has_value());
  EXPECT_EQ(meta.labels.size(), 3U);
  EXPECT_EQ(meta.labels.at("quant.example.com/trial"), "0");
  EXPECT_EQ(meta.annotations.at("quant.example.com/params"), R"({"lookback":20})");
  EXPECT_EQ(meta.finalizers, std::vector<std::string>{"quant.example.com/cleanup"});

  ASSERT_EQ(meta.owner_references.size(), 1U);
  const auto& owner = meta.owner_references[0];
  EXPECT_EQ(owner.api_version, "quant.example.com/v1alpha1");
  EXPECT_EQ(owner.kind, "BacktestJob");
  EXPECT_EQ(owner.name, "momentum-sweep");
  EXPECT_EQ(owner.uid, "0b9d7e44-3f2a-4d1b-8c6e-5a4f2e1d0c9b");
  EXPECT_TRUE(owner.controller);
  EXPECT_TRUE(owner.block_owner_deletion);
}

TEST(ObjectMeta, MissingFieldsGetDefaults) {
  const auto meta = json::parse(R"({"name": "x"})").get<ObjectMeta>();

  EXPECT_EQ(meta.name, "x");
  EXPECT_EQ(meta, ObjectMeta{.name = "x"});
}

TEST(ObjectMeta, DefaultSerializesToEmptyObject) {
  // omitempty: nothing set means nothing sent. This matters for merge patches, where sending
  // "finalizers": [] would delete every finalizer on the object.
  EXPECT_EQ(json(ObjectMeta{}), json::object());
}

TEST(ObjectMeta, OmitsOnlyEmptyFields) {
  const ObjectMeta meta{.name = "job", .labels = {{"a", "b"}}};
  EXPECT_EQ(json(meta), json::parse(R"({"name": "job", "labels": {"a": "b"}})"));
}

TEST(ObjectMeta, RoundTrips) {
  const auto original = json::parse(kPodMetadata).get<ObjectMeta>();
  ASSERT_FALSE(original.owner_references.empty());  // guard against passing on empty objects
  const auto copy = json(original).get<ObjectMeta>();
  EXPECT_EQ(copy, original);
}

TEST(ObjectMeta, DeletionTimestampRoundTrips) {
  const ObjectMeta meta{.name = "job", .deletion_timestamp = "2026-10-24T15:00:00Z"};
  const auto j = json(meta);
  EXPECT_EQ(j.at("deletionTimestamp"), "2026-10-24T15:00:00Z");
  EXPECT_EQ(j.get<ObjectMeta>(), meta);
}

TEST(OwnerReference, OmitsFalseFlags) {
  const OwnerReference ref{.api_version = "v1", .kind = "Pod", .name = "p", .uid = "u"};
  EXPECT_EQ(json(ref), json::parse(R"({"apiVersion":"v1","kind":"Pod","name":"p","uid":"u"})"));
}

TEST(OwnerReference, MissingRequiredFieldThrows) {
  const auto j = json::parse(R"({"apiVersion": "v1", "kind": "Pod", "name": "p"})");  // no uid
  EXPECT_THROW((void)j.get<OwnerReference>(), json::out_of_range);
}

TEST(ObjectMeta, WrongTypeThrows) {
  const auto j = json::parse(R"({"labels": ["not", "a", "map"]})");
  EXPECT_THROW((void)j.get<ObjectMeta>(), json::type_error);
}

}  // namespace
}  // namespace k8sop::api
