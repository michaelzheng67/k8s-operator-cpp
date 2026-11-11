#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http/verb.hpp>

#include "k8sop/api/parse.hpp"
#include "k8sop/client/api_error.hpp"
#include "k8sop/client/config.hpp"
#include "k8sop/client/connection.hpp"
#include "k8sop/client/resource.hpp"

namespace k8sop::client {

// Typed access to the Kubernetes API: builds the URL, sends the request, checks the status, and
// parses the body. Blocking, one request at a time, not thread-safe.
class ApiClient {
 public:
  explicit ApiClient(ClusterConfig config);

  template <Resource T>
  [[nodiscard]] std::expected<T, ApiError> get(std::string_view ns, std::string_view name) {
    using Verb = boost::beast::http::verb;

    auto res = request(Verb::get, resource_path<T>(ns, name));
    if (!res.has_value()) {
      return std::unexpected(res.error());
    }

    auto parse_res = api::parse<T>(res->body());
    if (!parse_res.has_value()) {
      return std::unexpected(ApiError{
          .status = static_cast<int>(res->result_int()),
          .reason = "InvalidResponse",
          .message = parse_res.error().message,
      });
    }

    return std::move(*parse_res);
  }

 private:
  // Sends one request over the kept-alive connection, connecting first if there isn't one.
  // Returns the response only for 2xx; any other status becomes an ApiError built from the
  // Kubernetes Status object in the body. Connection failures become ApiError{.status = 0}.
  std::expected<Response, ApiError> request(boost::beast::http::verb verb, std::string_view target,
                                            std::string body = {});

  std::expected<Response, ApiError> send_once(const Request& req);

  // Declaration order is lifetime order: conn_ refers to io_ and ssl_ctx_, so it's declared
  // last and destroyed first.
  ClusterConfig config_;
  boost::asio::io_context io_;
  boost::asio::ssl::context ssl_ctx_;
  std::optional<Connection> conn_;
};

}  // namespace k8sop::client
