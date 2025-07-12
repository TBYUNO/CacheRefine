/*
LFU
最近使用频率高的数据很大概率将会再次被使用,而最近使用频率低的数据,将来大概率不会再使用。
做法：把使用频率最小的数据置换出去。这种算法更多是从使用频率的角度（但是当缓存满时，如果最低访问频次的缓存数据有多个，就需要考虑哪个元素最近最久未被访问）去考虑的
存在问题：
●
频率爆炸问题：对于长期驻留在缓存中的热数据，频率计数可能会无限增长，占用额外的存储空间或导致计数溢出。
●
过时热点数据占用缓存：一些数据可能已经不再是热点数据，但因访问频率过高，难以被替换。
●
冷启动问题：刚加入缓存的项可能因为频率为1而很快被淘汰，即便这些项是近期访问的热门数据。
●
不适合短期热点：LFU对长期热点数据表现较好，但对短期热点数据响应较慢，可能导致短期热点数据无法及时缓存。
● 缺乏动态适应性：固定的LFU策略难以适应不同的应用场景或工作负载。
● 锁的粒度大，多线程高并发访问下锁的同步等待时间过长
优化方案：
● 加上最大平均访问次数限制
    在LFU算法之上，引入访问次数平均值概念，当平均值大于最大平均值限制时将所有结点的访问次数减去最大平均值限制的一半或者一个固定值。相当于热点数据“老化”了，这样可以避免频次计数溢出，也可以缓解缓存污染
    解决问题：
    1. 防止某一个缓存的访问频次无限增加，而导致的计数溢出。
    2.
旧的热点缓存，也就是该数据之前的访问频次很高，但是现在不再被访问了，也能够保证他在每次访问缓存平均访问次数大于最大平均访问次数的时候减去一个固定的值，使这个过去的热点缓存的访问频次逐步降到最低，然后从内存中淘汰出去
    3.
一定程度上是对新加入进来的缓存，也就是访问频次为1的数据缓存进行了保护，因为长时间没被访问的旧的数据不再会长期占据缓存空间，访问频率会逐步被降为小于1最终淘汰
● HashLFUCache
*/
#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include "CacheBase.h"

namespace CacheMgr {

template <typename Key, typename Value> class LFUCache;
template <typename Key, typename Value> class LFUAvgCache;

template <typename Key, typename Value> class FreqList {
private:
  struct LFUNode {
    // 访问频次
    int freq;
    // 键值，索引
    Key key;
    // 缓存值
    Value val;
    // 前置节点
    std::shared_ptr<LFUNode> prev;
    // 后置节点
    std::shared_ptr<LFUNode> next;

    LFUNode() : freq(1), prev(nullptr), next(nullptr) {}
    LFUNode(Key key, Value val)
        : freq(1), key(key), val(val), prev(nullptr), next(nullptr) {}
  };

public:
  using NodePtr = std::shared_ptr<LFUNode>;

  explicit FreqList(int count) : freq_(count) { initializeList(); }

  virtual ~FreqList() = default;

  // 缓存序列是否为空判断
  bool isEmpty() const {
    return (dummyHead_ && dummyTail_) ? dummyHead_->next == dummyTail_ : true;
  }

  // 添加缓存节点
  void addNode(NodePtr node) {
    if (!node || !dummyHead_ || !dummyTail_) {
      return;
    }

    node->prev = dummyTail_->prev;
    node->next = dummyTail_;
    dummyTail_->prev->next = node;
    dummyTail_->prev = node;
  }

  // 删除缓存节点
  void removeNode(NodePtr node) {
    if (!node || !dummyHead_ || !dummyTail_) {
      return;
    }

    if (!node->prev || !node->next) {
      return;
    }

    auto prev = node->prev;
    prev->next = node->next;
    node->next->prev = prev;
    node->prev = nullptr;
    node->next = nullptr;
  }

  // 获取第首个缓存节点
  NodePtr getFirstNode() const { return dummyHead_->next; }

private:
  // 缓存初始化
  void initializeList() {
    dummyHead_ = std::make_shared<LFUNode>();
    dummyTail_ = std::make_shared<LFUNode>();
    dummyHead_->next = dummyTail_;
    dummyTail_->prev = dummyHead_;
  }

private:
  // 访问频率
  int freq_;
  // 虚假的头部缓存节点
  NodePtr dummyHead_;
  // 虚假的末尾缓存节点
  NodePtr dummyTail_;

public:
  friend class LFUCache<Key, Value>;
  friend class LFUAvgCache<Key, Value>;
};

template <typename Key, typename Value>
class LFUCache : public CacheBase<Key, Value> {
public:
  using Node = typename FreqList<Key, Value>::LFUNode;
  using NodePtr = typename FreqList<Key, Value>::NodePtr;
  using NodeMap = std::unordered_map<Key, NodePtr>;

