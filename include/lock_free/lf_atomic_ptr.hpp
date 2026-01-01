/**
 *************************************************************************
 * @file lf_atomic_ptr.hpp
 * @author bit
 * @brief 简单的原子指针容器
 *
 * @copyright Copyright (c) 2025 bit
 * ************************************************************************
 */
#ifndef _LOCK_FREE_ATOMIC_PTR_HPP__
#define _LOCK_FREE_ATOMIC_PTR_HPP__

#include <atomic>

namespace bit {
namespace utility {
namespace lock_free {

template <typename T>
using AtomicPtr = std::atomic<T*>;

} // namespace lock_free
} // namespace utility
} // namespace bit

#endif // _LOCK_FREE_ATOMIC_PTR_HPP__