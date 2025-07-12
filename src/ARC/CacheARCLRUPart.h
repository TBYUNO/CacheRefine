#pragma once

#include <mutex>
#include <unordered_map>

#include "CacheARCNode.h"

namespace CacheMgr {

template <typename Key, typename Value> class ARCLRUCache {
public:
  using NodeType = ARCNode<Key, Value>;
  using NodePtr = std::shared_ptr<NodeType>;
  using NodeMap = std::unordered_map<Key, NodePtr>;

  explicit ARCLRUCache(size_t capacity, size_t transformThreshold)
      : capacity_(capacity), ghostCapacity_(capacity),
        transformThreshold_(transformThreshold) {
    initializeCache();
  }

  bool get(const Key &key, Value &value, bool &shouldTransform) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mainCache_.find(key);
    if (it != mainCache_.end()) {
      NodePtr node = it->second;
      shouldTransform = updateNodeAccess(node);
      value = node->getValue();
      return true;
    }
    return false;
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

  bool checkGhost(const Key &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ghostCache_.find(key);
    if (it != ghostCache_.end()) {
      removeFromGhostCache(it->second);
      ghostCache_.erase(it);
      return true; // 如果在影子缓存中找到，返回true
    }
    return false;
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
      evictLeastRecent(); // 如果主缓存已满，先移除最少使用的节点
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
    mainHead_ = std::make_shared<NodeType>();
    mainTail_ = std::make_shared<NodeType>();
    mainHead_->next = mainTail_;
    mainTail_->prev_ = mainHead_;

    ghostHead_ = std::make_shared<NodeType>();
    ghostTail_ = std::make_shared<NodeType>();
    ghostHead_->next = ghostTail_;
    ghostTail_->prev_ = ghostHead_;
  }

  bool updateExistingNode(NodePtr node, const Value &value) {
    if (node) {
      node->setValue(value);
      // node->incrementAccessCount();
      moveToFront(node);
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
    addToFront(newNode);
    return true; // 添加成功
  }

  bool updateNodeAccess(NodePtr node) {
    if (node) {
      moveToFront(node);
      node->incrementAccessCount();
      return node->getAccessCount() >= transformThreshold_; // 返回是否需要转换
    }
    return false; // 节点不存在
  }

  void moveToFront(NodePtr node) {
    if (node && node->prev_ && node->next) {
      // 从当前链表中移除节点
      node->prev_->next = node->next;
      if (node->next) {
        node->next->prev_ = node->prev_;
        node->next = nullptr;  // 清除 next 指针，避免悬挂指针
        node->prev_ = nullptr; // 清除 prev_ 指针，避免悬挂指针
      }
    }
    // 将节点添加到链表头部
    addToFront(node);
  }

  void addToFront(NodePtr node) {
    if (node) {
      node->next =
          mainHead_->next; // 将新节点的 next 指向当前头节点的下一个节点
      mainHead_->next->prev_ =
          node; // 将当前头节点的下一个节点的 prev_ 指向新节点
      mainHead_->next = node;  // 将头节点的 next 指向新节点
      node->prev_ = mainHead_; // 将新节点的 prev_ 指向头节点
      if (!mainTail_->prev_) {
        mainTail_->prev_ = node; // 如果尾节点的 prev_ 为空，设置为新节点
      }
    }
  }

  void evictLeastRecent() {
    if (mainTail_) {
      NodePtr leastRecentNode = mainTail_->prev_;
      if (!leastRecentNode || leastRecentNode == mainHead_) {
        return; // 如果尾节点的前一个节点是头节点，说明没有可移除的节点
      }
      // 从主缓存中移除 leastRecentNode
      removeFromMainCache(leastRecentNode);
      // 将 leastRecentNode 添加到影子缓存
      if (ghostCache_.size() >= ghostCapacity_) {
        removeOldestGhostNode(); // 如果影子缓存已满，移除最旧的影子节点
      }
      addToGhostCache(leastRecentNode); // 添加 leastRecentNode 到影子缓存
      mainCache_.erase(leastRecentNode->getKey()); // 从主缓存中移除节点
    }
  }

  void removeFromMainCache(NodePtr node) {
    if (node && node->prev_ && node->next) {
      // 从主缓存链表中移除节点
      node->prev_->next =
          node->next; // 前一个节点的 next 指向当前节点的下一个节点
      if (node->next) {
        node->next->prev_ = node->prev_; // 下一个节点的 prev_ 指向前一个节点
        node->next = nullptr;  // 清除 next 指针，避免悬挂指针
        node->prev_ = nullptr; // 清除 prev_ 指针，避免悬挂指针
      }
    }
  }

  void removeFromGhostCache(NodePtr node) {
    if (node && node->prev_ && node->next) {
      // 从影子缓存链表中移除节点
      node->prev_->next =
          node->next; // 前一个节点的 next 指向当前节点的下一个节点
      if (node->next) {
        node->next->prev_ = node->prev_; // 下一个节点的 prev_  指向前一个节点
        node->next = nullptr;  // 清除 next 指针，避免悬挂指针
        node->prev_ = nullptr; // 清除 prev_ 指针，避免悬挂指针
      }
    }
  }

  void addToGhostCache(NodePtr node) {
    if (!node) {
      return; // 如果节点为空，直接返回
    }
    node->count_ = 1;                   // 重置访问计数
    ghostCache_[node->getKey()] = node; // 将节点添加到影子缓存
    // 将节点添加到影子缓存链表头部
    // 将新节点的 next 指向当前影子缓存头节点的下一个节点
    node->next = ghostHead_->next;
    // 将新节点的 prev_ 指向影子缓存头节点
    node->prev_ = ghostHead_;
    // 将当前影子缓存头节点的下一个节点的 prev_ 指向新节点
    ghostHead_->next->prev_ = node;
    // 将影子缓存头节点的 next 指向新节点
    ghostHead_->next = node;
    if (!ghostTail_->prev_) {
      // 如果影子缓存尾节点的 prev_ 为空，设置为新节点
      ghostTail_->prev_ = node;
    }
  }

  void removeOldestGhostNode() {
    if (ghostTail_) {
      NodePtr oldestGhostNode = ghostTail_->prev_;
      if (!oldestGhostNode || oldestGhostNode == ghostHead_) {
        return; // 如果影子缓存尾节点的前一个节点是头节点，说明没有可移除的节点
      }
      // 从影子缓存中移除 oldestGhostNode
      removeFromGhostCache(oldestGhostNode);
      ghostCache_.erase(oldestGhostNode->getKey()); // 从影子缓存中移除节点
    }
  }

private:
  size_t capacity_;           // 缓存容量
  size_t ghostCapacity_;      // 影子缓存容量(淘汰缓存)
  size_t transformThreshold_; // 转换阈值
  std::mutex mutex_;          // 互斥锁

  NodeMap mainCache_;  // 主缓存
  NodeMap ghostCache_; // 影子缓存

  NodePtr mainHead_;  // 主缓存头节点
  NodePtr mainTail_;  // 主缓存尾节点
  NodePtr ghostHead_; // 影子缓存头节点
  NodePtr ghostTail_; // 影子缓存尾节点
};

} // namespace CacheMgr