  explicit LFUCache(int capacity) : capacity_(capacity), minFreq_(0) {}

  virtual ~LFUCache() override = default;

  // 添加缓存
  void put(const Key &key, const Value &val) override {
    if (0 == capacity_) {
      return; // No capacity to store new items
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
      // Key already exists, update value and frequency
      it->second->val = val;
    } else {
      if (cacheMap_.size() >= capacity_ && minFreq_ > 0 &&
          freqListMap_.find(minFreq_) != freqListMap_.end()) {
        // Remove the least frequently used item
        NodePtr node = freqListMap_.at(minFreq_)->getFirstNode();
        removeNodeFromFreqList(node);
        cacheMap_.erase(node->key);
      }
      // Create a new node and add it to the cache
      NodePtr newNode = std::make_shared<Node>(key, val);
      cacheMap_[key] = newNode;
      minFreq_ = 1; // Reset minFreq to 1 for new node
      addNodeToFreqList(newNode);
    }
  }

  // 访问缓存
  bool get(const Key &key, Value &val) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
      NodePtr node = it->second;
      removeNodeFromFreqList(node);
      ++node->freq;
      /**
       * 下面这个操作是为了防止当前 node对应的是最小频数双向链表里的唯一节点，
       * 具体情况可分两种讨论
       * 情况 ① 如果当前 node
       * 对应的是最小频数双向链表里的唯一节点，那么在进行对其的get操作后，它的频数
       * freq++， 原双向链表节点数目变为 0，则最小频数minFreq++，即执行这个 if
       * 操作 情况 ② 如果当前 node
       * 对应的不是最小频数双向链表里的唯一节点，那么无需更新 minFreq
       */
      if (freqListMap_.end() == freqListMap_.find(minFreq_) ||
          freqListMap_.at(minFreq_)->isEmpty()) {
        ++minFreq_;
      }
      addNodeToFreqList(node);
      val = node->val;
      return true;
    }
    return false;
  }

  // 访问缓存
  Value get(const Key &key) override {
    Value val;
    get(key, val);
    return val;
  }

  // 清空缓存
  void purge() {
    std::lock_guard<std::mutex> lock(mutex_);
    cacheMap_.clear();
    freqListMap_.clear();
    minFreq_ = 0; // Reset minFreq
  }

private:
  // 从频率列表中移除节点
  void removeNodeFromFreqList(NodePtr node) {
    if (!node || freqListMap_.end() == freqListMap_.find(node->freq)) {
      return;
    }

    freqListMap_[node->freq]->removeNode(node);

    if (freqListMap_.end() != freqListMap_.find(node->freq) &&
        freqListMap_[node->freq]->getFirstNode() ==
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
    if (!node) {
      return;
    }

    if (freqListMap_.end() == freqListMap_.find(node->freq)) {
      freqListMap_[node->freq] = new FreqList<Key, Value>(node->freq);
    }
    freqListMap_[node->freq]->addNode(node);
  }

private:
  // 缓存容量
  int capacity_;
  // 最小访问频率
  int minFreq_;
  // 互斥锁
  std::mutex mutex_;
  // 缓存映射表
  NodeMap cacheMap_;
  // 频率列表
  std::unordered_map<int, FreqList<Key, Value> *> freqListMap_;
};

} // namespace CacheMgr