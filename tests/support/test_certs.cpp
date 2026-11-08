#include "test_certs.hpp"

#include <memory>
#include <stdexcept>
#include <string>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

namespace k8sop::testing {
namespace {

// OpenSSL hands out raw pointers that must be freed with its own functions; unique_ptr with a
// custom deleter makes each one free itself on every return path.
template <auto Free>
struct Deleter {
  template <typename T>
  void operator()(T* p) const noexcept {
    Free(p);
  }
};
using PKeyPtr = std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY_free>>;
using X509Ptr = std::unique_ptr<X509, Deleter<X509_free>>;
using BioPtr = std::unique_ptr<BIO, Deleter<BIO_free>>;

void check(bool ok, const char* what) {
  if (!ok) {
    throw std::runtime_error(std::string("openssl: ") + what);
  }
}

std::string drain(BIO* bio) {
  char* data = nullptr;
  const long size = BIO_get_mem_data(bio, &data);
  return {data, static_cast<std::size_t>(size)};
}

}  // namespace

TestCertificate make_self_signed(std::string_view common_name) {
  const PKeyPtr key(EVP_EC_gen("P-256"));
  check(key != nullptr, "EVP_EC_gen");

  const X509Ptr cert(X509_new());
  check(cert != nullptr, "X509_new");
  check(X509_set_version(cert.get(), X509_VERSION_3) == 1, "X509_set_version");
  check(ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1) == 1, "serial");
  check(X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0) != nullptr, "notBefore");
  check(X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600) != nullptr, "notAfter");
  check(X509_set_pubkey(cert.get(), key.get()) == 1, "X509_set_pubkey");

  X509_NAME* name = X509_get_subject_name(cert.get());
  const std::string cn(common_name);
  check(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1,
                                   0) == 1,
        "subject");
  check(X509_set_issuer_name(cert.get(), name) == 1, "issuer");  // self-signed: issuer == subject
  check(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0, "X509_sign");

  const BioPtr cert_bio(BIO_new(BIO_s_mem()));
  const BioPtr key_bio(BIO_new(BIO_s_mem()));
  check(cert_bio && key_bio, "BIO_new");
  check(PEM_write_bio_X509(cert_bio.get(), cert.get()) == 1, "PEM_write_bio_X509");
  check(PEM_write_bio_PrivateKey(key_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) ==
            1,
        "PEM_write_bio_PrivateKey");
  return {.cert_pem = drain(cert_bio.get()), .key_pem = drain(key_bio.get())};
}

std::string base64_encode(std::string_view data) {
  std::string out(4 * ((data.size() + 2) / 3), '\0');
  const int written =
      EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                      reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()));
  out.resize(static_cast<std::size_t>(written));
  return out;
}

}  // namespace k8sop::testing
