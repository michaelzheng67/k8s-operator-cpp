#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include "k8sop/api/pod.hpp"
#include "k8sop/client/api_client.hpp"
#include "k8sop/client/config.hpp"
#include "k8sop/version.hpp"

namespace {

using k8sop::client::ApiClient;
using k8sop::client::ApiError;

constexpr std::string_view kUsage = R"(usage:
  operator                                        run the operator (not implemented yet)
  operator debug list-pods   [-n NS] [-l SELECTOR]
  operator debug create-pod  NAME [-n NS] [--image IMAGE]
  operator debug delete-pod  NAME [-n NS]
)";

// Flags shared by the debug subcommands. Anything that isn't a flag is a positional argument.
struct Args {
  std::string ns;
  std::string selector;
  std::string image = "busybox:1.36";
  std::vector<std::string_view> positional;
};

bool parse_flags(std::span<char*> argv, Args& args) {
  for (std::size_t i = 0; i < argv.size(); ++i) {
    const std::string_view arg = argv[i];
    const bool has_value = i + 1 < argv.size();
    if ((arg == "-n" || arg == "--namespace") && has_value) {
      args.ns = argv[++i];
    } else if ((arg == "-l" || arg == "--selector") && has_value) {
      args.selector = argv[++i];
    } else if (arg == "--image" && has_value) {
      args.image = argv[++i];
    } else if (arg.starts_with('-')) {
      return false;
    } else {
      args.positional.push_back(arg);
    }
  }
  return true;
}

int fail(const ApiError& error) {
  fmt::print(stderr, "error: {} {}: {}\n", error.status, error.reason, error.message);
  return 1;
}

int list_pods(ApiClient& client, const Args& args) {
  const auto pods = client.list<k8sop::api::Pod>(args.ns, args.selector);
  if (!pods) {
    return fail(pods.error());
  }
  fmt::print("{:<40} {}\n", "NAME", "STATUS");
  for (const auto& pod : pods->items) {
    fmt::print("{:<40} {}\n", pod.metadata.name, k8sop::api::to_string(pod.status.phase));
  }
  return 0;
}

int create_pod(ApiClient& client, const Args& args) {
  if (args.positional.size() != 1) {
    fmt::print(stderr, "{}", kUsage);
    return 2;
  }
  const k8sop::api::Pod pod{
      .metadata = {.name = std::string(args.positional[0]), .namespace_ = args.ns},
      .spec = {.containers = {{.name = "main", .image = args.image, .args = {"sleep", "3600"}}},
               .restart_policy = "Never"}};
  const auto created = client.create(pod);
  if (!created) {
    return fail(created.error());
  }
  fmt::print("pod/{} created (uid {})\n", created->metadata.name, created->metadata.uid);
  return 0;
}

int delete_pod(ApiClient& client, const Args& args) {
  if (args.positional.size() != 1) {
    fmt::print(stderr, "{}", kUsage);
    return 2;
  }
  const auto removed = client.remove<k8sop::api::Pod>(args.ns, args.positional[0]);
  if (!removed) {
    return fail(removed.error());
  }
  fmt::print("pod/{} deleted\n", args.positional[0]);
  return 0;
}

int debug(std::span<char*> argv) {
  if (argv.empty()) {
    fmt::print(stderr, "{}", kUsage);
    return 2;
  }
  const std::string_view command = argv[0];
  Args args;
  if (!parse_flags(argv.subspan(1), args)) {
    fmt::print(stderr, "{}", kUsage);
    return 2;
  }

  auto config = k8sop::client::load_default_kubeconfig();
  if (!config) {
    fmt::print(stderr, "error: {}\n", config.error().message);
    return 1;
  }
  if (args.ns.empty()) {
    args.ns = config->namespace_;
  }
  ApiClient client(std::move(*config));

  if (command == "list-pods") {
    return list_pods(client, args);
  }
  if (command == "create-pod") {
    return create_pod(client, args);
  }
  if (command == "delete-pod") {
    return delete_pod(client, args);
  }
  fmt::print(stderr, "{}", kUsage);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  const std::span<char*> args(argv, static_cast<std::size_t>(argc));
  if (args.size() >= 2 && std::string_view(args[1]) == "debug") {
    return debug(args.subspan(2));
  }
  if (args.size() >= 2) {
    fmt::print(stderr, "{}", kUsage);
    return 2;
  }
  spdlog::info("k8s-operator-cpp {}", k8sop::version());
  return 0;
}
