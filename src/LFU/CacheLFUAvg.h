/*
LFU-Avg
LFU-K（Least Frequently Used Average）是一种改进的LFU算法，旨在解决LFU的一些局限性。它通过引入参数Average来控制缓存的淘汰策略，使得缓存能够更好地适应不同的访问模式。
本次设计是最大平均访问次数限制在LFU算法之上，引入访问次数平均值概念，当平均值大于最大平均值限制时将所有结点的访问次数减去最大平均值限制的一半或者一个固定值。相当于热点数据“老化”了，这样可以避免频次计数溢出，也可以缓解缓存污染
解决问题：
1. 防止某一个缓存的访问频次无限增加，而导致的计数溢出。
2.
旧的热点缓存，也就是该数据之前的访问频次很高，但是现在不再被访问了，也能够保证他在每次访问缓存平均访问次数大于最大平均访问次数的时候减去一个固定的值，使这个过去的热点缓存的访问频次逐步降到最低，然后从内存中淘汰出去
3.
一定程度上是对新加入进来的缓存，也就是访问频次为1的数据缓存进行了保护，因为长时间没被访问的旧的数据不再会长期占据缓存空间，访问频率会逐步被降为小于1最终淘汰
*/
#pragma once

#include "CacheLFU.h"
#include <memory>
#include <mutex>
#include <unordered_map>

namespace CacheMgr {

template <typename Key, typename Value>
class LFUAvgCache : public CacheBase<Key, Value> {
public:
  using Node = typename FreqList<Key, Value>::LFUNode;
  using NodePtr = typename FreqList<Key, Value>::NodePtr;
  using NodeMap = std::unordered_map<Key, NodePtr>;

  explicit LFUAvgCache(int capacity, int maxAvgFreq = 1000000)
      : capacity_(capacity), minFreq_(INT8_MAX), maxAvgFreq_(maxAvgFreq),
        currentAvgFreq_(0), currentTotalFreq_(0) {}

  virtual ~LFUAvgCache() override = default;

  // 添加缓存
  void put(const Key& key, const Value& val) override {
    if (0 == capacity_) {
      return; // No capacity to store new items
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
      // Key already exists, update value and frequency
      it->second->val = val;
      // Move to most recent access
      Value cur_val;
      getInternal(it->second, cur_val);
      return;
    }
    putInternal(key, val);
  }

  // 访问缓存
  bool get(const Key& key, Value &val) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
      getInternal(it->second, val);
      return true;
    }
    return false;
  }

  // 访问缓存
  Value get(const Key& key) override {
    Value val;
    get(key, val);
    return val;
  }

  // 清空缓存
  virtual void purge() {
    std::lock_guard<std::mutex> lock(mutex_);
    cacheMap_.clear();
    freqListMap_.clear();
    minFreq_ = INT8_MAX; // Reset minFreq
    currentAvgFreq_ = 0;
    currentTotalFreq_ = 0;
  }

