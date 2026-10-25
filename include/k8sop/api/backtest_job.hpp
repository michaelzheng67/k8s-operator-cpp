#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "k8sop/api/condition.hpp"
#include "k8sop/api/meta.hpp"

namespace k8sop::api {

inline constexpr std::string_view kQuantApiVersion = "quant.example.com/v1alpha1";

// One value in a parameter grid. In YAML:  lookback: [20, 60]   threshold: [0.5, 1.0]   mode:
// [long]
using ParamValue = std::variant<std::int64_t, double, std::string>;

// One trial's full parameter assignment, e.g. {lookback: 60, threshold: 1.0}.
using ParamSet = std::map<std::string, ParamValue>;

// Human-readable form for logs and labels: 20 -> "20", 0.5 -> "0.5", "long" -> "long".
[[nodiscard]] std::string to_string(const ParamValue& value);

struct Universe {
  std::vector<std::string> symbols;   // "symbols"
  std::int32_t shards_per_trial = 1;  // "shardsPerTrial"

  bool operator==(const Universe&) const = default;
};

struct Window {
  std::string start;  // "start", YYYY-MM-DD
  std::string end;    // "end",   YYYY-MM-DD

  bool operator==(const Window&) const = default;
};

struct MergeSpec {
  std::string image;  // "image"

  bool operator==(const MergeSpec&) const = default;
};

enum class Goal : std::uint8_t { Maximize, Minimize };
[[nodiscard]] std::string_view to_string(Goal goal) noexcept;

struct Objective {
  std::string metric;          // "metric", e.g. "sharpe"
  Goal goal = Goal::Maximize;  // "goal": maximize | minimize
  std::int32_t top_k = 1;      // "topK"

  bool operator==(const Objective&) const = default;
};

struct Artifacts {
  std::string uri;  // "uri", e.g. s3://backtests/momentum-sweep

  bool operator==(const Artifacts&) const = default;
};

struct BacktestJobSpec {
  std::string strategy;                                       // "strategy"
  std::string image;                                          // "image"
  Universe universe;                                          // "universe"
  Window window;                                              // "window"
  std::map<std::string, std::vector<ParamValue>> parameters;  // "parameters" (the grid)
  MergeSpec merge;                                            // "merge"
  Objective objective;                                        // "objective"
  std::int32_t max_concurrent_pods = 10;                      // "maxConcurrentPods"
  std::int32_t max_retries = 2;                               // "maxRetries"
  Artifacts artifacts;                                        // "artifacts"

  bool operator==(const BacktestJobSpec&) const = default;
};

enum class JobPhase : std::uint8_t { Pending, Running, Succeeded, Failed };
[[nodiscard]] std::string_view to_string(JobPhase phase) noexcept;

struct Counts {
  std::int32_t total = 0;      // "total"
  std::int32_t succeeded = 0;  // "succeeded"
  std::int32_t running = 0;    // "running"
  std::int32_t pending = 0;    // "pending"
  std::int32_t failed = 0;     // "failed"

  bool operator==(const Counts&) const = default;
};

struct TrialResult {
  std::int32_t trial = 0;                 // "trial" (index into the expanded grid)
  ParamSet parameters;                    // "parameters"
  std::map<std::string, double> metrics;  // "metrics", e.g. {sharpe: 1.84, pnl: 0.231}

  bool operator==(const TrialResult&) const = default;
};

struct BacktestJobStatus {
  JobPhase phase = JobPhase::Pending;  // "phase"
  Counts trials;                       // "trials"
  Counts shards;                       // "shards"
  std::vector<TrialResult> best;       // "best" (top-K, best first)
  std::string results_uri;             // "resultsURI"
  std::vector<Condition> conditions;   // "conditions"

  bool operator==(const BacktestJobStatus&) const = default;
};

struct BacktestJob {
  ObjectMeta metadata;
  BacktestJobSpec spec;
  BacktestJobStatus status;

  bool operator==(const BacktestJob&) const = default;
};

void to_json(nlohmann::json& j, const Universe& universe);
void from_json(const nlohmann::json& j, Universe& universe);
void to_json(nlohmann::json& j, const Window& window);
void from_json(const nlohmann::json& j, Window& window);
void to_json(nlohmann::json& j, const MergeSpec& merge);
void from_json(const nlohmann::json& j, MergeSpec& merge);
void to_json(nlohmann::json& j, const Objective& objective);
void from_json(const nlohmann::json& j, Objective& objective);
void to_json(nlohmann::json& j, const Artifacts& artifacts);
void from_json(const nlohmann::json& j, Artifacts& artifacts);
void to_json(nlohmann::json& j, const BacktestJobSpec& spec);
void from_json(const nlohmann::json& j, BacktestJobSpec& spec);
void to_json(nlohmann::json& j, const Counts& counts);
void from_json(const nlohmann::json& j, Counts& counts);
void to_json(nlohmann::json& j, const TrialResult& result);
void from_json(const nlohmann::json& j, TrialResult& result);
void to_json(nlohmann::json& j, const BacktestJobStatus& status);
void from_json(const nlohmann::json& j, BacktestJobStatus& status);
// Always writes "apiVersion": "quant.example.com/v1alpha1" and "kind": "BacktestJob".
void to_json(nlohmann::json& j, const BacktestJob& job);
void from_json(const nlohmann::json& j, BacktestJob& job);

}  // namespace k8sop::api

// ParamValue is an alias for a std::variant, so ADL looks in namespace std, not k8sop::api: a
// to_json(json&, const ParamValue&) next to the structs above would never be found. Instead we
// specialize nlohmann's serializer for the type. This must be visible before any code converts a
// ParamValue, which is why it lives in this header.
namespace nlohmann {
template <>
struct adl_serializer<k8sop::api::ParamValue> {
  static void to_json(json& j, const k8sop::api::ParamValue& value);
  static void from_json(const json& j, k8sop::api::ParamValue& value);
};
}  // namespace nlohmann
