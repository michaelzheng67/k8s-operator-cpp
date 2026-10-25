#include <spdlog/spdlog.h>

#include "k8sop/version.hpp"

int main() {
  spdlog::info("k8s-operator-cpp {}", k8sop::version());
  return 0;
}
