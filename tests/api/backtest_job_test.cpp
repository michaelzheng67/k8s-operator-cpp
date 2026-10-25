#include "k8sop/api/backtest_job.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <variant>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace k8sop::api {
namespace {

using nlohmann::json;

// The example from the README, as the API server would return it (YAML converted to JSON).
constexpr const char* kMomentumSweep = R"json({
  "apiVersion": "quant.example.com/v1alpha1",
  "kind": "BacktestJob",
  "metadata": {"name": "momentum-sweep", "namespace": "backtests", "generation": 1},
  "spec": {
    "strategy": "momentum",
    "image": "ghcr.io/me/backtester:latest",
    "universe": {"symbols": ["AAPL", "MSFT", "NVDA", "AMZN", "GOOG", "META"], "shardsPerTrial": 3},
    "window": {"start": "2024-01-01", "end": "2024-12-31"},
    "parameters": {
      "lookback": [20, 60, 120],
      "threshold": [0.5, 1.0],
      "mode": ["long", "short"]
    },
    "merge": {"image": "ghcr.io/me/merger:latest"},
    "objective": {"metric": "sharpe", "goal": "maximize", "topK": 3},
    "maxConcurrentPods": 10,
    "maxRetries": 2,
    "artifacts": {"uri": "s3://backtests/momentum-sweep"}
  },
  "status": {
    "phase": "Running",
    "trials": {"total": 6, "succeeded": 3, "running": 2, "pending": 1},
    "shards": {"total": 18, "succeeded": 12, "running": 4, "pending": 2},
    "best": [
      {
        "trial": 4,
        "parameters": {"lookback": 60, "threshold": 1.0, "mode": "long"},
        "metrics": {"sharpe": 1.84, "pnl": 0.231, "maxDrawdown": -0.072}
      }
    ],
    "resultsURI": "s3://backtests/momentum-sweep/results.json",
    "conditions": [
      {"type": "Progressing", "status": "True", "reason": "TrialsRunning",
       "lastTransitionTime": "2026-10-24T14:03:11Z", "observedGeneration": 1}
    ]
  }
})json";

// ---- ParamValue ---------------------------------------------------------------------------

TEST(ParamValue, IntegerBecomesInt64) {
  const auto v = json::parse("20").get<ParamValue>();
  ASSERT_TRUE(std::holds_alternative<std::int64_t>(v));
  EXPECT_EQ(std::get<std::int64_t>(v), 20);
}

TEST(ParamValue, NegativeAndLargeIntegers) {
  EXPECT_EQ(json::parse("-3").get<ParamValue>(), ParamValue{std::int64_t{-3}});
  EXPECT_EQ(json::parse("9007199254740993").get<ParamValue>(),  // 2^53 + 1: not exact as a double
            ParamValue{std::int64_t{9007199254740993}});
}

TEST(ParamValue, FloatBecomesDouble) {
  const auto v = json::parse("0.5").get<ParamValue>();
  ASSERT_TRUE(std::holds_alternative<double>(v));
  EXPECT_DOUBLE_EQ(std::get<double>(v), 0.5);
}

TEST(ParamValue, DoubleKeepsFullPrecision) {
  // 0.1 has no exact binary representation, so any trip through float changes its value.
  // (0.5 and 0.25 are exact in both float and double, which is why they can't catch this.)
  EXPECT_EQ(std::get<double>(json::parse("0.1").get<ParamValue>()), 0.1);
}

TEST(ParamValue, OnePointZeroStaysDouble) {
  // The user wrote 1.0, so this parameter is a double even though the value is a whole number.
  EXPECT_TRUE(std::holds_alternative<double>(json::parse("1.0").get<ParamValue>()));
}

TEST(ParamValue, StringBecomesString) {
  EXPECT_EQ(json::parse(R"("long")").get<ParamValue>(), ParamValue{std::string("long")});
}

TEST(ParamValue, UnsupportedTypesThrow) {
  EXPECT_THROW((void)json::parse("true").get<ParamValue>(), json::type_error);
  EXPECT_THROW((void)json::parse("null").get<ParamValue>(), json::type_error);
  EXPECT_THROW((void)json::parse("[1, 2]").get<ParamValue>(), json::type_error);
  EXPECT_THROW((void)json::parse(R"({"a": 1})").get<ParamValue>(), json::type_error);
}

TEST(ParamValue, ToJsonKeepsTheType) {
  EXPECT_TRUE(json(ParamValue{std::int64_t{20}}).is_number_integer());
  EXPECT_TRUE(json(ParamValue{1.0}).is_number_float());
  EXPECT_EQ(json(ParamValue{std::string("long")}), "long");
}

TEST(ParamValue, RoundTripsEveryAlternative) {
  for (const ParamValue& v :
       {ParamValue{std::int64_t{-7}}, ParamValue{0.25},
        ParamValue{std::numeric_limits<std::int64_t>::max()}, ParamValue{std::string("short")}}) {
    EXPECT_EQ(json(v).get<ParamValue>(), v);
  }
}

TEST(ParamValue, ToString) {
  EXPECT_EQ(to_string(ParamValue{std::int64_t{20}}), "20");
  EXPECT_EQ(to_string(ParamValue{std::int64_t{-3}}), "-3");
  EXPECT_EQ(to_string(ParamValue{0.5}), "0.5");
  EXPECT_EQ(to_string(ParamValue{0.1}), "0.1");  // std::to_string would give "0.100000"
  EXPECT_EQ(to_string(ParamValue{1.0}), "1");
  EXPECT_EQ(to_string(ParamValue{std::string("long")}), "long");
}

