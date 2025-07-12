#pragma once

#include <list>
#include <map>
#include <mutex>
#include <unordered_map>

#include "CacheARCNode.h"

namespace CacheMgr {

template <typename Key, typename Value> class ARCLFUCache {
public:
  using NodeType = ARCNode<Key, Value>;
  using NodePtr = std::shared_ptr<NodeType>;
  using NodeMap = std::unordered_map<Key, NodePtr>;
  using FreqMap = std::map<size_t, std::list<NodePtr>>;

  explicit ARCLFUCache(size_t capacity, size_t transformThreshold)
      : capacity_(capacity), ghostCapacity_(capacity),
        transformThreshold_(transformThreshold), minFreq_(0) {
    initializeCache();
  }

  ~ARCLFUCache() = default;

  bool get(const Key &key, Value &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mainCache_.find(key);
    if (it != mainCache_.end()) {
      NodePtr node = it->second;
      updateNodeFrequency(node);
      value = node->getValue();
      return true;
    }
    return false; // 未找到节点
  }

  bool put(const Key &key, const Value &value) {
    if (0 == capacity_) {
      return false; // 如果容量为0，直接返回
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mainCache_.find(key);
    if (it != mainCache_.end()) {
      return updateExistingNode(it->second, value);
    }
    return addNewNode(key, value); // 添加新节点
  }

  bool contains(const Key &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return mainCache_.find(key) != mainCache_.end();
  }

  bool checkGhost(const Key &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ghostCache_.find(key);
    if (it != ghostCache_.end()) {
      NodePtr node = it->second;
      removeFromGhostCache(node);
      ghostCache_.erase(it);
      return true; // 如果在影子缓存中找到，返回true
    }
    return false; // 未找到节点
  }

  void increaseCapacity() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++capacity_;
    ++ghostCapacity_;
  }

  bool decreaseCapacity() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (0 >= capacity_) {
      return false; // 如果容量已经是0，直接返回
    }
    if (mainCache_.size() == capacity_) {
      evictLeastRecent(); // 如果主缓存已满，先移除最少使用
    }
    if (ghostCache_.size() == ghostCapacity_) {
      removeOldestGhostNode(); // 如果影子缓存已满，先移除最旧的影子节点
    }
    --capacity_;
    --ghostCapacity_;
    return true; // 成功减少容量
  }

private:
  void initializeCache() {
    ghostHead_ = std::make_shared<NodeType>();
    ghostTail_ = std::make_shared<NodeType>();
    ghostHead_->next = ghostTail_;
    ghostTail_->prev_ = ghostHead_;
  }

  bool updateExistingNode(NodePtr node, const Value &value) {
    if (node) {
      node->setValue(value);
      updateNodeFrequency(node);
      return true; // 更新成功
    }
    return false; // 节点不存在
  }

  bool addNewNode(const Key &key, const Value &value) {
    if (mainCache_.size() >= capacity_) {
      evictLeastRecent(); // 如果主缓存已满，先移除最少使用
    }
    NodePtr newNode = std::make_shared<NodeType>(key, value);
    mainCache_[key] = newNode;
    if (freqMap_.find(1) == freqMap_.end()) {
      freqMap_[1] = std::list<NodePtr>(); // 初始化频率为1的列表
    }
    freqMap_[1].push_back(newNode); // 将新节点添加到频率为1的列表中
    minFreq_ = 1;                   // 更新最小频率
    return true;                    // 添加成功
  }

  void updateNodeFrequency(NodePtr node) {
    size_t oldFreq = node->getAccessCount();
    node->incrementAccessCount(); // 增加访问计数
    size_t newFreq = node->getAccessCount();
    if (oldFreq == newFreq) {
      return; // 如果频率没有变化，直接返回
    }
    // 从旧频率列表中移除节点
    auto it = freqMap_.find(oldFreq);
    if (it != freqMap_.end()) {
      auto &nodeList = it->second;
      nodeList.remove(node); // 从旧频率列表中移除节点
      if (nodeList.empty()) {
        freqMap_.erase(it); // 如果旧频率列表为空，移除该频率
        if (minFreq_ == oldFreq) {
          minFreq_ = newFreq; // 如果旧频率是最小频率，更新最小频率
        }
      }
    }
    // 将节点添加到新频率列表
    if (freqMap_.find(newFreq) == freqMap_.end()) {
      freqMap_[newFreq] = std::list<NodePtr>(); // 初始化新频率
    }
    freqMap_[newFreq].push_back(node); // 将节点添加到新频率
    minFreq_ = std::min(minFreq_, newFreq); // 更新最小频率
  }

