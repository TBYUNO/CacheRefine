/*
HashLRU:
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

#include "CacheLRU.h"
#include <cmath>
#include <thread>
#include <vector>

namespace CacheMgr {

template <typename Key, typename Value> class LRUHashCache {
public:
  explicit LRUHashCache(size_t capacity, int sliceNum)
      : capacity_(capacity),
        sliceNum_(sliceNum > 0 ? sliceNum
                               : std::thread::hardware_concurrency()) {
    // 计算分片大小
    size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_));
    for (size_t idx = 0; idx < sliceNum_; ++idx) {
      lruSliceCaches_.emplace_back(new LRUCache<Key, Value>(sliceSize));
    }
  }

  virtual ~LRUHashCache() = default;

  void put(const Key &key, const Value &val) {
    // 获取key的hash值，并计算出对应的分片索引
    size_t sliceIndex = Hash(key) % sliceNum_;
    lruSliceCaches_[sliceIndex]->put(key, val);
  }

  bool get(const Key &key, Value &val) {
    // 获取key的hash值，并计算出对应的分片索引
    size_t sliceIndex = Hash(key) % sliceNum_;
    return lruSliceCaches_[sliceIndex]->get(key, val);
  }

  Value get(const Key &key) {
    Value val;
    memset(&val, 0, sizeof(val));
    get(key, val);
    return val;
  }

private:
  // 将Key转换为对应hash值
  size_t Hash(const Key &key) {
    std::hash<Key> hashFunc;
    return hashFunc(key);
  }

private:
  // 总容量
  size_t capacity_;
  // 切片数量
  int sliceNum_;
  // 切片LRU缓存
  std::vector<std::unique_ptr<LRUCache<Key, Value>>> lruSliceCaches_;
};

} // namespace CacheMgr