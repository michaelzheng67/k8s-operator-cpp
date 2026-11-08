#pragma once

#include <string>
#include <string_view>

namespace k8sop::testing {

// A self-signed certificate and its private key, as PEM text. Generated fresh in memory by each
// test run so the repo never contains a private key; it expires an hour after creation.
struct TestCertificate {
  std::string cert_pem;
  std::string key_pem;
};

TestCertificate make_self_signed(std::string_view common_name);

std::string base64_encode(std::string_view data);

}  // namespace k8sop::testing
