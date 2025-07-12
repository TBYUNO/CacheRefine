#pragma once

namespace CacheMgr {

template <typename Key, typename Value> class CacheBase {
public:
  virtual ~CacheBase() = default;

  /// @brief 添加缓存
  /// @param key 关键字
  /// @param val 缓存内容
  virtual void put(const Key& key, const Value& val) = 0;

  /// @brief 访问缓存
  /// @param key 待访问的缓存关键字，一个传入参数
  /// @param val 待访问的缓存内容，以引用传出参数形式返回
  /// @return 访问结果，访问成功返回true
  virtual bool get(const Key& key, Value &val) = 0;

  /// @brief 访问缓存
  /// @param key 待访问的缓存关键字
  /// @return 访问结果
  virtual Value get(const Key& key) = 0;
};

} // namespace CacheMgr