#include "k8sop/api/parse.hpp"

#include <nlohmann/json.hpp>

#include "k8sop/api/backtest_job.hpp"
#include "k8sop/api/backtest_schedule.hpp"
#include "k8sop/api/detail/json_helpers.hpp"
#include "k8sop/api/list.hpp"
#include "k8sop/api/meta.hpp"
#include "k8sop/api/pod.hpp"

namespace k8sop::api {

void from_json(const nlohmann::json& j, ListMeta& meta) {
  detail::read_optional(j, "resourceVersion", meta.resource_version);
  detail::read_optional(j, "continue", meta.continue_token);
}

template <typename T>
void from_json(const nlohmann::json& j, List<T>& list) {
  detail::read_optional(j, "metadata", list.metadata);
  detail::read_optional(j, "items", list.items);  // absent (not []) when the list is empty
}

// Only nlohmann exceptions are converted to ParseError. Anything else (in practice, bad_alloc)
// hits noexcept and terminates: out of memory isn't recoverable, so crash and let the Pod restart.
template <typename T>
std::expected<T, ParseError> parse(std::string_view body) noexcept {
  try {
    return nlohmann::json::parse(body).get<T>();
  } catch (const nlohmann::json::parse_error& e) {
    return std::unexpected(
        ParseError{.kind = ParseError::Kind::Syntax, .message = e.what(), .byte = e.byte});
  } catch (const nlohmann::json::type_error& e) {
    return std::unexpected(ParseError{.kind = ParseError::Kind::Type, .message = e.what()});
  } catch (const nlohmann::json::out_of_range& e) {
    return std::unexpected(ParseError{.kind = ParseError::Kind::Missing, .message = e.what()});
  } catch (const nlohmann::json::exception& e) {
    return std::unexpected(ParseError{.kind = ParseError::Kind::Other, .message = e.what()});
  }
}

template <typename T>
std::string serialize(const T& object) {
  return nlohmann::json(object).dump();
}

// Explicit instantiations: compile parse<T> and serialize<T> for these types here, once, so other
// files can call them knowing only the declarations in parse.hpp.
template std::expected<ObjectMeta, ParseError> parse<ObjectMeta>(std::string_view) noexcept;
template std::expected<Pod, ParseError> parse<Pod>(std::string_view) noexcept;
template std::expected<BacktestJob, ParseError> parse<BacktestJob>(std::string_view) noexcept;
template std::expected<BacktestSchedule, ParseError> parse<BacktestSchedule>(
    std::string_view) noexcept;

template std::string serialize<ObjectMeta>(const ObjectMeta&);
template std::string serialize<Pod>(const Pod&);
template std::string serialize<BacktestJob>(const BacktestJob&);
template std::string serialize<BacktestSchedule>(const BacktestSchedule&);

}  // namespace k8sop::api
