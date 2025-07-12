#pragma once

#include <stdexcept>

#include "CacheBase.h"
#include "CacheARCLFUPart.h"
#include "CacheARCLRUPart.h"

namespace CacheMgr {

template <typename Key, typename Value>
class ARCCache : public CacheBase<Key, Value> {
public:
  explicit ARCCache(size_t capacity = 10, size_t transformThreshold = 2)
      : capacity_(capacity), transformThreshold_(transformThreshold),
        lruCache_(std::make_unique<ARCLRUCache<Key, Value>>(
            capacity, transformThreshold)),
        lfuCache_(std::make_unique<ARCLFUCache<Key, Value>>(
            capacity, transformThreshold)) {}

  ~ARCCache() override = default;

  bool get(const Key& key, Value &value) override {
    checkGhost(key); // 检查影子缓存

    bool shouldTransform = false;
    if (lruCache_->get(key, value, shouldTransform)) {
      if (shouldTransform) {
        // 如果 LRU 部分需要转换到 LFU 部分
        lfuCache_->put(key, value);
      }
      return true; // 成功获取到值
    }
    return lfuCache_->get(key, value); // 尝试从 LFU 部分获取
  }

  void put(const Key& key, const Value& value) override {
    checkGhost(key); // 检查影子缓存

    // 检查LFU部分是否包含该键
    bool inLfu = lfuCache_->contains(key);
    // 更新LRU部分
    lruCache_->put(key, value);
    // 如果LFU部分包含该键，则添加到LFU部分
    if (inLfu) {
      lfuCache_->put(key, value);
    }
  }

  Value get(const Key& key) override {
    Value value;
    if (get(key, value)) {
      return value; // 成功获取到值
    }
    throw std::runtime_error("Key not found in cache");
  }

private:
  bool checkGhost(const Key &key) {
    bool inGhotst = false;
    if (lruCache_->checkGhost(key)) {
      if (lfuCache_->decreaseCapacity()) {
        lruCache_->increaseCapacity(); // 如果影子缓存容量减少，增加LRU部分容量
      }
      inGhotst = true;
    } else if (lfuCache_->checkGhost(key)) {
      if (lruCache_->decreaseCapacity()) {
        lfuCache_->increaseCapacity(); // 如果影子缓存容量减少，增加LFU部分容量
      }
      inGhotst = true;
    }
    return inGhotst; // 返回是否在影子缓存中找到
  }

private:
  size_t capacity_;                                   // 缓存容量
  size_t transformThreshold_;                         // 转换阈值
  std::unique_ptr<ARCLRUCache<Key, Value>> lruCache_; // LRU 部分
  std::unique_ptr<ARCLFUCache<Key, Value>> lfuCache_; // LFU 部分
};

} // namespace CacheMgr