#include "k8sop/api/backtest_schedule.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace k8sop::api {
namespace {

using nlohmann::json;

constexpr const char* kMomentumDaily = R"json({
  "apiVersion": "quant.example.com/v1alpha1",
  "kind": "BacktestSchedule",
  "metadata": {"name": "momentum-daily", "namespace": "backtests"},
  "spec": {
    "schedule": "0 6 * * 1-5",
    "timeZone": "America/New_York",
    "rollingWindow": "252d",
    "concurrencyPolicy": "Forbid",
    "historyLimit": 30,
    "alerts": {"minSharpe": 1.0},
    "jobTemplate": {
      "spec": {
        "strategy": "momentum",
        "image": "ghcr.io/me/backtester:latest",
        "parameters": {"lookback": [20, 60]}
      }
    }
  },
  "status": {
    "lastScheduleTime": "2026-10-23T10:00:00Z",
    "lastSuccessfulJob": "momentum-daily-20261023",
    "history": [
      {
        "job": "momentum-daily-20261023",
        "best": {"trial": 1, "parameters": {"lookback": 60}, "metrics": {"sharpe": 1.12}}
      },
      {"job": "momentum-daily-20261022"}
    ],
    "conditions": [{"type": "Degraded", "status": "False", "reason": "SharpeAboveThreshold"}]
  }
})json";

TEST(BacktestSchedule, ParsesSpec) {
  const auto spec = json::parse(kMomentumDaily).get<BacktestSchedule>().spec;

  EXPECT_EQ(spec.schedule, "0 6 * * 1-5");
  EXPECT_EQ(spec.time_zone, "America/New_York");
  EXPECT_EQ(spec.rolling_window, "252d");
  EXPECT_EQ(spec.concurrency_policy, ConcurrencyPolicy::Forbid);
  EXPECT_EQ(spec.history_limit, 30);
  EXPECT_EQ(spec.alerts.min_sharpe, 1.0);
}

TEST(BacktestSchedule, JobTemplateIsNestedUnderSpec) {
  const auto spec = json::parse(kMomentumDaily).get<BacktestSchedule>().spec;

  EXPECT_EQ(spec.job_template.strategy, "momentum");
  EXPECT_EQ(spec.job_template.parameters.at("lookback").size(), 2U);
  EXPECT_EQ(json(spec).at("jobTemplate").at("spec").at("strategy"), "momentum");
}

TEST(BacktestSchedule, ParsesStatus) {
  const auto status = json::parse(kMomentumDaily).get<BacktestSchedule>().status;

  EXPECT_EQ(status.last_schedule_time, "2026-10-23T10:00:00Z");
  EXPECT_EQ(status.last_successful_job, "momentum-daily-20261023");
  ASSERT_EQ(status.history.size(), 2U);
  ASSERT_TRUE(status.history[0].best.has_value());
  EXPECT_FALSE(status.history[1].best.has_value());
  EXPECT_EQ(status.history[0].best, (TrialResult{.trial = 1,
                                                 .parameters = {{"lookback", std::int64_t{60}}},
                                                 .metrics = {{"sharpe", 1.12}}}));
  ASSERT_EQ(status.conditions.size(), 1U);
  EXPECT_EQ(status.conditions[0].type, "Degraded");
}

TEST(BacktestSchedule, MissingFieldsGetDefaults) {
  const auto spec =
      json::parse(R"({"spec": {"schedule": "*/5 * * * *"}})").get<BacktestSchedule>().spec;
  EXPECT_EQ(spec.time_zone, "UTC");
  EXPECT_EQ(spec.concurrency_policy, ConcurrencyPolicy::Forbid);
  EXPECT_EQ(spec.history_limit, 30);
  EXPECT_FALSE(spec.alerts.min_sharpe.has_value());
}

TEST(BacktestSchedule, ZeroMinSharpeIsKept) {
  // 0.0 is a real threshold, not "unset": the optional must survive a round trip.
  const BacktestScheduleSpec spec{.alerts = {.min_sharpe = 0.0}};
  EXPECT_EQ(json(spec).get<BacktestScheduleSpec>().alerts.min_sharpe, 0.0);
}

TEST(BacktestSchedule, ConcurrencyPolicies) {
  EXPECT_EQ(to_string(ConcurrencyPolicy::Replace), "Replace");
  const auto spec = json::parse(R"({"concurrencyPolicy": "Allow"})").get<BacktestScheduleSpec>();
  EXPECT_EQ(spec.concurrency_policy, ConcurrencyPolicy::Allow);
  EXPECT_THROW(
      (void)json::parse(R"({"concurrencyPolicy": "Sometimes"})").get<BacktestScheduleSpec>(),
      json::type_error);
}

TEST(BacktestSchedule, RoundTrips) {
  const auto original = json::parse(kMomentumDaily).get<BacktestSchedule>();
  ASSERT_FALSE(original.status.history.empty());
  const auto j = json(original);
  EXPECT_EQ(j.at("kind"), "BacktestSchedule");
  EXPECT_EQ(j.get<BacktestSchedule>(), original);
}

}  // namespace
}  // namespace k8sop::api
