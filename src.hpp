#ifndef SRC_HPP
#define SRC_HPP
#include <cstddef>

/**
 * 枚举类，用于枚举可能的置换策略
 */
enum class ReplacementPolicy { kDEFAULT = 0, kFIFO, kLRU, kMRU, kLRU_K };

/**
 * @brief 该类用于维护每一个页对应的信息以及其访问历史，用于在尝试置换时查询需要的信息。
 */
class PageNode {
public:
  PageNode() : page_id_(0), timestamp_(0), access_count_(0),
               access_history_(nullptr), history_capacity_(0) {}

  explicit PageNode(std::size_t page_id, std::size_t k)
    : page_id_(page_id), timestamp_(0), access_count_(0),
      access_history_(nullptr), history_capacity_(k) {
    if (k > 0) {
      access_history_ = new std::size_t[k];
      for (std::size_t i = 0; i < k; ++i) {
        access_history_[i] = 0;
      }
    }
  }

  ~PageNode() {
    if (access_history_ != nullptr) {
      delete[] access_history_;
      access_history_ = nullptr;
    }
  }

  // Copy constructor
  PageNode(const PageNode& other)
    : page_id_(other.page_id_), timestamp_(other.timestamp_),
      access_count_(other.access_count_), history_capacity_(other.history_capacity_) {
    if (other.access_history_ != nullptr && history_capacity_ > 0) {
      access_history_ = new std::size_t[history_capacity_];
      for (std::size_t i = 0; i < history_capacity_; ++i) {
        access_history_[i] = other.access_history_[i];
      }
    } else {
      access_history_ = nullptr;
    }
  }

  // Copy assignment
  PageNode& operator=(const PageNode& other) {
    if (this != &other) {
      if (access_history_ != nullptr) {
        delete[] access_history_;
      }

      page_id_ = other.page_id_;
      timestamp_ = other.timestamp_;
      access_count_ = other.access_count_;
      history_capacity_ = other.history_capacity_;

      if (other.access_history_ != nullptr && history_capacity_ > 0) {
        access_history_ = new std::size_t[history_capacity_];
        for (std::size_t i = 0; i < history_capacity_; ++i) {
          access_history_[i] = other.access_history_[i];
        }
      } else {
        access_history_ = nullptr;
      }
    }
    return *this;
  }

  void RecordAccess(std::size_t current_time) {
    timestamp_ = current_time;

    // Update access history for LRU-K
    if (access_history_ != nullptr && history_capacity_ > 0) {
      // Shift history
      for (std::size_t i = history_capacity_ - 1; i > 0; --i) {
        access_history_[i] = access_history_[i - 1];
      }
      access_history_[0] = current_time;
    }

    ++access_count_;
  }

  std::size_t GetPageId() const { return page_id_; }
  std::size_t GetTimestamp() const { return timestamp_; }
  std::size_t GetAccessCount() const { return access_count_; }
  std::size_t GetKthLastAccess(std::size_t k) const {
    if (access_history_ == nullptr || k == 0 || k > history_capacity_) {
      return 0;
    }
    return access_history_[k - 1];
  }

private:
  std::size_t page_id_;
  std::size_t timestamp_;         // Last access time
  std::size_t access_count_;      // Total access count
  std::size_t* access_history_;   // Array to store k most recent access times
  std::size_t history_capacity_;  // Size of history array (k value)
};

class ReplacementManager {
public:
  constexpr static std::size_t npos = -1;

  ReplacementManager() = delete;

  /**
   * @brief 初始化整个类
   * @param max_size 缓存池可以容纳的页数量的上限
   * @param k LRU-K所基于的常数k，在类销毁前不会变更
   * @param default_policy 在置换时，如果没有显式指示，则默认使用default_policy作为策略
   * @note 我们将保证default_policy的值不是ReplacementPolicy::kDEFAULT。
   */
  ReplacementManager(std::size_t max_size, std::size_t k, ReplacementPolicy default_policy)
    : max_size_(max_size), k_(k), default_policy_(default_policy),
      current_size_(0), current_time_(0), pages_(nullptr) {
    if (max_size_ > 0) {
      pages_ = new PageNode*[max_size_];
      for (std::size_t i = 0; i < max_size_; ++i) {
        pages_[i] = nullptr;
      }
    }
  }

