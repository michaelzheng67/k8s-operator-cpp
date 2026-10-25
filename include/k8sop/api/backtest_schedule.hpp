#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "k8sop/api/backtest_job.hpp"
#include "k8sop/api/condition.hpp"
#include "k8sop/api/meta.hpp"

namespace k8sop::api {

// What to do when a tick fires while the previous job is still running.
enum class ConcurrencyPolicy : std::uint8_t { Forbid, Replace, Allow };
[[nodiscard]] std::string_view to_string(ConcurrencyPolicy policy) noexcept;

struct Alerts {
  // Absent means "no alert". std::optional because 0.0 is a legitimate threshold.
  std::optional<double> min_sharpe;  // "minSharpe"

  bool operator==(const Alerts&) const = default;
};

struct BacktestScheduleSpec {
  std::string schedule;                                              // "schedule", 5-field cron
  std::string time_zone = "UTC";                                     // "timeZone", IANA name
  std::string rolling_window;                                        // "rollingWindow", e.g. "252d"
  ConcurrencyPolicy concurrency_policy = ConcurrencyPolicy::Forbid;  // "concurrencyPolicy"
  std::int32_t history_limit = 30;                                   // "historyLimit"
  Alerts alerts;                                                     // "alerts"
  BacktestJobSpec job_template;  // JSON: "jobTemplate": {"spec": {...}}; window is filled per run

  bool operator==(const BacktestScheduleSpec&) const = default;
};

struct HistoryEntry {
  std::string job;                  // "job", name of the BacktestJob
  std::optional<TrialResult> best;  // "best", absent if the job produced no successful trial

  bool operator==(const HistoryEntry&) const = default;
};

struct BacktestScheduleStatus {
  std::optional<std::string> last_schedule_time;  // "lastScheduleTime", RFC 3339
  std::string last_successful_job;                // "lastSuccessfulJob"
  std::vector<HistoryEntry> history;              // "history", newest first
  std::vector<Condition> conditions;              // "conditions" (Degraded)

  bool operator==(const BacktestScheduleStatus&) const = default;
};

struct BacktestSchedule {
  ObjectMeta metadata;
  BacktestScheduleSpec spec;
  BacktestScheduleStatus status;

  bool operator==(const BacktestSchedule&) const = default;
};

void to_json(nlohmann::json& j, const Alerts& alerts);
void from_json(const nlohmann::json& j, Alerts& alerts);
void to_json(nlohmann::json& j, const BacktestScheduleSpec& spec);
void from_json(const nlohmann::json& j, BacktestScheduleSpec& spec);
void to_json(nlohmann::json& j, const HistoryEntry& entry);
void from_json(const nlohmann::json& j, HistoryEntry& entry);
void to_json(nlohmann::json& j, const BacktestScheduleStatus& status);
void from_json(const nlohmann::json& j, BacktestScheduleStatus& status);
// Always writes "apiVersion": "quant.example.com/v1alpha1" and "kind": "BacktestSchedule".
void to_json(nlohmann::json& j, const BacktestSchedule& schedule);
void from_json(const nlohmann::json& j, BacktestSchedule& schedule);

}  // namespace k8sop::api
