/**
 *************************************************************************
 * @file lf_object_allocator.hpp
 * @author bit
 * @brief 一个并发安全的对象分配器
 *
 * @copyright Copyright (c) 2026 bit
 * ************************************************************************
 */
#ifndef _LOCK_FREE_OBJECT_ALLOCATOR_HPP__
#define _LOCK_FREE_OBJECT_ALLOCATOR_HPP__

#include "lf_memory_allocator.hpp"

namespace bit {
namespace utility {
namespace lock_free {

/**
 * ************************************************************************
 * @brief 并发安全的对象分配器
 * @tparam T 对象类型
 * @tparam S 缓存大小
 * ************************************************************************
 */
template <typename T, uint32_t S>
class ObjectAllocator {
public:
    /**
     *************************************************************************
     * @brief 分配一个完成构造的对象
     * @tparam _Args 参数类型
     * @param[in] args 构造的参数
     * @return 完成构造的对象，如果未能成功分配返回 nullptr
     * @exception 如果类型 T 的构造函数为 noexcept，则为 noexcept；否则将转发出现的异常
     * ************************************************************************
     */
    template <typename ... _Args>
    inline T* alloc(_Args&& ... args) noexcept(std::is_nothrow_constructible<T, _Args...>::value) {
        T* memory = mMemoryAllocator.alloc();
        if (memory) {
            if constexpr (std::is_nothrow_constructible<T, _Args...>::value) {
                new (memory) T(std::forward<_Args>(args)...);
            } else {
                try {
                    new (memory) T(std::forward<_Args>(args)...);
                } catch (...) {
                    mMemoryAllocator.free(memory);
                    memory = nullptr;
                    throw;
                }
            }
        }
        return memory;
    }

    /**
     *************************************************************************
     * @brief 析构并回收传入对象
     * @param[in] ptr 需要回收的对象
     * @exception 如果类型 T 的析构函数为 noexcept，则为 noexcept；否则将转发出现的异常。注意抛出异常后原指针指向的对象内存已回收，不应再被使用
     * ************************************************************************
     */
    inline void free(T* ptr) noexcept(std::is_nothrow_destructible<T>::value) {
        if (!ptr) return;
        if constexpr (!std::is_fundamental<T>::value) {
            if constexpr (std::is_nothrow_destructible<T>::value) {
                ptr->~T();
            } else {
                try {
                    ptr->~T();
                } catch (...) {
                    mMemoryAllocator.free(ptr);
                    throw;
                }
            }
        }
        mMemoryAllocator.free(ptr);
    }
private:
    MemoryAllocator<T, S> mMemoryAllocator;
};

} // namespace lock_free
} // namespace utility
} // namespace bit

#endif // _LOCK_FREE_OBJECT_ALLOCATOR_HPP__