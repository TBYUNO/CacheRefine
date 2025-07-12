/*
HashLFU:
通过哈希分片的方式优化锁的粒度，优化多线程支持
根据传入的key值进行哈希运算后找到对应的lru分片，然后调用该分片相应的方法
普通的LRU和LFU在高并发情况下耗时增加：
线程安全的LFU中有锁的存在。每次读写操作之前都有加锁操作，完成读写操作之后还有解锁操作。在低QPS下，锁的竞争的耗时基本可以忽略；但在高并发的情况下，大量的时间消耗在等待锁的操作上，导致耗时增长
Hash LRU和Hash LFU适应高并发场景：
针对大量同步等待操作导致耗时增加的情况，解决方案就是尽量减小临界区。引入hash机制，对全量数据做分片处理，在原有LFU的基础上形成Hash
LFU，以降低查询耗时 Hash
LFU引入哈希算法，将缓存数据分散到N个LFU上，查询时也按照相同的哈希算法，先获取数据可能存在的分片，然后再去对应的分片上查询数据。这样可以增加LFU的读写操作的并行度，减少同步等待的耗时
*/
#pragma once

#include <vector>
#include <thread>
#include <cmath>

#include "CacheLFUAvg.h"

namespace CacheMgr {
template <typename Key, typename Value>
class LFUHashCache {
public:
  explicit LFUHashCache(int capacity, int sliceNu, int maxAvgFreq = 10)
      : capacity_(capacity),
        sliceNum_(sliceNu > 0 ? sliceNu : std::thread::hardware_concurrency()) {
    size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_));
    // 初始化分片LFU缓存
    for (int idx = 0; idx < sliceNum_; ++idx) {
      lfuSliceCaches_.emplace_back(
          std::make_unique<LFUAvgCache<Key, Value>>(sliceSize, maxAvgFreq));
    }
  }

  virtual ~LFUHashCache() = default;

  void put(const Key& key, const Value& val) {
    size_t sliceIndex = getSliceIndex(key);
    if (sliceIndex < lfuSliceCaches_.size()) {
      lfuSliceCaches_[sliceIndex]->put(key, val);
    }
  }

  bool get(const Key& key, Value &val) {
    size_t sliceIndex = getSliceIndex(key);
    if (sliceIndex < lfuSliceCaches_.size()) {
      return lfuSliceCaches_[sliceIndex]->get(key, val);
    }
    return false;
  }

  Value get(const Key& key) {
    Value val;
    get(key, val);
    return val;
  }

  void purge() {
    for (auto &sliceCache : lfuSliceCaches_) {
      sliceCache->purge();
    }
    lfuSliceCaches_.clear();
  }

private:
  // 将Key转换为对应hash值
  size_t Hash(const Key& key) {
    std::hash<Key> hashFunc;
    return static_cast<int>(hashFunc(key));
  }

  // 获取对应的分片索引
  int getSliceIndex(const Key& key) {
    size_t hashValue = Hash(key);
    return static_cast<int>(hashValue % sliceNum_);
  }

private:
  // 缓存总量
  int capacity_;
  // 缓存分片数量
  int sliceNum_;
  // 缓存LFU分片容器
  std::vector<std::unique_ptr<LFUAvgCache<Key, Value>>> lfuSliceCaches_;
};

} // namespace CacheMgr