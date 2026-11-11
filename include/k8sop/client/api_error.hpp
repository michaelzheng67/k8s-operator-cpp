#pragma once

#include <string>

namespace k8sop::client {

struct ApiError {
  int status{0};
  std::string reason;
  std::string message;

  [[nodiscard]] bool is_retryable() const noexcept {
    return status == 0 || status == 429 || (status >= 500 && status <= 599);
  }
};

}  // namespace k8sop::client
