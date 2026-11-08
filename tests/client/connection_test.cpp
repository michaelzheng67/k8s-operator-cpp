// Integration tests against a real API server. They need a cluster in the current kubeconfig
// context (`kind create cluster --name bt`) and run only when K8SOP_INTEGRATION=1, so a plain
// `ctest` on a machine with no cluster skips them instead of failing.

#include "k8sop/client/connection.hpp"

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

#include <boost/asio/ssl/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <gtest/gtest.h>

#include "k8sop/client/config.hpp"
#include "test_certs.hpp"

namespace k8sop::client {
namespace {

namespace http = boost::beast::http;

// The design: a unique resource that can change owners.
static_assert(!std::is_copy_constructible_v<Connection>);
static_assert(!std::is_copy_assignable_v<Connection>);
static_assert(std::is_nothrow_move_constructible_v<Connection>);
static_assert(std::is_nothrow_move_assignable_v<Connection>);

class ConnectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* flag = std::getenv("K8SOP_INTEGRATION");
    if (flag == nullptr || std::string_view(flag) != "1") {
      GTEST_SKIP() << "set K8SOP_INTEGRATION=1 to run against the kubeconfig's cluster";
    }
    auto loaded = load_default_kubeconfig();
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    config_ = std::move(*loaded);
  }

  Request get(std::string_view target) const {
    Request request{http::verb::get, target, 11};
    request.set(http::field::host, config_.host);
    return request;
  }

  boost::asio::io_context io_;
  ClusterConfig config_;
};

TEST_F(ConnectionTest, GetVersion) {
  auto ssl_ctx = make_ssl_context(config_);
  auto conn = Connection::connect(io_, ssl_ctx, config_.host, config_.port);
  ASSERT_TRUE(conn.has_value()) << conn.error().message();

  const auto response = conn->send(get("/version"));
  ASSERT_TRUE(response.has_value()) << response.error().message();
  EXPECT_EQ(response->result(), http::status::ok);
  EXPECT_NE(response->body().find("gitVersion"), std::string::npos);
}

TEST_F(ConnectionTest, KeepAliveServesSeveralRequests) {
  auto ssl_ctx = make_ssl_context(config_);
  auto conn = Connection::connect(io_, ssl_ctx, config_.host, config_.port);
  ASSERT_TRUE(conn.has_value()) << conn.error().message();

  for (const char* target : {"/version", "/api/v1/namespaces/default/pods", "/version"}) {
    SCOPED_TRACE(target);
    const auto response = conn->send(get(target));
    ASSERT_TRUE(response.has_value()) << response.error().message();
    EXPECT_EQ(response->result(), http::status::ok);
  }
}

TEST_F(ConnectionTest, MovedConnectionStillWorks) {
  auto ssl_ctx = make_ssl_context(config_);
  auto conn = Connection::connect(io_, ssl_ctx, config_.host, config_.port);
  ASSERT_TRUE(conn.has_value()) << conn.error().message();

  Connection moved = std::move(*conn);
  const auto response = moved.send(get("/version"));
  ASSERT_TRUE(response.has_value()) << response.error().message();
  EXPECT_EQ(response->result(), http::status::ok);
}

TEST_F(ConnectionTest, CloseIsClean) {
  auto ssl_ctx = make_ssl_context(config_);
  auto conn = Connection::connect(io_, ssl_ctx, config_.host, config_.port);
  ASSERT_TRUE(conn.has_value()) << conn.error().message();
  ASSERT_TRUE(conn->send(get("/version")).has_value());
  EXPECT_FALSE(conn->close()) << conn->close().message();
}

// A CA that didn't sign the server's certificate must fail the handshake: this is the check that
// stops us from sending credentials to an impostor.
TEST_F(ConnectionTest, UntrustedServerIsRejected) {
  ClusterConfig wrong_ca = config_;
  wrong_ca.ca_pem = testing::make_self_signed("not-the-cluster-ca").cert_pem;
  auto ssl_ctx = make_ssl_context(wrong_ca);
  const auto conn = Connection::connect(io_, ssl_ctx, config_.host, config_.port);
  EXPECT_FALSE(conn.has_value());
}

}  // namespace
}  // namespace k8sop::client