// ---- BacktestJob --------------------------------------------------------------------------

TEST(BacktestJob, ParsesSpec) {
  const auto job = json::parse(kMomentumSweep).get<BacktestJob>();
  const auto& spec = job.spec;

  EXPECT_EQ(job.metadata.name, "momentum-sweep");
  EXPECT_EQ(spec.strategy, "momentum");
  EXPECT_EQ(spec.image, "ghcr.io/me/backtester:latest");
  EXPECT_EQ(spec.universe.symbols.size(), 6U);
  EXPECT_EQ(spec.universe.shards_per_trial, 3);
  EXPECT_EQ(spec.window, (Window{.start = "2024-01-01", .end = "2024-12-31"}));
  EXPECT_EQ(spec.merge.image, "ghcr.io/me/merger:latest");
  EXPECT_EQ(spec.objective, (Objective{.metric = "sharpe", .goal = Goal::Maximize, .top_k = 3}));
  EXPECT_EQ(spec.max_concurrent_pods, 10);
  EXPECT_EQ(spec.max_retries, 2);
  EXPECT_EQ(spec.artifacts.uri, "s3://backtests/momentum-sweep");
}

TEST(BacktestJob, ParsesParameterGrid) {
  const auto params = json::parse(kMomentumSweep).get<BacktestJob>().spec.parameters;

  ASSERT_EQ(params.size(), 3U);
  EXPECT_EQ(params.at("lookback"),
            (std::vector<ParamValue>{std::int64_t{20}, std::int64_t{60}, std::int64_t{120}}));
  EXPECT_EQ(params.at("threshold"), (std::vector<ParamValue>{0.5, 1.0}));
  EXPECT_EQ(params.at("mode"),
            (std::vector<ParamValue>{std::string("long"), std::string("short")}));
}

TEST(BacktestJob, ParsesStatus) {
  const auto status = json::parse(kMomentumSweep).get<BacktestJob>().status;

  EXPECT_EQ(status.phase, JobPhase::Running);
  EXPECT_EQ(status.trials, (Counts{.total = 6, .succeeded = 3, .running = 2, .pending = 1}));
  EXPECT_EQ(status.shards.total, 18);
  EXPECT_EQ(status.results_uri, "s3://backtests/momentum-sweep/results.json");

  ASSERT_EQ(status.best.size(), 1U);
  const auto& best = status.best[0];
  EXPECT_EQ(best.trial, 4);
  EXPECT_EQ(best.parameters.at("lookback"), ParamValue{std::int64_t{60}});
  EXPECT_EQ(best.parameters.at("threshold"), ParamValue{1.0});
  EXPECT_DOUBLE_EQ(best.metrics.at("sharpe"), 1.84);

  ASSERT_EQ(status.conditions.size(), 1U);
  EXPECT_EQ(status.conditions[0].type, "Progressing");
  EXPECT_EQ(status.conditions[0].observed_generation, 1);
}

TEST(BacktestJob, MissingFieldsGetDefaults) {
  const auto job = json::parse(R"({"spec": {"strategy": "momentum"}})").get<BacktestJob>();

  EXPECT_EQ(job.spec.universe.shards_per_trial, 1);
  EXPECT_EQ(job.spec.max_concurrent_pods, 10);
  EXPECT_EQ(job.spec.max_retries, 2);
  EXPECT_EQ(job.spec.objective.goal, Goal::Maximize);
  EXPECT_EQ(job.spec.objective.top_k, 1);
  EXPECT_EQ(job.status.phase, JobPhase::Pending);
}

TEST(BacktestJob, EnumNames) {
  EXPECT_EQ(to_string(JobPhase::Pending), "Pending");
  EXPECT_EQ(to_string(JobPhase::Succeeded), "Succeeded");
  EXPECT_EQ(to_string(Goal::Minimize), "minimize");
}

TEST(BacktestJob, UnknownEnumValuesThrow) {
  // Unlike PodPhase, these are our own types: the CRD schema rejects anything else, so an unknown
  // value means a bug, not a newer API server.
  EXPECT_THROW((void)json::parse(R"({"status": {"phase": "Paused"}})").get<BacktestJob>(),
               json::type_error);
  EXPECT_THROW(
      (void)json::parse(R"({"spec": {"objective": {"goal": "maximise"}}})").get<BacktestJob>(),
      json::type_error);
}

TEST(BacktestJob, SerializesApiVersionAndKind) {
  const auto j = json(BacktestJob{.metadata = {.name = "j"}});
  EXPECT_EQ(j.at("apiVersion"), "quant.example.com/v1alpha1");
  EXPECT_EQ(j.at("kind"), "BacktestJob");
}

TEST(BacktestJob, RoundTrips) {
  const auto original = json::parse(kMomentumSweep).get<BacktestJob>();
  ASSERT_FALSE(original.spec.parameters.empty());
  ASSERT_FALSE(original.status.best.empty());
  EXPECT_EQ(json(original).get<BacktestJob>(), original);
}

}  // namespace
}  // namespace k8sop::api
