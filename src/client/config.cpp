#include "k8sop/client/config.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

#include <boost/asio/buffer.hpp>
#include <yaml-cpp/yaml.h>

namespace k8sop::client {
namespace {

std::unexpected<ConfigError> fail(std::string message) {
  return std::unexpected(ConfigError{std::move(message)});
}

std::optional<std::string> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Standard base64 (RFC 4648), as used by kubeconfig's *-data fields. Whitespace is skipped.
std::optional<std::string> base64_decode(std::string_view in) {
  constexpr auto kTable = [] {
    std::array<std::int8_t, 256> table{};
    table.fill(-1);
    constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t i = 0; i < kAlphabet.size(); ++i) {
      table[static_cast<unsigned char>(kAlphabet[i])] = static_cast<std::int8_t>(i);
    }
    return table;
  }();

  std::string out;
  out.reserve(in.size() / 4 * 3);
  std::uint32_t bits = 0;
  int bit_count = 0;
  for (const char c : in) {
    if (c == '=') {
      break;
    }
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
      continue;
    }
    const std::int8_t value = kTable[static_cast<unsigned char>(c)];
    if (value < 0) {
      return std::nullopt;
    }
    bits = (bits << 6U) | static_cast<std::uint32_t>(value);
    bit_count += 6;
    if (bit_count >= 8) {
      bit_count -= 8;
      out.push_back(static_cast<char>((bits >> static_cast<unsigned>(bit_count)) & 0xFFU));
    }
  }
  return out;
}

// Finds the entry named `name` in a kubeconfig list like `clusters: [{name: x, cluster: {...}}]`
// and returns its `field` ("cluster", "user", "context").
YAML::Node find_named(const YAML::Node& list, const std::string& name, const char* field) {
  for (const auto& entry : list) {
    if (entry["name"] && entry["name"].as<std::string>() == name) {
      return entry[field];
    }
  }
  return {};
}

// Reads a credential that may be inline (`<key>-data`, base64) or a file path (`<key>`).
// Returns "" when neither is present; an error when one is present but unreadable.
std::expected<std::string, ConfigError> read_credential(const YAML::Node& node,
                                                        const std::string& key,
                                                        const std::filesystem::path& base_dir) {
  if (const auto data = node[key + "-data"]) {
    auto decoded = base64_decode(data.as<std::string>());
    if (!decoded) {
      return fail(key + "-data is not valid base64");
    }
    return std::move(*decoded);
  }
  if (const auto file = node[key]) {
    std::filesystem::path path = file.as<std::string>();
    if (path.is_relative()) {
      path = base_dir / path;
    }
    auto contents = read_file(path);
    if (!contents) {
      return fail("cannot read " + key + " file " + path.string());
    }
    return std::move(*contents);
  }
  return std::string{};
}

// "https://127.0.0.1:50349" -> host "127.0.0.1", port "50349". Port defaults to 443.
std::expected<std::pair<std::string, std::string>, ConfigError> split_server(
    std::string_view server) {
  constexpr std::string_view kScheme = "https://";
  if (!server.starts_with(kScheme)) {
    return fail("server must be an https:// URL, got " + std::string(server));
  }
  server.remove_prefix(kScheme.size());
  if (const auto slash = server.find('/'); slash != std::string_view::npos) {
    server = server.substr(0, slash);
  }
  if (const auto colon = server.rfind(':'); colon != std::string_view::npos) {
    return std::pair{std::string(server.substr(0, colon)), std::string(server.substr(colon + 1))};
  }
  return std::pair{std::string(server), std::string("443")};
}

}  // namespace

std::expected<ClusterConfig, ConfigError> load_kubeconfig(const std::filesystem::path& path) {
  try {
    const YAML::Node root = YAML::LoadFile(path.string());
    const auto context_name = root["current-context"].as<std::string>("");
    if (context_name.empty()) {
      return fail("no current-context in " + path.string());
    }
    const YAML::Node context = find_named(root["contexts"], context_name, "context");
    if (!context) {
      return fail("context " + context_name + " not found");
    }
    const auto cluster_name = context["cluster"].as<std::string>("");
    const auto user_name = context["user"].as<std::string>("");
    const YAML::Node cluster = find_named(root["clusters"], cluster_name, "cluster");
    const YAML::Node user = find_named(root["users"], user_name, "user");
    if (!cluster) {
      return fail("cluster " + cluster_name + " not found");
    }

    ClusterConfig config;
    auto server = split_server(cluster["server"].as<std::string>(""));
    if (!server) {
      return std::unexpected(server.error());
    }
    std::tie(config.host, config.port) = std::move(*server);
    config.namespace_ = context["namespace"].as<std::string>("default");

    const auto base_dir = path.parent_path();
    auto ca = read_credential(cluster, "certificate-authority", base_dir);
    if (!ca) {
      return std::unexpected(ca.error());
    }
    config.ca_pem = std::move(*ca);

    if (user) {
      auto cert = read_credential(user, "client-certificate", base_dir);
      auto key = read_credential(user, "client-key", base_dir);
      if (!cert) {
        return std::unexpected(cert.error());
      }
      if (!key) {
        return std::unexpected(key.error());
      }
      config.client_cert_pem = std::move(*cert);
      config.client_key_pem = std::move(*key);
      config.token = user["token"].as<std::string>("");
    }
    return config;
  } catch (const YAML::Exception& e) {
    return fail("cannot parse kubeconfig " + path.string() + ": " + e.what());
  }
}

std::expected<ClusterConfig, ConfigError> load_default_kubeconfig() {
  if (const char* env = std::getenv("KUBECONFIG"); env != nullptr && *env != '\0') {
    const std::string_view paths = env;
    return load_kubeconfig(std::string(paths.substr(0, paths.find(':'))));
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr) {
    return fail("neither KUBECONFIG nor HOME is set");
  }
  return load_kubeconfig(std::filesystem::path(home) / ".kube" / "config");
}

std::expected<ClusterConfig, ConfigError> load_in_cluster(const std::filesystem::path& root) {
  const char* host = std::getenv("KUBERNETES_SERVICE_HOST");
  const char* port = std::getenv("KUBERNETES_SERVICE_PORT");
  if (host == nullptr || port == nullptr) {
    return fail("KUBERNETES_SERVICE_HOST/PORT not set: not running in a Pod");
  }
  auto token = read_file(root / "token");
  auto ca = read_file(root / "ca.crt");
  if (!token || !ca) {
    return fail("cannot read service account token or CA under " + root.string());
  }
  auto ns = read_file(root / "namespace");
  return ClusterConfig{.host = host,
                       .port = port,
                       .namespace_ = ns ? std::move(*ns) : "default",
                       .ca_pem = std::move(*ca),
                       .token = std::move(*token)};
}

boost::asio::ssl::context make_ssl_context(const ClusterConfig& config) {
  boost::asio::ssl::context ctx(boost::asio::ssl::context::tls_client);
  ctx.set_verify_mode(boost::asio::ssl::verify_peer);
  ctx.add_certificate_authority(boost::asio::buffer(config.ca_pem));
  if (!config.client_cert_pem.empty()) {
    ctx.use_certificate_chain(boost::asio::buffer(config.client_cert_pem));
    ctx.use_private_key(boost::asio::buffer(config.client_key_pem),
                        boost::asio::ssl::context::pem);
  }
  return ctx;
}

}  // namespace k8sop::client
