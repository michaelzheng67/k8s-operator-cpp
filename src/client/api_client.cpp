#include "k8sop/client/api_client.hpp"

#include <utility>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>

namespace k8sop::client {

namespace http = boost::beast::http;

namespace {

// Non-2xx responses carry a Status object: {"kind":"Status","reason":"NotFound","message":...}.
// If the body isn't one (a proxy's HTML error page, say), fall back to the HTTP reason phrase.
ApiError error_from(const Response& response) {
  ApiError error{.status = static_cast<int>(response.result_int()),
                 .reason = std::string(response.reason()),
                 .message = response.body()};
  const auto status = nlohmann::json::parse(response.body(), nullptr, /*allow_exceptions=*/false);
  if (status.is_object()) {
    error.reason = status.value("reason", error.reason);
    error.message = status.value("message", error.message);
  }
  return error;
}

}  // namespace

ApiClient::ApiClient(ClusterConfig config)
    : config_(std::move(config)), ssl_ctx_(make_ssl_context(config_)) {}

std::expected<Response, ApiError> ApiClient::request(http::verb verb, std::string_view target,
                                                     std::string body) {
  Request req{verb, target, 11};
  req.set(http::field::host, config_.host);
  req.set(http::field::accept, "application/json");
  req.set(http::field::user_agent, "k8s-operator-cpp");
  if (!config_.token.empty()) {
    req.set(http::field::authorization, "Bearer " + config_.token);
  }
  if (!body.empty()) {
    req.set(http::field::content_type, "application/json");
    req.body() = std::move(body);
  }
  req.keep_alive(true);
  req.prepare_payload();

  // A kept-alive connection may have been closed by the server while idle; that only shows up
  // when we use it. Retry once on a fresh connection in that case. A connection we just opened
  // failing is a real error, so it isn't retried here.
  const bool reused = conn_.has_value();
  auto response = send_once(req);
  if (!response && response.error().status == 0 && reused) {
    response = send_once(req);
  }
  return response;
}

std::expected<Response, ApiError> ApiClient::send_once(const Request& req) {
  if (!conn_) {
    auto conn = Connection::connect(io_, ssl_ctx_, config_.host, config_.port);
    if (!conn) {
      return std::unexpected(ApiError{.message = "connect: " + conn.error().message()});
    }
    conn_.emplace(std::move(*conn));
  }

  auto response = conn_->send(req);
  if (!response) {
    conn_.reset();  // the stream is in an unknown state; never reuse it
    return std::unexpected(ApiError{.message = "send: " + response.error().message()});
  }
  if (response->need_eof()) {
    conn_.reset();  // the server said Connection: close
  }
  if (http::to_status_class(response->result()) != http::status_class::successful) {
    return std::unexpected(error_from(*response));
  }
  return std::move(*response);
}

}  // namespace k8sop::client