  void evictLeastRecent() {
    if (freqMap_.empty()) {
      return; // 如果频率映射为空，直接返回
    }
    auto it = freqMap_.find(minFreq_);
    if (it == freqMap_.end()) {
      return; // 如果最小频率列表为空，直接返回
    }
    if (it->second.empty()) {
      freqMap_.erase(it); // 如果最小频率列表为空，移除该频率
      if (freqMap_.empty()) {
        minFreq_ = 0; // 如果频率映射为空，重置最小频率
        return;
      }
      it = freqMap_.begin(); // 获取下一个最小频率
    }
    NodePtr leastRecentNode = it->second.front(); // 获取最少使用的节点
    it->second.pop_front(); // 从频率列表中移除节点
    if (it->second.empty()) {
      freqMap_.erase(it); // 如果频率列表为空，移除该频率
      if (!freqMap_.empty()) {
        minFreq_ = freqMap_.begin()->first; // 更新最小频率为下一个最小频率
      } else {
        minFreq_ = 0; // 如果频率映射为空，重置最小频率
      }
    }
    if (ghostCache_.size() >= ghostCapacity_) {
      removeOldestGhostNode(); // 如果影子缓存已满，移除最旧的影子节点
    }
    addToGhostCache(leastRecentNode); // 将 leastRecentNode 添加到影子缓存
    mainCache_.erase(leastRecentNode->getKey()); // 从主缓存中移除节点
  }

  void removeFromGhostCache(NodePtr node) {
    if (node && node->prev_ && node->next) {
      // 从影子缓存链表中移除节点
      node->prev_->next =
          node->next; // 前一个节点的 next 指向当前节点的下一个节点
      if (node->next) {
        node->next->prev_ = node->prev_; // 下一个节点的 prev_ 指向前一个节点
        node->next = nullptr; // 清除 next 指针，避免悬挂指针
        node->prev_ = nullptr; // 清除 prev_ 指针，避免悬挂指针
      }
    }
  }

  void addToGhostCache(NodePtr node) {
    if (node) {
      node->next = ghostTail_;
      node->prev_ = ghostTail_->prev_;
      if (ghostTail_->prev_) {
        ghostTail_->prev_->next = node; // 前一个节点的 next 指向新节点
      }
      ghostTail_->prev_ = node; // 影子缓存尾节点的 prev_指向新节点
      if (!ghostHead_->next) {
        ghostHead_->next = node; // 如果影子缓存头节点的 next 为空
      }
      ghostCache_[node->getKey()] = node; // 将节点添加到影子缓存
    }
  }

  void removeOldestGhostNode() {
    NodePtr oldestNode = ghostHead_->next; // 获取影子缓存中的第一个节点
    if (oldestNode && oldestNode != ghostTail_) {
      removeFromGhostCache(oldestNode);        // 从影子缓存链表中移除
      ghostCache_.erase(oldestNode->getKey()); // 从影子缓存映射中移除节点
      oldestNode->next = nullptr; // 清除 next 指针，避免悬挂指针
      oldestNode->prev_ = nullptr;  // 清除 prev_ 指针，避免悬挂指针
      oldestNode.reset(); // 释放节点资源
    }
  }

private:
  size_t capacity_;           // 缓存容量
  size_t ghostCapacity_;      // 影子缓存容量(淘汰缓存)
  size_t transformThreshold_; // 转换阈值
  size_t minFreq_;            // 最小访问频率
  std::mutex mutex_;          // 互斥锁

  NodeMap mainCache_;  // 主缓存
  NodeMap ghostCache_; // 影子缓存
  FreqMap freqMap_;    // 访问频率映射

  NodePtr ghostHead_; // 影子缓存头节点
  NodePtr ghostTail_; // 影子缓存尾节点
};

} // namespace CacheMgr