private:
  // 添加缓存
  void putInternal(const Key& key, const Value& val) {
    // 如果不在缓存中，则需要判断缓存是否已满
    if (cacheMap_.size() >= capacity_) {
      // 缓存已满，删除最不常访问的结点，更新当前平均访问频次和总访问频次
      kickOut(); // Remove the least frequently used item
    }

    // 创建新结点，将新结点添加进入，更新最小访问频次
    auto node = std::make_shared<Node>(key, val);
    cacheMap_[key] = node;
    addNodeToFreqList(node);
    addFreqNum();
    minFreq_ = std::min(minFreq_, 1);
  }

  // 获取缓存
  void getInternal(NodePtr node, Value &val) {
    if (!node) {
      return;
    }

    // 找到之后需要将其从低访问频次的链表中删除，并且添加到+1的访问频次链表中，
    // 访问频次+1, 然后把value值返回
    val = node->val;
    // 从原有访问频次的链表中删除节点
    removeNodeFromFreqList(node);
    node->freq++;
    addNodeToFreqList(node);
    // 如果当前node的访问频次如果等于minFreq+1，并且其前驱链表为空，则说明
    // freqToFreqList_[node->freq - 1]链表因node的迁移已经空了，需要更新最小访问频次
    if (node->freq - 1 == minFreq_ && freqListMap_[node->freq - 1]->isEmpty()) {
      minFreq_++;
    }
    addFreqNum();
  }

  // 移除缓存中的过期数据
  void kickOut() {
    NodePtr node = freqListMap_[minFreq_]->getFirstNode();
    removeNodeFromFreqList(node);
    cacheMap_.erase(node->key);
    decreaseFreqNum(node->freq);
  }

  // 从频率列表中移除节点
  void removeNodeFromFreqList(NodePtr node) {
    if (!node || freqListMap_.end() == freqListMap_.find(node->freq)) {
      return;
    }

    freqListMap_[node->freq]->removeNode(node);

    if (freqListMap_[node->freq]->getFirstNode() ==
        freqListMap_[node->freq]->dummyTail_) {
      // 如果当前频率列表为空，删除该频率列表
      delete freqListMap_[node->freq];
      freqListMap_.erase(node->freq);
      if (minFreq_ == node->freq) {
        minFreq_ = (freqListMap_.empty()) ? 0 : minFreq_ + 1;
      }
    }
  }

  // 添加到频率列表
  void addNodeToFreqList(NodePtr node) {
    // 检查结点是否为空
    if (!node)
      return;

    // 添加进入相应的频次链表前需要判断该频次链表是否存在
    if (freqListMap_.find(node->freq) == freqListMap_.end()) {
      freqListMap_[node->freq] = new FreqList<Key, Value>(node->freq);
    }
    freqListMap_[node->freq]->addNode(node);
  }

  // 增加平均访问等频率
  void addFreqNum() {
    currentTotalFreq_++;
    if (cacheMap_.empty()) {
      currentAvgFreq_ = 0;
    } else {
      currentAvgFreq_ = currentTotalFreq_ / cacheMap_.size();
    }
    if (currentAvgFreq_ > maxAvgFreq_) {
      handleOverMaxAverageNum();
    }
  }

  // 减少平均访问等频率
  void decreaseFreqNum(int num) {
    currentTotalFreq_ -= num;
    if (currentTotalFreq_ < 0) {
      currentTotalFreq_ = 0;
    }
    if (cacheMap_.empty()) {
      currentAvgFreq_ = 0;
    } else {
      currentAvgFreq_ = currentTotalFreq_ / cacheMap_.size();
    }
  }

  // 处理当前平均访问频率超过上限的情况
  void handleOverMaxAverageNum() {
    if (cacheMap_.empty()) {
      return; // No items to remove
    }
    // 当前平均访问频次已经超过了最大平均访问频次，所有结点的访问频次- (maxAverageNum_ / 2)
    for (auto it = cacheMap_.begin(); it != cacheMap_.end(); ++it) {
      // 检查结点是否为空
      if (!it->second) {
        continue;
      }
      NodePtr node = it->second;
      // 先从当前频率列表中移除
      removeNodeFromFreqList(node);
      // 减少频率
      node->freq -= maxAvgFreq_ / 2; // Reduce frequency
      if (node->freq <= 0) {
        node->freq = 1; // Ensure frequency is at least 1
      }
      // 添加到新的频率列表
      addNodeToFreqList(node);
    }
    // 更新最小频率
    updateMinFreq();
  }

  // 更新最小访问频率
  void updateMinFreq() {
    minFreq_ = INT8_MAX;
    for (const auto &pair : freqListMap_) {
      if (pair.second && !pair.second->isEmpty()) {
        minFreq_ = std::min(minFreq_, pair.first);
      }
    }
    if (minFreq_ == INT8_MAX) {
      minFreq_ = 1; // Reset if no items are present
    }
  }

private:
  // 缓存容量
  int capacity_;
  // 最小访问频率
  int minFreq_;
  // 最大平均访问频率
  int maxAvgFreq_;
  // 当前平均访问频率
  int currentAvgFreq_;
  // 当前访问所有缓存次数的总数
  int currentTotalFreq_;
  // 互斥锁
  std::mutex mutex_;
  // 缓存映射表
  NodeMap cacheMap_;
  // 频率列表
  std::unordered_map<int, FreqList<Key, Value> *> freqListMap_;
};

} // namespace CacheMgr