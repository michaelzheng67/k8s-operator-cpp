#include "k8sop/api/parse.hpp"

#include <string_view>

#include <gtest/gtest.h>

#include "k8sop/api/backtest_job.hpp"
#include "k8sop/api/backtest_schedule.hpp"
#include "k8sop/api/pod.hpp"

namespace k8sop::api {
namespace {

using Kind = ParseError::Kind;

// parse() must never throw, whatever the input. The compiler can check the declaration:
static_assert(noexcept(parse<Pod>(std::string_view{})));

// For readable failures: prints the error when a parse unexpectedly fails.
template <typename T>
std::string describe(const std::expected<T, ParseError>& result) {
  return result ? "ok" : result.error().message;
}

TEST(Parse, ValidPod) {
  const auto pod = parse<Pod>(R"({"metadata": {"name": "p"}, "status": {"phase": "Running"}})");
  ASSERT_TRUE(pod.has_value()) << describe(pod);
  EXPECT_EQ(pod->metadata.name, "p");
  EXPECT_EQ(pod->status.phase, PodPhase::Running);
}

TEST(Parse, ValidBacktestJobAndSchedule) {
  const auto job = parse<BacktestJob>(R"({"spec": {"parameters": {"lookback": [20, 60]}}})");
  ASSERT_TRUE(job.has_value()) << describe(job);
  EXPECT_EQ(job->spec.parameters.at("lookback").size(), 2U);

  const auto schedule = parse<BacktestSchedule>(R"({"spec": {"schedule": "0 6 * * 1-5"}})");
  ASSERT_TRUE(schedule.has_value()) << describe(schedule);
  EXPECT_EQ(schedule->spec.schedule, "0 6 * * 1-5");
}

TEST(Parse, TruncatedBodyIsSyntaxError) {
  const auto pod = parse<Pod>(R"({"metadata": {"name": "p")");
  ASSERT_FALSE(pod.has_value());
  EXPECT_EQ(pod.error().kind, Kind::Syntax);
  EXPECT_GT(pod.error().byte, 0U);
  EXPECT_FALSE(pod.error().message.empty());
}

TEST(Parse, EmptyBodyIsSyntaxError) {
  const auto pod = parse<Pod>("");
  ASSERT_FALSE(pod.has_value());
  EXPECT_EQ(pod.error().kind, Kind::Syntax);
}

TEST(Parse, WrongTypeIsTypeError) {
  const auto pod = parse<Pod>(R"({"metadata": {"labels": ["not", "a", "map"]}})");
  ASSERT_FALSE(pod.has_value());
  EXPECT_EQ(pod.error().kind, Kind::Type);
  EXPECT_EQ(pod.error().byte, 0U);
}

TEST(Parse, UnknownEnumIsTypeError) {
  const auto job = parse<BacktestJob>(R"({"status": {"phase": "Paused"}})");
  ASSERT_FALSE(job.has_value());
  EXPECT_EQ(job.error().kind, Kind::Type);
  EXPECT_NE(job.error().message.find("Paused"), std::string::npos);
}

TEST(Parse, UnsupportedParamValueIsTypeError) {
  const auto job = parse<BacktestJob>(R"({"spec": {"parameters": {"flag": [true]}}})");
  ASSERT_FALSE(job.has_value());
  EXPECT_EQ(job.error().kind, Kind::Type);
}

TEST(Parse, MissingRequiredFieldIsMissing) {
  // ownerReferences[].uid is required.
  const auto meta = parse<ObjectMeta>(
      R"({"ownerReferences": [{"apiVersion": "v1", "kind": "Pod", "name": "p"}]})");
  ASSERT_FALSE(meta.has_value());
  EXPECT_EQ(meta.error().kind, Kind::Missing);
}

TEST(Parse, NonObjectIsTypeError) {
  // Valid JSON, but an array is not a Pod. Must not silently parse as a default Pod.
  const auto pod = parse<Pod>("[1, 2, 3]");
  ASSERT_FALSE(pod.has_value());
  EXPECT_EQ(pod.error().kind, Kind::Type);

  const auto nested = parse<Pod>(R"({"metadata": [1]})");
  ASSERT_FALSE(nested.has_value());
  EXPECT_EQ(nested.error().kind, Kind::Type);
}

TEST(Serialize, RoundTripsThroughParse) {
  const Pod pod{.metadata = {.name = "p", .labels = {{"a", "b"}}},
                .spec = {.containers = {{.name = "worker", .image = "img"}}}};
  const auto body = serialize(pod);
  EXPECT_EQ(body.find('\n'), std::string::npos);  // compact: one line, ready to send

  const auto parsed = parse<Pod>(body);
  ASSERT_TRUE(parsed.has_value()) << describe(parsed);
  EXPECT_EQ(*parsed, pod);
}

}  // namespace
}  // namespace k8sop::api
