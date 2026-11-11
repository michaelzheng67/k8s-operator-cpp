#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http/verb.hpp>

#include "k8sop/api/list.hpp"
#include "k8sop/api/parse.hpp"
#include "k8sop/client/api_error.hpp"
#include "k8sop/client/config.hpp"
#include "k8sop/client/connection.hpp"
#include "k8sop/client/resource.hpp"

namespace k8sop::client {

// How a PATCH body is interpreted. See https://kubernetes.io/docs/tasks/manage-kubernetes-objects/update-api-object-kubectl-patch/
enum class PatchType : std::uint8_t {
  Merge,  // JSON merge patch (RFC 7386): the body is a partial object; null deletes a field
  Json,   // JSON patch (RFC 6902): a list of operations, e.g. [{"op":"replace","path":...}]
  Apply,  // server-side apply: the body is the full intent of this field manager
};

namespace detail {
// Adds "apiVersion" and "kind" to a serialized object; the API server rejects bodies without them.
std::string set_type_meta(std::string_view body, std::string_view api_version,
                          std::string_view kind);
}  // namespace detail

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

    template <Resource T>
  [[nodiscard]] std::expected<api::List<T>, ApiError> list(std::string_view ns,
                                                           std::string_view label_selector = "") {
    using Verb = boost::beast::http::verb;

    auto url = resource_path<T>(ns);
    if (!label_selector.empty()) {
      url += "?labelSelector=";
      url += url_encode(label_selector);
    }
    auto res = request(Verb::get, url);
    if (!res.has_value()) {
      return std::unexpected(res.error());
    }

    auto parse_res = api::parse<api::List<T>>(res->body());
    if (!parse_res.has_value()) {
      return std::unexpected(ApiError{
          .status = static_cast<int>(res->result_int()),
          .reason = "InvalidResponse",
          .message = parse_res.error().message,
      });
    }

    return std::move(*parse_res);
  }

  // POST the object to its namespace's collection. Returns the object as stored, with the
  // server-set fields filled in (uid, resourceVersion, creationTimestamp, ...).
  template <Resource T>
  [[nodiscard]] std::expected<T, ApiError> create(const T& object) {
    auto body = detail::set_type_meta(api::serialize(object), api_version<T>(),
                                      ResourceTraits<T>::kind);
    auto res = request(boost::beast::http::verb::post, resource_path<T>(object.metadata.namespace_),
                       std::move(body));
    if (!res) {
      return std::unexpected(res.error());
    }
    return parse_body<T>(*res);
  }

  // DELETE. Deletion is asynchronous: the object may linger with metadata.deletionTimestamp set
  // until finalizers and graceful shutdown complete. A 404 comes back as an error; callers that
  // want "delete if present" check error().status == 404.
  template <Resource T>
  [[nodiscard]] std::expected<void, ApiError> remove(std::string_view ns, std::string_view name) {
    auto res = request(boost::beast::http::verb::delete_, resource_path<T>(ns, name));
    if (!res) {
      return std::unexpected(res.error());
    }
    return {};
  }

  // PATCH the object. `body` is in the format `type` names.
  template <Resource T>
  [[nodiscard]] std::expected<T, ApiError> patch(std::string_view ns, std::string_view name,
                                                 std::string body, PatchType type) {
    return patch_path<T>(resource_path<T>(ns, name), std::move(body), type);
  }

  // PATCH the status subresource: the only way to change .status on a resource that has one, and
  // changes to anything but .status are ignored there.
  template <Resource T>
  [[nodiscard]] std::expected<T, ApiError> patch_status(std::string_view ns, std::string_view name,
                                                        std::string body, PatchType type) {
    return patch_path<T>(resource_path<T>(ns, name) + "/status", std::move(body), type);
  }

 private:
  // Parses a 2xx body. A body that doesn't match T is a permanent error (we and the server
  // disagree about the schema), so it keeps the HTTP status and isn't retryable.
  template <typename U>
  static std::expected<U, ApiError> parse_body(const Response& res) {
    auto parsed = api::parse<U>(res.body());
    if (!parsed) {
      return std::unexpected(ApiError{.status = static_cast<int>(res.result_int()),
                                      .reason = "InvalidResponse",
                                      .message = parsed.error().message});
    }
    return std::move(*parsed);
  }

  template <Resource T>
  std::expected<T, ApiError> patch_path(std::string target, std::string body, PatchType type) {
    if (type == PatchType::Apply) {
      target += "?fieldManager=k8s-operator-cpp&force=true";
    }
    auto res = request(boost::beast::http::verb::patch, target, std::move(body),
                       patch_content_type(type));
    if (!res) {
      return std::unexpected(res.error());
    }
    return parse_body<T>(*res);
  }

  static std::string_view patch_content_type(PatchType type) noexcept;

  // Sends one request over the kept-alive connection, connecting first if there isn't one.
  // Returns the response only for 2xx; any other status becomes an ApiError built from the
  // Kubernetes Status object in the body. Connection failures become ApiError{.status = 0}.
  std::expected<Response, ApiError> request(boost::beast::http::verb verb, std::string_view target,
                                            std::string body = {},
                                            std::string_view content_type = "application/json");

  std::expected<Response, ApiError> send_once(const Request& req);

  // Declaration order is lifetime order: conn_ refers to io_ and ssl_ctx_, so it's declared
  // last and destroyed first.
  ClusterConfig config_;
  boost::asio::io_context io_;
  boost::asio::ssl::context ssl_ctx_;
  std::optional<Connection> conn_;
};

}  // namespace k8sop::client
