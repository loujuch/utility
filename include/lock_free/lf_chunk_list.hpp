/**
 *************************************************************************
 * @file lf_chunk_list.hpp
 * @author bit
 * @brief 一读一写时线程安全的 Chunk 块的分配、管理类
 *
 * @copyright Copyright (c) 2026 bit
 * ************************************************************************
 */
#ifndef _LOCK_FREE_CHUNK_LIST_HPP__
#define _LOCK_FREE_CHUNK_LIST_HPP__

#include "lf_memory_allocator.hpp"

namespace bit {
namespace utility {
namespace lock_free {

/**
 *************************************************************************
 * @brief Chunk 的管理类
 * @tparam T 实际数据类型
 * @tparam N 一个 Chunk 的大小
 * @tparam S 缓存大小
 * ************************************************************************
 */
template <typename T, uint32_t N, uint32_t S>
class ChunkList {
private:
    struct Chunk {
        T data[N];
        struct Chunk* prev;
        struct Chunk* next;
    };
private:
    using AllocatorType = MemoryAllocator<Chunk, S>;
    using ChunkType = Chunk;
    using ChunkPtr = ChunkType*;
public:
    /**
     *************************************************************************
     * @brief 构造 Chunk List 对象
     * @note 未处理分配失败返回 nullptr 的情况
     * ************************************************************************
     */
    inline ChunkList() noexcept {
        auto pchunk = mAllocator.alloc();
        mBeginChunk = pchunk;
        mBeginPos = 0;
        mBackChunk = nullptr; //mBackChunk总是指向队列中最后一个元素所在的chunk，现在还没有元素，所以初始为空
        mBackPos = 0;
        mEndChunk = mBeginChunk; //mEndChunk总是指向链表的最后一个chunk
        mEndPos = 0;
    }

    /**
     *************************************************************************
     * @brief 析构  Chunk List 对象
     * ************************************************************************
     */
    inline ~ChunkList() noexcept {
        while (true) {
            if (mBeginChunk == mEndChunk) {
                mAllocator.free(mBeginChunk);
                break;
            }

            ChunkPtr tmp = mBeginChunk;
            mBeginChunk = mBeginChunk->next;
            mAllocator.free(tmp);
        }
    }

    ChunkList(const ChunkList&) = delete;
    ChunkList& operator=(const ChunkList&) = delete;
    ChunkList(ChunkList&&) = delete;
    ChunkList& operator=(ChunkList&&) = delete;

    /**
     *************************************************************************
     * @brief 获取队列头
     * @return 当前队列头的左值引用
     * @note 队列为空时为未定义行为
     * ************************************************************************
     */
    inline T& front() noexcept {
        return mBeginChunk->data[mBeginPos];
    }

    /**
     *************************************************************************
     * @brief 获取队列尾
     * @return 当前队列尾的左值引用
     * @note 队列为空时为未定义行为
     * ************************************************************************
     */
    inline T& back() noexcept {
        return mBackChunk->data[mBackPos];
    }

    /**
     *************************************************************************
     * @brief 推入元素到尾部，只允许写线程操作，因此无需内部同步
     * @note 未处理分配失败返回 nullptr 的情况
     * ************************************************************************
     */
    inline void push() noexcept {
        // 初始化 back
        mBackChunk = mEndChunk;
        mBackPos = mEndPos;

        //mEndPos != N表明这个chunk节点还没有满，无需添加 chunk
        if (++mEndPos != N) {
            return;
        }

        // 需要添加 chunk，取出缓存的 chunk
        ChunkPtr sc = mAllocator.alloc();

        // 将 chunk 挂链
        mEndChunk->next = sc;
        sc->prev = mEndChunk;

        // 更新相关指针
        mEndChunk = mEndChunk->next;
        mEndPos = 0;
    }

    /**
     *************************************************************************
     * @brief 从尾部取出未分配元素，只允许写线程操作，因此无需内部同步
     * @note 未处理分配失败返回 nullptr 的情况
     * @note 队列为空时行为未定义
     * ************************************************************************
     */
    inline void unpush() noexcept {
        // 从尾部删除元素
        if (mBackPos) {
            // 不跨 chunk
            --mBackPos;
        } else { // 跨 chunk
            mBackPos = N - 1;
            mBackChunk = mBackChunk->prev;
        }

        if (mEndPos) {
            // end chunk 不为空
            --mEndPos;
        } else {
            // end chunk 为空
            mEndPos = N - 1;
            mEndChunk = mEndChunk->prev;

            // 缓存最新的 chunk
            mAllocator.free(mEndChunk->next);

            mEndChunk->next = nullptr;
        }
    }

    // 从头部删除元素
    /**
     *************************************************************************
     * @brief 推出头部元素，只允许读线程操作，因此无需内部同步
     * @note 未处理分配失败返回 nullptr 的情况
     * ************************************************************************
     */
    inline void pop() noexcept {
        if (++mBeginPos == N) {
            // 删除满一个chunk才回收chunk
            ChunkPtr o = mBeginChunk;
            mBeginChunk = mBeginChunk->next;
            mBeginChunk->prev = nullptr;
            mBeginPos = 0;

            // 缓存最新的 chunk
            mAllocator.free(o);
        }
    }
private:
    AllocatorType mAllocator;

    // (front/pop) 访问点
    ChunkPtr mBeginChunk; // 链表头结点
    uint32_t mBeginPos;        // 起始点

    // (back/push) 访问点
    ChunkPtr mBackChunk;  // 队列中最后一个元素所在的链表结点
    uint32_t mBackPos;         // 尾部
    ChunkPtr mEndChunk;   // 拿来扩容的，总是指向链表的最后一个结点
    uint32_t mEndPos;
};

/**
 *************************************************************************
 * @brief ChunkList 在 N 为 0 时编译失败
 * ************************************************************************
 */
template <typename T, uint32_t S>
class ChunkList<T, 0, S>;

} // namespace lock_free
} // namespace utility
} // namespace bit


#endif // _LOCK_FREE_CHUNK_LIST_HPP__