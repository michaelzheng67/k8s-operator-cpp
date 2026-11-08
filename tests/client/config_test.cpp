#include "k8sop/client/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>
#include <unistd.h>

#include "test_certs.hpp"

namespace k8sop::client {
namespace {

namespace fs = std::filesystem;

void write(const fs::path& path, const std::string& contents) {
  std::ofstream(path, std::ios::binary) << contents;
}

// Writes kubeconfigs and a fake service account directory into a temp dir, using a certificate
// generated for this run.
class ConfigTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    cert_ = testing::make_self_signed("k8sop-test");
    dir_ = fs::temp_directory_path() / ("k8sop-config-test-" + std::to_string(::getpid()));
    fs::create_directories(dir_ / "serviceaccount");

    write(dir_ / "ca.crt", cert_.cert_pem);
    write(dir_ / "inline.yaml", R"(apiVersion: v1
kind: Config
current-context: test
contexts:
- name: other
  context: {cluster: other, user: other}
- name: test
  context: {cluster: test-cluster, user: test-user, namespace: backtests}
clusters:
- name: other
  cluster: {server: "https://10.0.0.1:6443"}
- name: test-cluster
  cluster:
    server: https://127.0.0.1:50349
    certificate-authority-data: )" + testing::base64_encode(cert_.cert_pem) + R"(
users:
- name: test-user
  user:
    client-certificate-data: )" + testing::base64_encode(cert_.cert_pem) + R"(
    client-key-data: )" + testing::base64_encode(cert_.key_pem) + "\n");
    write(dir_ / "files.yaml", R"(apiVersion: v1
kind: Config
current-context: test
contexts:
- name: test
  context: {cluster: test-cluster, user: test-user}
clusters:
- name: test-cluster
  cluster:
    server: https://kube.example.com
    certificate-authority: ca.crt
users:
- name: test-user
  user:
    token: abc123
)");
    write(dir_ / "serviceaccount" / "ca.crt", cert_.cert_pem);
    write(dir_ / "serviceaccount" / "token", "sa-token");
    write(dir_ / "serviceaccount" / "namespace", "backtests");
  }

  static void TearDownTestSuite() {
    std::error_code ec;
    fs::remove_all(dir_, ec);
  }

  static inline testing::TestCertificate cert_;
  static inline fs::path dir_;
};

TEST_F(ConfigTest, InlineDataUsesCurrentContext) {
  const auto config = load_kubeconfig(dir_ / "inline.yaml");
  ASSERT_TRUE(config.has_value()) << config.error().message;
  EXPECT_EQ(config->host, "127.0.0.1");
  EXPECT_EQ(config->port, "50349");
  EXPECT_EQ(config->namespace_, "backtests");
  EXPECT_EQ(config->ca_pem, cert_.cert_pem);
  EXPECT_EQ(config->client_cert_pem, cert_.cert_pem);
  EXPECT_EQ(config->client_key_pem, cert_.key_pem);
  EXPECT_TRUE(config->token.empty());
}

TEST_F(ConfigTest, FilePathsAndTokenAndDefaults) {
  const auto config = load_kubeconfig(dir_ / "files.yaml");
  ASSERT_TRUE(config.has_value()) << config.error().message;
  EXPECT_EQ(config->host, "kube.example.com");
  EXPECT_EQ(config->port, "443");
  EXPECT_EQ(config->namespace_, "default");
  EXPECT_EQ(config->ca_pem, cert_.cert_pem);  // read relative to the kubeconfig's directory
  EXPECT_EQ(config->token, "abc123");
  EXPECT_TRUE(config->client_cert_pem.empty());
}

TEST_F(ConfigTest, MissingFileIsError) {
  const auto config = load_kubeconfig(dir_ / "does-not-exist.yaml");
  ASSERT_FALSE(config.has_value());
  EXPECT_NE(config.error().message.find("does-not-exist.yaml"), std::string::npos);
}

TEST_F(ConfigTest, InClusterReadsServiceAccount) {
  ::setenv("KUBERNETES_SERVICE_HOST", "10.96.0.1", 1);
  ::setenv("KUBERNETES_SERVICE_PORT", "443", 1);
  const auto config = load_in_cluster(dir_ / "serviceaccount");
  ::unsetenv("KUBERNETES_SERVICE_HOST");
  ::unsetenv("KUBERNETES_SERVICE_PORT");

  ASSERT_TRUE(config.has_value()) << config.error().message;
  EXPECT_EQ(config->host, "10.96.0.1");
  EXPECT_EQ(config->token, "sa-token");
  EXPECT_EQ(config->namespace_, "backtests");
  EXPECT_EQ(config->ca_pem, cert_.cert_pem);
}

TEST_F(ConfigTest, InClusterOutsideAPodIsError) {
  ::unsetenv("KUBERNETES_SERVICE_HOST");
  EXPECT_FALSE(load_in_cluster(dir_ / "serviceaccount").has_value());
}

TEST_F(ConfigTest, SslContextLoadsCaAndClientCertificate) {
  const auto config = load_kubeconfig(dir_ / "inline.yaml");
  ASSERT_TRUE(config.has_value()) << config.error().message;
  EXPECT_NO_THROW({ auto ctx = make_ssl_context(*config); });
}

TEST_F(ConfigTest, SslContextMalformedPemThrows) {
  const ClusterConfig config{.ca_pem = "not a certificate"};
  EXPECT_THROW({ auto ctx = make_ssl_context(config); }, boost::system::system_error);
}

}  // namespace
}  // namespace k8sop::client
