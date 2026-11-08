#pragma once

#include <expected>
#include <filesystem>
#include <string>

#include <boost/asio/ssl/context.hpp>

namespace k8sop::client {

// Everything needed to open an authenticated connection to one API server. PEM fields hold the
// decoded certificate text ("-----BEGIN CERTIFICATE-----..."), not base64 or file paths.
struct ClusterConfig {
  std::string host;             // "127.0.0.1"
  std::string port;             // "50349"; a string because that's what the resolver takes
  std::string namespace_;       // the context's default namespace, "default" if unset
  std::string ca_pem;           // verifies the server's certificate
  std::string client_cert_pem;  // client-certificate auth (kind, minikube) ...
  std::string client_key_pem;
  std::string token;            // ... or bearer-token auth (in-cluster service accounts)

  bool operator==(const ClusterConfig&) const = default;
};

struct ConfigError {
  std::string message;
};

// Reads the current context of a kubeconfig file. Inline `*-data` fields and file paths (relative
// to the kubeconfig's directory) are both supported.
[[nodiscard]] std::expected<ClusterConfig, ConfigError> load_kubeconfig(
    const std::filesystem::path& path);

// $KUBECONFIG if set (first entry only), else ~/.kube/config.
[[nodiscard]] std::expected<ClusterConfig, ConfigError> load_default_kubeconfig();

// Inside a Pod: KUBERNETES_SERVICE_HOST/PORT plus the mounted service account token and CA.
// `root` exists so tests can point it at a fake directory.
[[nodiscard]] std::expected<ClusterConfig, ConfigError> load_in_cluster(
    const std::filesystem::path& root = "/var/run/secrets/kubernetes.io/serviceaccount");

// Builds the TLS settings shared by every connection to this cluster: trust only `ca_pem` for
// the server, and present the client certificate if there is one. Throws boost::system::
// system_error on malformed PEM; it runs once at startup, where failing loudly is what we want.
[[nodiscard]] boost::asio::ssl::context make_ssl_context(const ClusterConfig& config);

}  // namespace k8sop::client
