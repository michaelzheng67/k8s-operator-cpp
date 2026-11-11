#include "k8sop/client/resource.hpp"

#include <gtest/gtest.h>

namespace k8sop::client {
namespace {

static_assert(Resource<api::Pod>);
static_assert(Resource<api::BacktestJob>);
static_assert(Resource<api::BacktestSchedule>);
static_assert(!Resource<api::Container>);  // a part of a Pod, not something with its own URL
static_assert(!Resource<int>);

TEST(ResourcePath, CoreGroupObject) {
  EXPECT_EQ(resource_path<api::Pod>("default", "shard-demo"),
            "/api/v1/namespaces/default/pods/shard-demo");
}

TEST(ResourcePath, CoreGroupCollection) {
  EXPECT_EQ(resource_path<api::Pod>("default"), "/api/v1/namespaces/default/pods");
}

TEST(ResourcePath, CustomGroup) {
  EXPECT_EQ(resource_path<api::BacktestJob>("backtests", "momentum-sweep"),
            "/apis/quant.example.com/v1alpha1/namespaces/backtests/backtestjobs/momentum-sweep");
  EXPECT_EQ(resource_path<api::BacktestSchedule>("backtests"),
            "/apis/quant.example.com/v1alpha1/namespaces/backtests/backtestschedules");
}

}  // namespace
}  // namespace k8sop::client