  /**
   * @brief 析构函数
   * @note 我们将对代码进行Valgrind Memcheck，请保证你的代码不发生内存泄漏
   */
  ~ReplacementManager() {
    if (pages_ != nullptr) {
      for (std::size_t i = 0; i < max_size_; ++i) {
        if (pages_[i] != nullptr) {
          delete pages_[i];
          pages_[i] = nullptr;
        }
      }
      delete[] pages_;
      pages_ = nullptr;
    }
  }

  /**
   * @brief 重设当前默认的缓存置换政策
   * @param default_policy 新的默认政策，保证default_policy不是ReplacementPolicy::kDEFAULT
   */
  void SwitchDefaultPolicy(ReplacementPolicy default_policy) {
    default_policy_ = default_policy;
  }

  /**
   * @brief 访问某个页面。
   * @param page_id 访问页的编号
   * @param evict_id 需要被置换的页编号，如果不需要置换请将其设置为npos
   * @param policy 如果需要置换，那么置换所基于的策略
   * (a) 若访问的页已经在缓存池中，那么直接记录其访问信息。
   * (b) 若访问的页不在缓存池中，那么：
   *    1. 若缓存池已满，就从中依照policy置换一个页（彻底删除其对应节点），并将新访问的页加入缓存池，记录其访问
   *    2. 若缓存池未满，则直接将其加入缓存池并记录其访问
   * @note 我们不保证page_id在调用间连续，也不保证page_id的范围，只保证page_id在std::size_t内
   */
  void Visit(std::size_t page_id, std::size_t &evict_id, ReplacementPolicy policy = ReplacementPolicy::kDEFAULT) {
    evict_id = npos;

    if (policy == ReplacementPolicy::kDEFAULT) {
      policy = default_policy_;
    }

    // Check if page is already in cache
    int found_idx = -1;
    for (std::size_t i = 0; i < current_size_; ++i) {
      if (pages_[i] != nullptr && pages_[i]->GetPageId() == page_id) {
        found_idx = i;
        break;
      }
    }

    ++current_time_;

    if (found_idx != -1) {
      // Page already in cache, just update access time
      pages_[found_idx]->RecordAccess(current_time_);
    } else {
      // Page not in cache
      if (current_size_ < max_size_) {
        // Cache not full, add directly
        PageNode* new_node = new PageNode(page_id, k_);
        new_node->RecordAccess(current_time_);
        pages_[current_size_] = new_node;
        ++current_size_;
      } else {
        // Cache full, need to evict
        evict_id = TryEvict(policy);

        // Find and remove the evicted page
        int evict_idx = -1;
        for (std::size_t i = 0; i < current_size_; ++i) {
          if (pages_[i] != nullptr && pages_[i]->GetPageId() == evict_id) {
            evict_idx = i;
            break;
          }
        }

        if (evict_idx != -1) {
          delete pages_[evict_idx];
          pages_[evict_idx] = nullptr;

          // Compact array
          for (std::size_t i = evict_idx; i < current_size_ - 1; ++i) {
            pages_[i] = pages_[i + 1];
          }
          pages_[current_size_ - 1] = nullptr;
          --current_size_;
        }

        // Add new page
        PageNode* new_node = new PageNode(page_id, k_);
        new_node->RecordAccess(current_time_);
        pages_[current_size_] = new_node;
        ++current_size_;
      }
    }
  }

  /**
   * @brief 强制地删除特定的页（无论缓存池是否已满）
   * @param page_id 被删除页的编号
   * @return 如果成功删除，则返回true; 如果该页不存在于缓存池中，则返回false
   * 如果page_id存在于缓存池中，则删除它；否则，直接返回false
   */
  bool RemovePage(std::size_t page_id) {
    int found_idx = -1;
    for (std::size_t i = 0; i < current_size_; ++i) {
      if (pages_[i] != nullptr && pages_[i]->GetPageId() == page_id) {
        found_idx = i;
        break;
      }
    }

    if (found_idx == -1) {
      return false;
    }

    delete pages_[found_idx];
    pages_[found_idx] = nullptr;

    // Compact array
    for (std::size_t i = found_idx; i < current_size_ - 1; ++i) {
      pages_[i] = pages_[i + 1];
    }
    pages_[current_size_ - 1] = nullptr;
    --current_size_;

    return true;
  }

