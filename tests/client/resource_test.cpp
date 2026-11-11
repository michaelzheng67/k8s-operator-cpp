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

TEST(UrlEncode, LabelSelector) {
  EXPECT_EQ(url_encode("quant.example.com/job=demo"), "quant.example.com%2Fjob%3Ddemo");
}

TEST(UrlEncode, UnreservedCharactersPassThrough) {
  EXPECT_EQ(url_encode("AZaz09-._~"), "AZaz09-._~");
  EXPECT_EQ(url_encode(""), "");
}

TEST(UrlEncode, EverythingElseIsEscaped) {
  EXPECT_EQ(url_encode("a b,c!=d"), "a%20b%2Cc%21%3Dd");
  EXPECT_EQ(url_encode("\xff"), "%FF");  // bytes above 127: watch out for signed char
}

}  // namespace
}  // namespace k8sop::client
