/*
ARC(Adaptive Replacement Cache)
ARC:
自适应替换缓存（ARC）是一种结合了最近最少使用（LRU）和最不经常使用（LFU）缓存策略优点的缓存算法。它根据
LRU 和 LFU 缓存的命中率动态调整其大小，从而提供一种更适应性强且高效的缓存机制
它维护两个独立的列表：一个用于最近使用的项目（LRU），另一个用于频繁使用的项目（LFU）。该算法通过在这两个列表之间取得平衡来确定哪些项目保留在缓存中，哪些项目被淘汰，从而能够适应随时间变化的访问模式
此 ARC 实现结合了 LRU 和 LFU 策略，根据 LRU 和 LFU
缓存的命中率动态调整其大小。它通过访问历史记录来确定哪些项目最有价值保留在缓存中，并使用两级结构
特点：
    1. 自适应性：
        ARC 动态调整两个缓存区域的大小：T1 （最近使用的元素） 和
T2（频繁访问的元素）。 根据访问模式自动调整策略，适应不同的工作负载。
    2. 缓存分区：
        将缓存分为两个部分：
            LRU-like 区域：处理新进入的访问数据（类似最近最少使用，LRU）
            LFU-like 区域：处理频繁访问的数据
    3. 避免缓存污染：
        ARC
在不同的访问模式（如频繁访问与一次性访问）之间表现较好，因为它可以动态调整缓存分区的大小。
    4. 命中率高：
        在混合访问模式下，ARC 通常能够提供比单一策略更高的缓存命中率。
优点：
    自适应性强，适合工作负载模式变化频繁的场景。
    比单纯的 LFU 或 LRU 更能处理缓存污染问题。
缺点：
    实现复杂性较高，维护多种数据结构（如双链表和哈希表）可能增加 CPU 开销。
    在工作负载高度一致（如完全频繁访问）优势不明显。
ARC 解决的问题
    1. 解决了 LRU 的循环缓存问题
        问题描述：
            在 LRU
中，如果缓存中有热点数据（频繁访问的少量数据），但新的数据不断进入，可能导致热点数据被淘汰，出现缓存抖动（thrashing）。
        ARC的解决方案：
            ARC 使用了两个队列来分别跟踪最近访问（类似 LRU）和经常访问（类似
LFU）的数据，并根据访问模式动态调整这两部分缓存的大小，从而避免热点数据被过早淘汰。
    2. 解决了 LFU 的冷启动问题
        问题描述：
            在 LFU
中，新加入的缓存项起初频率低，可能在尚未证明其重要性时就被淘汰。 ARC的解决方案：
            ARC 保留了一个专门存储最近访问但被淘汰的数据队列（ghost
list），帮助识别新数据的价值。如果某个新数据被多次访问，可以快速将其提升为频繁访问的数据。
    3. 动态适应工作负载
        问题描述：
            LRU 和 LFU 在固定的策略下，难以同时适应短期热点和长期热点数据。
        ARC的解决方案：
            ARC 动态调整 LRU 和 LFU 队列的比例，能够同时处理短期和长期热点数据。
*/
#pragma once
#include <memory>

namespace CacheMgr {

template <typename Key, typename Value> class ARCLRUCache; // 前向声明
template <typename Key, typename Value> class ARCLFUCache; // 前向声明

template <typename Key, typename Value> class ARCNode {
public:
  ARCNode() : count_(1), prev_(nullptr), next(nullptr) {}
  ARCNode(Key key, Value val)
      : key_(key), val_(val), count_(1), prev_(nullptr), next(nullptr) {}

  ~ARCNode() = default;

  Key getKey() const { return key_; }

  Value getValue() const { return val_; }

  void setValue(const Value &val) { val_ = val; }

  size_t getAccessCount() const { return count_; }

  void incrementAccessCount() { ++count_; }

private:
  Key key_;                                 // 关键字，索引
  Value val_;                               // 缓存值
  size_t count_;                            // 访问次数
  std::shared_ptr<ARCNode<Key, Value>> prev_; // 前置缓存，弱指针避免重复引用
  std::shared_ptr<ARCNode<Key, Value>> next; // 后置缓存

public:
  friend class ARCLRUCache<Key, Value>; // 允许 ARCLRUCache 访问私有成员
  friend class ARCLFUCache<Key, Value>; // 允许 ARCLFUCache 访问私有成员
};

} // namespace CacheMgr