  /**
   * @brief 查询特定策略下首先被置换的页
   * @param policy 置换策略
   * @return 当前策略下会被置换的页的编号。若缓存池没满，则返回npos
   * 不对缓存池做任何修改，只查询在需要置换的情况下，基于给定的政策，应该置换哪个页。
   * @note 如果缓存池没有满，请直接返回npos
   */
  [[nodiscard]] std::size_t TryEvict(ReplacementPolicy policy = ReplacementPolicy::kDEFAULT) const {
    if (current_size_ < max_size_) {
      return npos;
    }

    if (policy == ReplacementPolicy::kDEFAULT) {
      policy = default_policy_;
    }

    if (current_size_ == 0) {
      return npos;
    }

    std::size_t evict_idx = 0;

    switch (policy) {
      case ReplacementPolicy::kFIFO: {
        // Evict the page with smallest timestamp (earliest added)
        std::size_t min_time = pages_[0]->GetTimestamp();
        for (std::size_t i = 1; i < current_size_; ++i) {
          std::size_t first_access = pages_[i]->GetKthLastAccess(pages_[i]->GetAccessCount());
          std::size_t min_first_access = pages_[evict_idx]->GetKthLastAccess(pages_[evict_idx]->GetAccessCount());

          if (first_access < min_first_access) {
            evict_idx = i;
          }
        }
        break;
      }

      case ReplacementPolicy::kLRU: {
        // Evict the page with smallest timestamp (least recently used)
        std::size_t min_time = pages_[0]->GetTimestamp();
        for (std::size_t i = 1; i < current_size_; ++i) {
          if (pages_[i]->GetTimestamp() < min_time) {
            min_time = pages_[i]->GetTimestamp();
            evict_idx = i;
          }
        }
        break;
      }

      case ReplacementPolicy::kMRU: {
        // Evict the page with largest timestamp (most recently used)
        std::size_t max_time = pages_[0]->GetTimestamp();
        for (std::size_t i = 1; i < current_size_; ++i) {
          if (pages_[i]->GetTimestamp() > max_time) {
            max_time = pages_[i]->GetTimestamp();
            evict_idx = i;
          }
        }
        break;
      }

      case ReplacementPolicy::kLRU_K: {
        // Evict based on k-th last access time
        // If access count < k, consider k-th access time as -infinity (0)
        // Among pages with access_count < k, choose the one with earliest first access
        // Otherwise, choose the one with earliest k-th access

        for (std::size_t i = 1; i < current_size_; ++i) {
          bool evict_has_k = pages_[evict_idx]->GetAccessCount() >= k_;
          bool i_has_k = pages_[i]->GetAccessCount() >= k_;

          if (!evict_has_k && !i_has_k) {
            // Both don't have k accesses, compare first access
            std::size_t evict_first = pages_[evict_idx]->GetKthLastAccess(pages_[evict_idx]->GetAccessCount());
            std::size_t i_first = pages_[i]->GetKthLastAccess(pages_[i]->GetAccessCount());
            if (i_first < evict_first) {
              evict_idx = i;
            }
          } else if (!evict_has_k && i_has_k) {
            // evict_idx doesn't have k accesses, prioritize it
            // No change needed
          } else if (evict_has_k && !i_has_k) {
            // i doesn't have k accesses, prioritize it
            evict_idx = i;
          } else {
            // Both have k accesses, compare k-th access
            std::size_t evict_kth = pages_[evict_idx]->GetKthLastAccess(k_);
            std::size_t i_kth = pages_[i]->GetKthLastAccess(k_);
            if (i_kth < evict_kth) {
              evict_idx = i;
            }
          }
        }
        break;
      }

      default:
        break;
    }

    return pages_[evict_idx]->GetPageId();
  }

  /**
   * @brief 返回当前缓存管理器是否为空。
   */
  [[nodiscard]] bool Empty() const {
    return current_size_ == 0;
  }

  /**
   * @brief 返回当前缓存管理器是否已满（即是否页数量已经达到上限）
   */
  [[nodiscard]] bool Full() const {
    return current_size_ >= max_size_;
  }

  /**
   * @brief 返回当前缓存管理器中页的数量
   */
  [[nodiscard]] std::size_t Size() const {
    return current_size_;
  }

private:
  std::size_t max_size_;
  std::size_t k_;
  ReplacementPolicy default_policy_;
  std::size_t current_size_;
  std::size_t current_time_;
  PageNode** pages_;  // Array of pointers to PageNode
};
#endif
