#include "k8sop/client/api_error.hpp"

#include <gtest/gtest.h>

namespace k8sop::client {
namespace {

bool retryable(int status) { return ApiError{.status = status}.is_retryable(); }

TEST(ApiError, DefaultIsConnectionError) { EXPECT_EQ(ApiError{}.status, 0); }

TEST(ApiError, TransientFailuresAreRetryable) {
  EXPECT_TRUE(retryable(0));  // connection dropped, timed out, refused
  EXPECT_TRUE(retryable(429));
  EXPECT_TRUE(retryable(500));
  EXPECT_TRUE(retryable(503));
  EXPECT_TRUE(retryable(504));
}

TEST(ApiError, PermanentAndStaleFailuresAreNot) {
  EXPECT_FALSE(retryable(400));
  EXPECT_FALSE(retryable(403));  // RBAC misconfiguration: retrying won't fix it
  EXPECT_FALSE(retryable(404));
  EXPECT_FALSE(retryable(409));  // stale resourceVersion: re-read and recompute, don't resend
  EXPECT_FALSE(retryable(422));
}

}  // namespace
}  // namespace k8sop::client
