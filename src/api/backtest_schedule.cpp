#include "k8sop/api/backtest_schedule.hpp"

#include "k8sop/api/detail/enum_table.hpp"
#include "k8sop/api/detail/json_helpers.hpp"

namespace k8sop::api {

using detail::EnumTable;
using detail::read_enum;
using detail::read_optional;
using detail::write_if_set;
using nlohmann::json;

namespace {

constexpr EnumTable<ConcurrencyPolicy, 3> kConcurrencyPolicies{{
    {ConcurrencyPolicy::Forbid, "Forbid"},
    {ConcurrencyPolicy::Replace, "Replace"},
    {ConcurrencyPolicy::Allow, "Allow"},
}};

}  // namespace

std::string_view to_string(ConcurrencyPolicy policy) noexcept {
  return detail::enum_to_string(kConcurrencyPolicies, policy);
}

void to_json(json& j, const Alerts& alerts) {
  j = json::object();
  write_if_set(j, "minSharpe", alerts.min_sharpe);
}

void from_json(const json& j, Alerts& alerts) { read_optional(j, "minSharpe", alerts.min_sharpe); }

void to_json(json& j, const BacktestScheduleSpec& spec) {
  j = {{"concurrencyPolicy", to_string(spec.concurrency_policy)}};
  write_if_set(j, "schedule", spec.schedule);
  write_if_set(j, "timeZone", spec.time_zone);
  write_if_set(j, "rollingWindow", spec.rolling_window);
  write_if_set(j, "historyLimit", spec.history_limit);
  write_if_set(j, "alerts", spec.alerts);
  if (spec.job_template != BacktestJobSpec{}) {
    j["jobTemplate"]["spec"] = spec.job_template;
  }
}

void from_json(const json& j, BacktestScheduleSpec& spec) {
  read_optional(j, "schedule", spec.schedule);
  read_optional(j, "timeZone", spec.time_zone);
  read_optional(j, "rollingWindow", spec.rolling_window);
  if (auto it = j.find("concurrencyPolicy"); it != j.end()) {
    spec.concurrency_policy = read_enum(*it, kConcurrencyPolicies, "concurrency policy");
  }
  read_optional(j, "historyLimit", spec.history_limit);
  read_optional(j, "alerts", spec.alerts);
  if (auto tmpl = j.find("jobTemplate"); tmpl != j.end()) {
    read_optional(*tmpl, "spec", spec.job_template);
  }
}

void to_json(json& j, const HistoryEntry& entry) {
  j = json::object();
  write_if_set(j, "job", entry.job);
  write_if_set(j, "best", entry.best);
}

void from_json(const json& j, HistoryEntry& entry) {
  read_optional(j, "job", entry.job);
  read_optional(j, "best", entry.best);
}

void to_json(json& j, const BacktestScheduleStatus& status) {
  j = json::object();
  write_if_set(j, "lastScheduleTime", status.last_schedule_time);
  write_if_set(j, "lastSuccessfulJob", status.last_successful_job);
  write_if_set(j, "history", status.history);
  write_if_set(j, "conditions", status.conditions);
}

void from_json(const json& j, BacktestScheduleStatus& status) {
  read_optional(j, "lastScheduleTime", status.last_schedule_time);
  read_optional(j, "lastSuccessfulJob", status.last_successful_job);
  read_optional(j, "history", status.history);
  read_optional(j, "conditions", status.conditions);
}

void to_json(json& j, const BacktestSchedule& schedule) {
  j = {{"apiVersion", kQuantApiVersion}, {"kind", "BacktestSchedule"}};
  write_if_set(j, "metadata", schedule.metadata);
  write_if_set(j, "spec", schedule.spec);
  write_if_set(j, "status", schedule.status);
}

void from_json(const json& j, BacktestSchedule& schedule) {
  read_optional(j, "metadata", schedule.metadata);
  read_optional(j, "spec", schedule.spec);
  read_optional(j, "status", schedule.status);
}

}  // namespace k8sop::api
