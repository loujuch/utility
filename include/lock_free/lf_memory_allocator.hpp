/**
 *************************************************************************
 * @file lf_memory_allocator.hpp
 * @author bit
 * @brief 实现了并发安全的内存分配器
 *
 * @copyright Copyright (c) 2025 bit
 * ************************************************************************
 */
#ifndef _LOCK_FREE_MEMORY_ALLOCATOR_HPP__
#define _LOCK_FREE_MEMORY_ALLOCATOR_HPP__

#include "lf_atomic_ptr.hpp"

#include <new>
#include <array>

namespace bit {
namespace utility {
namespace lock_free {

/**
 *************************************************************************
 * @brief 并发安全的内存分配器的主要模板
 * @tparam T 分配的类型。注意，其只创建内存，不会调用对应方法的构造函数
 * @tparam S 内存分配器中缓存池的大小
 * ************************************************************************
 */
template <typename T, uint32_t S>
class MemoryAllocator {
private:
    // 因内部实现中 Index 存储了 2S 的中间变量，因此当 2S >= UINT32_MAX 时 Index 需要升级为 UINT64
    // 因 UINT32_MAX 为奇数，因此当 S <= (UINT32_MAX / 2) 时, 2S < UINT32_MAX
    using IndexType = std::conditional_t<
        (S > (UINT32_MAX / 2)),  // S > 2^31-1 (2147483647)
        std::atomic_uint64_t,
        std::atomic_uint32_t
    >;
public:
    /**
     *************************************************************************
     * @brief 构造 Memory Allocator 对象
     * ************************************************************************
     */
    inline MemoryAllocator() noexcept = default;

    /**
     *************************************************************************
     * @brief 析构  Memory Allocator 对象
     * ************************************************************************
     */
    inline ~MemoryAllocator() noexcept = default;

    MemoryAllocator(const MemoryAllocator&) = delete;
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;
    MemoryAllocator(MemoryAllocator&&) = delete;
    MemoryAllocator& operator=(MemoryAllocator&&) = delete;

    /**
     *************************************************************************
     * @brief 分配相关的内存，当缓存内存在数据时使用缓存，否则使用 MemoryAllocator<T, 0> 进行分配
     * @return 分配成功时返回内存指针，当分配失败时返回 nullptr
     * ************************************************************************
     */
    T* alloc() noexcept {
        // 多线程竞争 head
        T* ptr = nullptr;
        auto head = mHead.load();

        while (true) {
            // 如果当前队列为空，则无法取出数据
            if (head == mTail.load()) {
                break;
            }

            // 模 2S 保证不需要额外判断 Head == Tail 的情况
            auto nHead = (head + 1) % (2 * S);

            // 取出数据，防止更新 mHead 后取到的是 mTail 新写的脏数据
            auto rHead = head < S ? head : head - S;
            ptr = mQueueBuffer[rHead];

            // 进行 CAS 判断是否有其他读线程修改
            if (mHead.compare_exchange_strong(head, nHead)) {
                // 没有其他修改则成功取出
                break;
            }

            // 否则继续尝试判断，注意此时 head 已被更新为新的 mHead
        }

        return ptr ? ptr : mAllocator.alloc();
    }

    /**
     *************************************************************************
     * @brief 释放一个 T 对象的内存
     * @param[in] ptr 释放的内存，允许为 nullptr
     * ************************************************************************
     */
    void free(T* ptr) noexcept {
        if (!ptr) return;

        auto tail = mTail.load();

        do {
            // 如果当前队列为满，则无法放入数据
            // 即 2S 的映射队列满位为S或空位为S
            auto head = mHead.load();
            if (head + S == tail || tail + S == head) {
                break;
            }

            // 模 2S 保证不需要额外判断 Head == Tail 的情况
            auto nTail = (tail + 1) % (2 * S);

            // 进行 CAS 判断是否有其他读线程修改
            if (mTail.compare_exchange_strong(tail, nTail)) {
                // 没有其他修改则成功获得写入位

                // 进行写入
                auto rTail = tail < S ? tail : tail - S;
                mQueueBuffer[rTail] = ptr;
                ptr = nullptr;
                break;
            }

            // 否则继续尝试判断，注意此时 tail 已被更新为新的 mTail
        } while (true);

        if (ptr) mAllocator.free(ptr);
    }
private:
    std::array<T*, S> mQueueBuffer;
    IndexType mHead;
    IndexType mTail;
    MemoryAllocator<T, 0> mAllocator;
};

/**
 * ************************************************************************
 * @brief 并发安全的内存分配器在缓存大小为1时的模板
 * @tparam T 分配的类型。注意，其只创建内存，不会调用对应方法的构造函数
 * ************************************************************************
 */
template <typename T>
class MemoryAllocator<T, 1> {
public:
    /**
     *************************************************************************
     * @brief 构造 Memory Allocator 对象
     * ************************************************************************
     */
    inline MemoryAllocator() noexcept : mPtr{ nullptr } {}

    /**
     *************************************************************************
     * @brief 析构  Memory Allocator 对象
     * ************************************************************************
     */
    inline ~MemoryAllocator() noexcept { free(nullptr); }

    MemoryAllocator(const MemoryAllocator&) = delete;
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;
    MemoryAllocator(MemoryAllocator&&) = delete;
    MemoryAllocator& operator=(MemoryAllocator&&) = delete;

    /**
     *************************************************************************
     * @brief 分配相关的内存，当缓存内存在数据时使用缓存，否则使用 MemoryAllocator<T, 0> 进行分配
     * @return 分配成功时返回内存指针，当分配失败时返回 nullptr
     * ************************************************************************
     */
    inline T* alloc() noexcept {
        auto tmp = mPtr.exchange(nullptr);
        return tmp ? tmp : mAllocator.alloc();
    }

    /**
     *************************************************************************
     * @brief 释放一个 T 对象的内存
     * @param[in] ptr 释放的内存，允许为 nullptr
     * ************************************************************************
     */
    inline void free(T* ptr) noexcept {
        auto tmp = mPtr.exchange(ptr);
        if (tmp) mAllocator.free(tmp);
    }
private:
    AtomicPtr<T> mPtr;
    MemoryAllocator<T, 0> mAllocator;
};

/**
 *************************************************************************
 * @brief 并发安全的内存分配器在缓冲区为0时的模板，实际上相当于相关方法的薄封装
 * @tparam T 分配的类型。注意，其只创建内存，不会调用对应方法的构造函数
 * ************************************************************************
 */
template <typename T>
class MemoryAllocator<T, 0> {
public:
    /**
     *************************************************************************
     * @brief 分配一个 T 对象的内存
     * @return 当内存分配成功时返回内存指针，否则返回 nullptr
     * ************************************************************************
     */
    inline T* alloc() noexcept {
        return static_cast<T*>(::operator new(sizeof(T), std::nothrow));
    }

    /**
     *************************************************************************
     * @brief 释放一个 T 对象的内存
     * @param[in] ptr 释放的内存，允许为 nullptr
     * ************************************************************************
     */
    inline void free(T* ptr) noexcept {
        ::operator delete(ptr, std::nothrow);
    }
};


} // namespace lock_free
} // namespace utility
} // namespace bit


#endif // _LOCK_FREE_MEMORY_ALLOCATOR_HPP__