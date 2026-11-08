#include "k8sop/client/connection.hpp"

#include <utility>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

namespace k8sop::client {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
using boost::system::error_code;

std::expected<Connection, error_code> Connection::connect(asio::io_context& io,
                                                          asio::ssl::context& ssl_ctx,
                                                          const std::string& host,
                                                          const std::string& port) {
  error_code ec;

  asio::ip::tcp::resolver resolver{io};
  const auto results = resolver.resolve(host, port, ec);
  if (ec) {
    return std::unexpected(ec);
  }

  auto stream = std::make_unique<Stream>(io, ssl_ctx);

  stream->set_verify_callback(asio::ssl::host_name_verification(host), ec);
  if (ec) {
    return std::unexpected(ec);
  }

  beast::get_lowest_layer(*stream).connect(results, ec);
  if (ec) {
    return std::unexpected(ec);
  }

  stream->handshake(asio::ssl::stream_base::client, ec);
  if (ec) {
    return std::unexpected(ec);
  }

  return Connection(std::move(stream));
}

Connection::Connection(std::unique_ptr<Stream> stream) : stream_(std::move(stream)) {}

std::expected<Response, error_code> Connection::send(const Request& request) {
  error_code ec;
  http::write(*stream_, request, ec);
  if (ec) {
    return std::unexpected(ec);
  }

  Response response;
  http::read(*stream_, buffer_, response, ec);
  if (ec) {
    return std::unexpected(ec);
  }

  return response;
}

error_code Connection::close() {
  error_code ec;
  stream_->shutdown(ec);

  // case where server closes TCP without replying
  if (ec == asio::ssl::error::stream_truncated) {
    return {};
  }
  return ec;
}

}  // namespace k8sop::client
