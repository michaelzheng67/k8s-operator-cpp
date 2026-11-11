#pragma once

#include <memory>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace k8sop::informer {

// Thread-safe cache of the cluster's objects, keyed by "namespace/name".
// One writer (the informer), many readers (reconcilers). Readers get immutable snapshots.
template <typename T>
class Store {
  std::shared_mutex mutable mtx_;
  std::unordered_map<std::string, std::shared_ptr<const T>> mp_;

 public:
  void upsert(const std::string& key, std::shared_ptr<const T> obj) {
    std::unique_lock<std::shared_mutex> lk(mtx_);
    mp_[key] = std::move(obj);
  }

  void erase(const std::string& key) {
    std::unique_lock<std::shared_mutex> lk(mtx_);
    mp_.erase(key);
  }

  std::shared_ptr<const T> get(std::string_view key) const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    const auto it = mp_.find(std::string(key));
    return it == mp_.end() ? nullptr : it->second;
  }

  std::vector<std::shared_ptr<const T>> list() const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    std::vector<std::shared_ptr<const T>> ret;
    ret.reserve(mp_.size());

    for (const auto& val : std::views::values(mp_)) {
      ret.push_back(val);
    }

    return ret;
  }

  void replace(std::unordered_map<std::string, std::shared_ptr<const T>> all) {
    std::unique_lock<std::shared_mutex> lk(mtx_);
    std::swap(mp_, all);
  }
};

}  // namespace k8sop::informer
