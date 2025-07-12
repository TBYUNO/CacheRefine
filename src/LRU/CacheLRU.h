/*
LRU:
最近最少使用算法，如果一个数据最近被访问过，那么将来被访问的可能性也较大。因此，它选择最近最长时间未被访问的页面进行替换。LRU的性能和效率接近OPT，但是对于频繁访问的页面更新开销较大。
潜在问题：淘汰热点数据，如果有个数据在1个小时的前59分钟访问了1万次(可见这是个热点数据),再后一分钟没有访问这个数据，但是有其他的数据访问，就导致了我们这个热点数据被淘汰。
缺点：
●
对访问模式不敏感：如果是循环的一次性遍历大量不重复的数据（如A->B->C->D->A->B->...），LRU可能逐步清空，几乎无法命中。
●
缓存污染：如果加载一些不再会被访问的冷数据（如一次性数据），将原有的热点数据挤出，冷数据留在缓存中，降低了缓存的利用率。
● 不适用于某些场景：在某些场景下，最近最少使用并不代表最不重要或最少需要。
● 锁的粒度大：多线程高并发的访问下，同步等待将是一笔极大的时间开销。
 */
#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include "CacheBase.h"

namespace CacheMgr {

template <typename Key, typename Value> class LRUCache;

template <typename Key, typename Value> class LRUNode {
public:
  explicit LRUNode(Key key, Value val) : key_(key), val_(val), count_(1) {}

  ~LRUNode() = default;

  // 缓存节点操作，索引、值、次数的访问与设置
  Key getKey() const { return key_; }
  Value getValue() const { return val_; }
  void setValue(const Value &val) { val_ = val; }
  size_t getAccessCount() const { return count_; }
  void incrementAccessCount() { ++count_; }

private:
  // 关键字，索引
  Key key_;
  // 缓存值
  Value val_;
  // 访问次数
  size_t count_;
  // 前置缓存，弱指针避免重复引用
  std::shared_ptr<LRUNode<Key, Value>> prev_;
  // 后置缓存
  std::shared_ptr<LRUNode<Key, Value>> next_;

public:
  friend class LRUCache<Key, Value>;
};

template <typename Key, typename Value>
class LRUCache : public CacheBase<Key, Value> {
public:
  using LRUNodeType = LRUNode<Key, Value>;
  using NodePtr = std::shared_ptr<LRUNodeType>;
  using NodeMap = std::unordered_map<Key, NodePtr>;

  explicit LRUCache(int capacity) : capacity_(capacity) { initializeList(); }

  virtual ~LRUCache() override = default;

  void put(const Key& key, const Value& val) override {
    if (0 >= capacity_) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodeMap_.find(key);
    if (nodeMap_.end() != it) {
      // 如果在当前容器中,则更新value,并调用get方法，代表该数据刚被访问
      updateExistingNode(it->second, val);
      return;
    }

    addNode(key, val);
  }

  bool get(const Key& key, Value &val) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodeMap_.find(key);
    if (nodeMap_.end() != it) {

      moveToMostRecent(it->second);
      val = it->second->getValue();
      return true;
    }
    return false;
  }

  Value get(const Key& key) override {
    Value val{};
    get(key, val);
    return val;
  }

  // 删除指定缓存
  void remove(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodeMap_.find(key);
    if (nodeMap_.end() != it) {
      removeNode(it->second);
      nodeMap_.erase(it);
    }
  }

private:
  // 缓存初始化
  void initializeList() {
    dummyHead_ = std::make_shared<LRUNodeType>(Key(), Value());
    dummyTail_ = std::make_shared<LRUNodeType>(Key(), Value());
    dummyHead_->next_ = dummyTail_;
    dummyTail_->prev_ = dummyHead_;
  }

  // 更新现有缓存节点
  void updateExistingNode(NodePtr node, const Value &val) {
    node->setValue(val);
    moveToMostRecent(node);
  }

  // 增加缓存节点
  void addNode(const Key &key, const Value &val) {
    if (nodeMap_.size() >= capacity_) {
      evictLeastRecent();
    }
    NodePtr node = std::make_shared<LRUNodeType>(key, val);
    insertNode(node);
    nodeMap_[key] = node;
  }

  // 移动指定缓存节点到最新位置
  void moveToMostRecent(NodePtr node) {
    removeNode(node);
    insertNode(node);
  }

  // 删除指定节点
  void removeNode(NodePtr node) {
    if (node->prev_ && node->next_) {
      auto prev = node->prev_;
      prev->next_ = node->next_;
      node->next_->prev_ = prev;
      // 清空next_指针，彻底断开缓存节点与链表的连接
      node->next_ = nullptr;
    }
  }

  // 在末尾添加指定节点
  void insertNode(NodePtr node) {
    node->next_ = dummyTail_;
    node->prev_ = dummyTail_->prev_;
    dummyTail_->prev_->next_ = node;
    dummyTail_->prev_ = node;
  }

  // 驱逐最近最少访问的缓存节点
  void evictLeastRecent() {
    NodePtr leastRecent = dummyHead_->next_;
    removeNode(leastRecent);
    nodeMap_.erase(leastRecent->getKey());
  }

private:
  // 缓存容量
  int capacity_;
  // 节点映射
  NodeMap nodeMap_;
  // 互斥锁
  std::mutex mutex_;
  // 虚拟头节点
  NodePtr dummyHead_;
  // 虚拟末节点
  NodePtr dummyTail_;
};

} // namespace CacheMgr