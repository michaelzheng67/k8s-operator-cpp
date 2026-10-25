#include "k8sop/api/pod.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace k8sop::api {
namespace {

using nlohmann::json;

// A failed shard Pod as returned by `kubectl get pod -o json`, trimmed but with plenty of fields
// we don't model (volumes, nodeName, conditions, ...), which must be ignored.
constexpr const char* kFailedShardPod = R"json({
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "momentum-sweep-t0-s1-a0",
    "namespace": "backtests",
    "uid": "6f1c2a0e-8a1b-4c55-9d1e-2b7f3c9a4e10",
    "resourceVersion": "48290",
    "labels": {"quant.example.com/job": "momentum-sweep"}
  },
  "spec": {
    "restartPolicy": "Never",
    "nodeName": "bt-control-plane",
    "volumes": [{"name": "kube-api-access-x7k2p", "projected": {"sources": []}}],
    "containers": [
      {
        "name": "worker",
        "image": "ghcr.io/me/backtester:latest",
        "command": ["python", "-m", "backtester"],
        "args": ["--verbose"],
        "env": [
          {"name": "BT_STRATEGY", "value": "momentum"},
          {"name": "BT_SYMBOLS", "value": "NVDA,AMZN"}
        ],
        "imagePullPolicy": "IfNotPresent",
        "resources": {}
      }
    ]
  },
  "status": {
    "phase": "Failed",
    "conditions": [{"type": "Ready", "status": "False"}],
    "containerStatuses": [
      {
        "name": "worker",
        "ready": false,
        "restartCount": 0,
        "image": "ghcr.io/me/backtester:latest",
        "state": {
          "terminated": {
            "exitCode": 1,
            "reason": "Error",
            "message": "KeyError: 'NVDA'",
            "startedAt": "2026-10-24T14:03:15Z",
            "finishedAt": "2026-10-24T14:03:41Z"
          }
        }
      }
    ]
  }
})json";

TEST(Pod, ParsesSpec) {
  const auto pod = json::parse(kFailedShardPod).get<Pod>();

  EXPECT_EQ(pod.metadata.name, "momentum-sweep-t0-s1-a0");
  EXPECT_EQ(pod.spec.restart_policy, "Never");
  ASSERT_EQ(pod.spec.containers.size(), 1U);

  const auto& c = pod.spec.containers[0];
  EXPECT_EQ(c.name, "worker");
  EXPECT_EQ(c.image, "ghcr.io/me/backtester:latest");
  EXPECT_EQ(c.command, (std::vector<std::string>{"python", "-m", "backtester"}));
  EXPECT_EQ(c.args, std::vector<std::string>{"--verbose"});
  ASSERT_EQ(c.env.size(), 2U);
  EXPECT_EQ(c.env[1], (EnvVar{.name = "BT_SYMBOLS", .value = "NVDA,AMZN"}));
}

TEST(Pod, ParsesTerminatedStatus) {
  const auto pod = json::parse(kFailedShardPod).get<Pod>();

  EXPECT_EQ(pod.status.phase, PodPhase::Failed);
  ASSERT_EQ(pod.status.container_statuses.size(), 1U);

  const auto& cs = pod.status.container_statuses[0];
  EXPECT_EQ(cs.name, "worker");
  // JSON nests this as state.terminated; in C++ it's flattened to a single optional.
  ASSERT_TRUE(cs.terminated.has_value());
  const auto& terminated = cs.terminated.value();
  EXPECT_EQ(terminated.exit_code, 1);
  EXPECT_EQ(terminated.reason, "Error");
  EXPECT_EQ(terminated.message, "KeyError: 'NVDA'");
}

TEST(Pod, RunningContainerHasNoTerminatedState) {
  const auto j = json::parse(R"({
    "metadata": {"name": "p"},
    "status": {
      "phase": "Running",
      "containerStatuses": [{"name": "worker", "state": {"running": {"startedAt": "x"}}}]
    }
  })");
  const auto pod = j.get<Pod>();

  EXPECT_EQ(pod.status.phase, PodPhase::Running);
  ASSERT_EQ(pod.status.container_statuses.size(), 1U);
  EXPECT_FALSE(pod.status.container_statuses[0].terminated.has_value());
}

TEST(Pod, MissingStatusDefaultsToPending) {
  // A Pod that was just created has no status yet.
  const auto pod = json::parse(R"({"metadata": {"name": "p"}})").get<Pod>();
  EXPECT_EQ(pod.status.phase, PodPhase::Pending);
  EXPECT_TRUE(pod.status.container_statuses.empty());
}

TEST(PodPhase, ParsesEveryPhase) {
  const auto phase_of = [](const char* s) {
    return json{{"status", {{"phase", s}}}}.get<Pod>().status.phase;
  };
  EXPECT_EQ(phase_of("Pending"), PodPhase::Pending);
  EXPECT_EQ(phase_of("Running"), PodPhase::Running);
  EXPECT_EQ(phase_of("Succeeded"), PodPhase::Succeeded);
  EXPECT_EQ(phase_of("Failed"), PodPhase::Failed);
  EXPECT_EQ(phase_of("Unknown"), PodPhase::Unknown);
}

TEST(PodPhase, UnrecognizedPhaseMapsToUnknown) {
  // Forward compatibility: a newer API server might add a phase. Don't throw.
  const auto pod = json{{"status", {{"phase", "Hibernating"}}}}.get<Pod>();
  EXPECT_EQ(pod.status.phase, PodPhase::Unknown);
}

TEST(PodPhase, ToString) {
  EXPECT_EQ(to_string(PodPhase::Pending), "Pending");
  EXPECT_EQ(to_string(PodPhase::Running), "Running");
  EXPECT_EQ(to_string(PodPhase::Succeeded), "Succeeded");
  EXPECT_EQ(to_string(PodPhase::Failed), "Failed");
  EXPECT_EQ(to_string(PodPhase::Unknown), "Unknown");
}

// The JSON the operator sends to create a shard Pod: apiVersion and kind are always present,
// empty fields are omitted, and a default (empty) status is not sent: the API server owns it.
TEST(Pod, SerializesPodForCreate) {
  const Pod pod{
      .metadata = {.name = "momentum-sweep-t0-s1-a0",
                   .namespace_ = "backtests",
                   .labels = {{"quant.example.com/job", "momentum-sweep"}}},
      .spec = {.containers = {{.name = "worker",
                               .image = "ghcr.io/me/backtester:latest",
                               .env = {{.name = "BT_SYMBOLS", .value = "NVDA,AMZN"}}}},
               .restart_policy = "Never"},
  };

  EXPECT_EQ(json(pod), json::parse(R"({
    "apiVersion": "v1",
    "kind": "Pod",
    "metadata": {
      "name": "momentum-sweep-t0-s1-a0",
      "namespace": "backtests",
      "labels": {"quant.example.com/job": "momentum-sweep"}
    },
    "spec": {
      "restartPolicy": "Never",
      "containers": [
        {
          "name": "worker",
          "image": "ghcr.io/me/backtester:latest",
          "env": [{"name": "BT_SYMBOLS", "value": "NVDA,AMZN"}]
        }
      ]
    }
  })"));
}

TEST(Pod, StatusRoundTrips) {
  // Not something the operator sends, but tests and fakes need to build Pods with a status.
  const auto original = json::parse(kFailedShardPod).get<Pod>();
  ASSERT_TRUE(original.status.container_statuses.at(0).terminated.has_value());
  EXPECT_EQ(json(original).get<Pod>(), original);
}

}  // namespace
}  // namespace k8sop::api
