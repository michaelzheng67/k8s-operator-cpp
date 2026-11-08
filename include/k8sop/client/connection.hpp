#pragma once

#include <expected>
#include <memory>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/system/error_code.hpp>

namespace k8sop::client {

using Request = boost::beast::http::request<boost::beast::http::string_body>;
using Response = boost::beast::http::response<boost::beast::http::string_body>;

// One live HTTPS connection (TCP + TLS session) to the API server. Blocking.
//
// Owns only the session. The io_context and ssl::context are shared by every connection to the
// cluster and belong to whoever creates connections; both must outlive the Connection.
class Connection {
 public:
  using Stream = boost::asio::ssl::stream<boost::beast::tcp_stream>;

  // Resolves host:port, connects TCP, and runs the TLS handshake, verifying that the server's
  // certificate chains to the CA in `ssl_ctx` and was issued for `host`.
  [[nodiscard]] static std::expected<Connection, boost::system::error_code> connect(
      boost::asio::io_context& io, boost::asio::ssl::context& ssl_ctx, const std::string& host,
      const std::string& port);

  Connection(const Connection& other) = delete;
  Connection& operator=(const Connection& other) = delete;
  Connection(Connection&& other) = default;
  Connection& operator=(Connection&& other) = default;
  ~Connection() = default;

  // Writes `request` and reads one complete response. The connection stays open for the next
  // request if the server allows keep-alive.
  [[nodiscard]] std::expected<Response, boost::system::error_code> send(const Request& request);

  // Graceful TLS shutdown (close_notify). Returns success if the server closed without replying
  // (stream_truncated), which HTTP's own framing makes harmless. The Connection is unusable
  // afterwards either way.
  boost::system::error_code close();

 private:
  explicit Connection(std::unique_ptr<Stream> stream);

  // Behind a pointer because the stream's own moves are no use to us: move assignment is deleted
  // (tcp_stream's is) and move construction isn't noexcept. Moving the pointer is both.
  std::unique_ptr<Stream> stream_;
  boost::beast::flat_buffer buffer_;
};

}  // namespace k8sop::client
