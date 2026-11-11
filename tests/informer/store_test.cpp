#include "k8sop/informer/store.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "k8sop/api/pod.hpp"

namespace k8sop::informer {
namespace {

using api::Pod;

std::shared_ptr<const Pod> make_pod(const std::string& name, const std::string& rv) {
  return std::make_shared<const Pod>(
      Pod{.metadata = {.name = name, .namespace_ = "default", .resource_version = rv}});
}

TEST(Store, MissingKeyIsNull) {
  const Store<Pod> store;
  EXPECT_EQ(store.get("default/nope"), nullptr);
  EXPECT_TRUE(store.list().empty());
}

TEST(Store, UpsertThenGet) {
  Store<Pod> store;
  store.upsert("default/a", make_pod("a", "1"));
  const auto pod = store.get("default/a");
  ASSERT_NE(pod, nullptr);
  EXPECT_EQ(pod->metadata.name, "a");
}

TEST(Store, OldSnapshotSurvivesReplacement) {
  Store<Pod> store;
  store.upsert("default/a", make_pod("a", "1"));
  const auto before = store.get("default/a");

  store.upsert("default/a", make_pod("a", "2"));

  EXPECT_EQ(before->metadata.resource_version, "1");  // a reader's snapshot never changes
  EXPECT_EQ(store.get("default/a")->metadata.resource_version, "2");
}

TEST(Store, OldSnapshotSurvivesErase) {
  Store<Pod> store;
  store.upsert("default/a", make_pod("a", "1"));
  const auto held = store.get("default/a");

  store.erase("default/a");

  EXPECT_EQ(store.get("default/a"), nullptr);
  EXPECT_EQ(held->metadata.name, "a");  // still alive: the reader owns a reference
}

TEST(Store, EraseMissingKeyIsNoop) {
  Store<Pod> store;
  store.erase("default/nope");
  EXPECT_TRUE(store.list().empty());
}

TEST(Store, ListReturnsEverything) {
  Store<Pod> store;
  store.upsert("default/a", make_pod("a", "1"));
  store.upsert("default/b", make_pod("b", "1"));
  store.upsert("default/c", make_pod("c", "1"));
  EXPECT_EQ(store.list().size(), 3U);
}

TEST(Store, ReplaceSwapsInFullRelist) {
  Store<Pod> store;
  store.upsert("default/stale", make_pod("stale", "1"));

  std::unordered_map<std::string, std::shared_ptr<const Pod>> relist{
      {"default/a", make_pod("a", "5")}, {"default/b", make_pod("b", "5")}};
  store.replace(std::move(relist));

  EXPECT_EQ(store.get("default/stale"), nullptr);  // deleted while we weren't watching
  EXPECT_NE(store.get("default/a"), nullptr);
  EXPECT_EQ(store.list().size(), 2U);
}

// Meaningful under the tsan preset: one writer, several readers, no data races allowed.
TEST(Store, ConcurrentReadersAndWriter) {
  Store<Pod> store;
  std::atomic<bool> done{false};

  std::vector<std::jthread> readers;
  for (int r = 0; r < 4; ++r) {
    readers.emplace_back([&] {
      while (!done.load(std::memory_order_relaxed)) {
        if (const auto pod = store.get("default/hot")) {
          EXPECT_EQ(pod->metadata.name, "hot");  // touch the object, not just the pointer
        }
        for (const auto& pod : store.list()) {
          EXPECT_FALSE(pod->metadata.name.empty());
        }
      }
    });
  }

  for (int i = 0; i < 2000; ++i) {
    store.upsert("default/hot", make_pod("hot", std::to_string(i)));
    store.upsert("default/k" + std::to_string(i % 10), make_pod("k", std::to_string(i)));
    if (i % 7 == 0) {
      store.erase("default/hot");
    }
  }
  done = true;
}

}  // namespace
}  // namespace k8sop::informer
