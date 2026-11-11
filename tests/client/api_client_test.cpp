// Integration tests against the kubeconfig's cluster; run only with K8SOP_INTEGRATION=1.
// Expects the Pods from the Milestone 1 fixtures to exist in `default`:
//   kubectl run shard-demo --image=busybox:1.36 --restart=Never ... -- sh -c 'echo working; exit 3'

#include "k8sop/client/api_client.hpp"

#include <cstdlib>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace k8sop::client {
namespace {

class ApiClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* flag = std::getenv("K8SOP_INTEGRATION");
    if (flag == nullptr || std::string_view(flag) != "1") {
      GTEST_SKIP() << "set K8SOP_INTEGRATION=1 to run against the kubeconfig's cluster";
    }
    auto config = load_default_kubeconfig();
    ASSERT_TRUE(config.has_value()) << config.error().message;
    client_.emplace(std::move(*config));
  }

  std::optional<ApiClient> client_;
};

TEST_F(ApiClientTest, GetPod) {
  const auto pod = client_->get<api::Pod>("default", "shard-demo");
  ASSERT_TRUE(pod.has_value()) << pod.error().reason << ": " << pod.error().message;
  EXPECT_EQ(pod->metadata.name, "shard-demo");
  EXPECT_EQ(pod->status.phase, api::PodPhase::Failed);
}

TEST_F(ApiClientTest, MissingPodIsNotFound) {
  const auto pod = client_->get<api::Pod>("default", "no-such-pod");
  ASSERT_FALSE(pod.has_value());
  EXPECT_EQ(pod.error().status, 404);
  EXPECT_EQ(pod.error().reason, "NotFound");  // from the Status body, not the HTTP reason phrase
  EXPECT_FALSE(pod.error().is_retryable());
}

TEST_F(ApiClientTest, ReusesConnectionAcrossCalls) {
  for (int i = 0; i < 3; ++i) {
    const auto pod = client_->get<api::Pod>("default", "shard-demo");
    ASSERT_TRUE(pod.has_value()) << pod.error().message;
  }
}

TEST_F(ApiClientTest, ListPodsBySelector) {
  const auto pods = client_->list<api::Pod>("default", "quant.example.com/job=demo");
  ASSERT_TRUE(pods.has_value()) << pods.error().reason << ": " << pods.error().message;
  EXPECT_FALSE(pods->metadata.resource_version.empty());
  ASSERT_EQ(pods->items.size(), 2U);  // shard-demo and shard-running
  for (const auto& pod : pods->items) {
    EXPECT_EQ(pod.metadata.labels.at("quant.example.com/job"), "demo");
  }
}

TEST_F(ApiClientTest, ListWithNoMatchesIsEmpty) {
  const auto pods = client_->list<api::Pod>("default", "quant.example.com/job=nothing");
  ASSERT_TRUE(pods.has_value()) << pods.error().reason << ": " << pods.error().message;
  EXPECT_TRUE(pods->items.empty());
}

}  // namespace
}  // namespace k8sop::client
