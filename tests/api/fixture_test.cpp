// Parses real API responses captured from a kind cluster with `kubectl get pod <name> -o json`.
// They carry every field Kubernetes fills in (managedFields, tolerations, volumes, conditions,
// ...), so they check that unmodeled fields are ignored, not just the hand-picked ones elsewhere.

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "k8sop/api/parse.hpp"
#include "k8sop/api/pod.hpp"

namespace k8sop::api {
namespace {

std::string read_fixture(const std::string& name) {
  const std::string path = std::string(K8SOP_FIXTURE_DIR) + "/" + name;
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    ADD_FAILURE() << "cannot open fixture " << path;
    return {};
  }
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

template <typename T>
std::string describe(const std::expected<T, ParseError>& result) {
  return result ? "ok" : result.error().message;
}

// A shard that ran `sh -c 'echo working; exit 3'` with restartPolicy Never.
TEST(Fixture, FailedPod) {
  const auto pod = parse<Pod>(read_fixture("pod_failed.json"));
  ASSERT_TRUE(pod.has_value()) << describe(pod);

  EXPECT_EQ(pod->metadata.name, "shard-demo");
  EXPECT_EQ(pod->metadata.namespace_, "default");
  EXPECT_FALSE(pod->metadata.uid.empty());
  EXPECT_FALSE(pod->metadata.resource_version.empty());
  EXPECT_TRUE(pod->metadata.creation_timestamp.has_value());
  EXPECT_EQ(pod->metadata.labels.at("quant.example.com/job"), "demo");
  EXPECT_EQ(pod->metadata.labels.at("quant.example.com/shard"), "0");

  ASSERT_EQ(pod->spec.containers.size(), 1U);
  const auto& container = pod->spec.containers[0];
  EXPECT_EQ(container.image, "busybox:1.36");
  EXPECT_TRUE(container.command.empty());
  EXPECT_EQ(container.args, (std::vector<std::string>{"sh", "-c", "echo working; exit 3"}));
  ASSERT_EQ(container.env.size(), 1U);
  EXPECT_EQ(container.env[0].name, "BT_SYMBOLS");
  EXPECT_EQ(container.env[0].value, "AAPL,MSFT");
  EXPECT_EQ(pod->spec.restart_policy, "Never");

  EXPECT_EQ(pod->status.phase, PodPhase::Failed);
  ASSERT_EQ(pod->status.container_statuses.size(), 1U);
  const auto& terminated = pod->status.container_statuses[0].terminated;
  ASSERT_TRUE(terminated.has_value());
  EXPECT_EQ(terminated->exit_code, 3);
  EXPECT_EQ(terminated->reason, "Error");
}

// A shard still sleeping: state.running instead of state.terminated, and an env var set through
// valueFrom (the downward API), which has no "value" key.
TEST(Fixture, RunningPod) {
  const auto pod = parse<Pod>(read_fixture("pod_running.json"));
  ASSERT_TRUE(pod.has_value()) << describe(pod);

  EXPECT_EQ(pod->metadata.name, "shard-running");
  EXPECT_EQ(pod->status.phase, PodPhase::Running);
  ASSERT_EQ(pod->status.container_statuses.size(), 1U);
  EXPECT_FALSE(pod->status.container_statuses[0].terminated.has_value());

  ASSERT_EQ(pod->spec.containers.size(), 1U);
  const auto& env = pod->spec.containers[0].env;
  ASSERT_EQ(env.size(), 2U);
  EXPECT_EQ(env[1].name, "POD_NAME");
  EXPECT_EQ(env[1].value, "");
}

// Unmodeled fields are dropped on the way in, so the JSON text can't round-trip; compare the
// typed objects instead: everything we model must survive serialize -> parse.
TEST(Fixture, RoundTripKeepsModeledFields) {
  for (const char* name : {"pod_failed.json", "pod_running.json"}) {
    SCOPED_TRACE(name);
    const auto first = parse<Pod>(read_fixture(name));
    ASSERT_TRUE(first.has_value()) << describe(first);

    const auto second = parse<Pod>(serialize(*first));
    ASSERT_TRUE(second.has_value()) << describe(second);
    EXPECT_EQ(*second, *first);
  }
}

}  // namespace
}  // namespace k8sop::api
