#include "k8sop/version.hpp"

#include <algorithm>

#include <gtest/gtest.h>

TEST(Version, IsSemver) {
  const auto v = k8sop::version();
  EXPECT_EQ(std::count(v.begin(), v.end(), '.'), 2);
}
