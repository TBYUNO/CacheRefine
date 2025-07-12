/*
LRU-k：
LRU-k算法是对LRU算法的改进，基础的LRU算法被访问数据进入缓存队列只需要访问(put、get)一次就行，但是现在需要被访问k（大小自定义）次才能被放入缓存中，基础的LRU算法可以看成是LRU-1。
LRU-k算法有两个队列一个是缓存队列，一个是数据访问历史队列。当访问一个数据时，首先将其添加进入访问历史队列并进行累加访问次数，当该数据的访问次数超过k次后，才将数据缓存到缓存队列，从而避免缓存队列被冷数据所污染。同时访问历史队列中的数据也不是一直保留的，也是需要按照LRU的规则进行淘汰的。
一般情况下，当k的值越大，缓存的命中率越高，但也使得缓存难以淘汰。综合来说，k = 2
时性能最优。
*/
#pragma once
#include "CacheLRU.h"

namespace CacheMgr {

template <typename Key, typename Value>
class LRUKCache : public LRUCache<Key, Value> {
public:
  explicit LRUKCache(int capatity, int histCapatity, int k)
      : LRUCache<Key, Value>(capatity),
        histList_(std::make_unique<LRUCache<Key, size_t>>(histCapatity)),
        k_(k) {}

  virtual ~LRUKCache() override = default;

  void put(const Key& key, const Value& val) override {
    // 检查主缓存是否已有数据
    Value existingVal{};
    if (LRUCache<Key, Value>::get(key, existingVal)) {
      // 已在主缓存，直接更新
      LRUCache<Key, Value>::put(key, val);
      return;
    }

    // 获取并更新访问历史
    size_t histCount = histList_->get(key);
    ++histCount;
    histList_->put(key, histCount);
    // 保存值到历史记录映射，供后续get操作使用
    histValMap_[key] = val;

    // 检查是否达到k次访问阈值
    if (histCount >= k_) {
      // 达到阈值，添加到主缓存
      histList_->remove(key);
      histValMap_.erase(key);
      LRUCache<Key, Value>::put(key, val);
    }
  }

  bool get(const Key& key, Value &val) override {
    // 优先尝试从主缓存中查询数据
    bool inMainCache = LRUCache<Key, Value>::get(key, val);

    // 获取并更新访问历史计数
    size_t histCount = histList_->get(key);
    ++histCount;
    histList_->put(key, histCount);

    // 如果数据在主缓存中，直接返回
    if (inMainCache) {
      return true;
    }

    // 如果数据不在主缓存，但访问次数达到了k次
    if (histCount >= k_) {
      // 检查是否有历史记录值
      auto it = histValMap_.find(key);
      if (histValMap_.end() != it) {
        // 有历史值，将其添加到主缓存
        val = it->second;
        // 删除历史记录
        histList_->remove(key);
        histValMap_.erase(it);
        // 添加到主缓存
        LRUCache<Key, Value>::put(key, val);

        return true;
      }
      // 没有历史值记录，无法添加到缓存，返回默认值
    }

    // 数据不在主缓存且不满足添加条件，返回false
    return false;
  }

  Value get(const Key& key) override {
    Value val{};
    get(key, val);
    return val;
  }

private:
  // 进入缓存队列的评判标准
  int k_;
  // 访问数据历史记录（value为访问次数）
  std::unique_ptr<LRUCache<Key, size_t>> histList_;
  // 存储未达到k次访问的数据值
  std::unordered_map<Key, Value> histValMap_;
};

} // namespace CacheMgr