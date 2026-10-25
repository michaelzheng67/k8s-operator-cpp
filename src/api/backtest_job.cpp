#include "k8sop/api/backtest_job.hpp"

#include <array>
#include <cassert>
#include <charconv>
#include <system_error>
#include <type_traits>
#include <variant>

#include "k8sop/api/detail/enum_table.hpp"
#include "k8sop/api/detail/json_helpers.hpp"

namespace k8sop::api {

using detail::EnumTable;
using detail::read_enum;
using detail::read_optional;
using detail::write_if_set;
using nlohmann::json;

namespace {

constexpr EnumTable<Goal, 2> kGoals{{
    {Goal::Maximize, "maximize"},
    {Goal::Minimize, "minimize"},
}};

constexpr EnumTable<JobPhase, 4> kJobPhases{{
    {JobPhase::Pending, "Pending"},
    {JobPhase::Running, "Running"},
    {JobPhase::Succeeded, "Succeeded"},
    {JobPhase::Failed, "Failed"},
}};

}  // namespace

std::string to_string(const ParamValue& value) {
  return std::visit(
      [](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return v;
        } else {
          std::array<char, 32> buf{};
          auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
          assert(ec == std::errc{} && "32 bytes fits any int64_t or shortest-form double");
          return std::string{buf.data(), ptr};
        }
      },
      value);
}

std::string_view to_string(Goal goal) noexcept { return detail::enum_to_string(kGoals, goal); }

std::string_view to_string(JobPhase phase) noexcept {
  return detail::enum_to_string(kJobPhases, phase);
}

void to_json(json& j, const Universe& universe) {
  j = json::object();
  write_if_set(j, "symbols", universe.symbols);
  write_if_set(j, "shardsPerTrial", universe.shards_per_trial);
}

void from_json(const json& j, Universe& universe) {
  read_optional(j, "symbols", universe.symbols);
  read_optional(j, "shardsPerTrial", universe.shards_per_trial);
}

void to_json(json& j, const Window& window) {
  j = json::object();
  write_if_set(j, "start", window.start);
  write_if_set(j, "end", window.end);
}

void from_json(const json& j, Window& window) {
  read_optional(j, "start", window.start);
  read_optional(j, "end", window.end);
}

void to_json(json& j, const MergeSpec& merge) {
  j = json::object();
  write_if_set(j, "image", merge.image);
}

void from_json(const json& j, MergeSpec& merge) { read_optional(j, "image", merge.image); }

void to_json(json& j, const Objective& objective) {
  j = {{"goal", to_string(objective.goal)}};
  write_if_set(j, "metric", objective.metric);
  write_if_set(j, "topK", objective.top_k);
}

void from_json(const json& j, Objective& objective) {
  read_optional(j, "metric", objective.metric);
  if (auto it = j.find("goal"); it != j.end()) {
    objective.goal = read_enum(*it, kGoals, "goal");
  }
  read_optional(j, "topK", objective.top_k);
}

void to_json(json& j, const Artifacts& artifacts) {
  j = json::object();
  write_if_set(j, "uri", artifacts.uri);
}

void from_json(const json& j, Artifacts& artifacts) { read_optional(j, "uri", artifacts.uri); }

void to_json(json& j, const BacktestJobSpec& spec) {
  j = json::object();
  write_if_set(j, "strategy", spec.strategy);
  write_if_set(j, "image", spec.image);
  write_if_set(j, "universe", spec.universe);
  write_if_set(j, "window", spec.window);
  write_if_set(j, "parameters", spec.parameters);
  write_if_set(j, "merge", spec.merge);
  write_if_set(j, "objective", spec.objective);
  write_if_set(j, "maxConcurrentPods", spec.max_concurrent_pods);
  write_if_set(j, "maxRetries", spec.max_retries);
  write_if_set(j, "artifacts", spec.artifacts);
}

void from_json(const json& j, BacktestJobSpec& spec) {
  read_optional(j, "strategy", spec.strategy);
  read_optional(j, "image", spec.image);
  read_optional(j, "universe", spec.universe);
  read_optional(j, "window", spec.window);
  read_optional(j, "parameters", spec.parameters);
  read_optional(j, "merge", spec.merge);
  read_optional(j, "objective", spec.objective);
  read_optional(j, "maxConcurrentPods", spec.max_concurrent_pods);
  read_optional(j, "maxRetries", spec.max_retries);
  read_optional(j, "artifacts", spec.artifacts);
}

void to_json(json& j, const Counts& counts) {
  j = json::object();
  write_if_set(j, "total", counts.total);
  write_if_set(j, "succeeded", counts.succeeded);
  write_if_set(j, "running", counts.running);
  write_if_set(j, "pending", counts.pending);
  write_if_set(j, "failed", counts.failed);
}

void from_json(const json& j, Counts& counts) {
  read_optional(j, "total", counts.total);
  read_optional(j, "succeeded", counts.succeeded);
  read_optional(j, "running", counts.running);
  read_optional(j, "pending", counts.pending);
  read_optional(j, "failed", counts.failed);
}

void to_json(json& j, const TrialResult& result) {
  j = {{"trial", result.trial}};  // always written: trial 0 is a real trial
  write_if_set(j, "parameters", result.parameters);
  write_if_set(j, "metrics", result.metrics);
}

void from_json(const json& j, TrialResult& result) {
  read_optional(j, "trial", result.trial);
  read_optional(j, "parameters", result.parameters);
  read_optional(j, "metrics", result.metrics);
}

void to_json(json& j, const BacktestJobStatus& status) {
  j = {{"phase", to_string(status.phase)}};
  write_if_set(j, "trials", status.trials);
  write_if_set(j, "shards", status.shards);
  write_if_set(j, "best", status.best);
  write_if_set(j, "resultsURI", status.results_uri);
  write_if_set(j, "conditions", status.conditions);
}

void from_json(const json& j, BacktestJobStatus& status) {
  if (auto it = j.find("phase"); it != j.end()) {
    status.phase = read_enum(*it, kJobPhases, "job phase");
  }
  read_optional(j, "trials", status.trials);
  read_optional(j, "shards", status.shards);
  read_optional(j, "best", status.best);
  read_optional(j, "resultsURI", status.results_uri);
  read_optional(j, "conditions", status.conditions);
}

void to_json(json& j, const BacktestJob& job) {
  j = {{"apiVersion", kQuantApiVersion}, {"kind", "BacktestJob"}};
  write_if_set(j, "metadata", job.metadata);
  write_if_set(j, "spec", job.spec);
  write_if_set(j, "status", job.status);
}

void from_json(const json& j, BacktestJob& job) {
  read_optional(j, "metadata", job.metadata);
  read_optional(j, "spec", job.spec);
  read_optional(j, "status", job.status);
}

}  // namespace k8sop::api

namespace nlohmann {

void adl_serializer<k8sop::api::ParamValue>::to_json(json& j, const k8sop::api::ParamValue& value) {
  std::visit([&j](const auto& v) { j = v; }, value);
}

void adl_serializer<k8sop::api::ParamValue>::from_json(const json& j,
                                                       k8sop::api::ParamValue& value) {
  if (j.is_number_integer()) {
    value = j.get<std::int64_t>();
  } else if (j.is_number_float()) {
    value = j.get<double>();
  } else if (j.is_string()) {
    value = j.get<std::string>();
  } else {
    k8sop::api::detail::throw_type_error(j, "parameter values must be numbers or strings");
  }
}

}  // namespace nlohmann
