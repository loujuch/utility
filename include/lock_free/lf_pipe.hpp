/**
 *************************************************************************
 * @file lf_pipe.hpp
 * @author bit
 * @brief 非阻塞的一读一写无锁队列
 *
 * @example 该管道 read 时不保证读到数据，该保证通常需要由业务提供。一个具有此保证的简要使用示例如下：
 *  template <typename T, unsigned N, unsigned S>
 *  class Queue {
 *  public:
 *      bool push(const T& value) {
 *          if (!mRunning) {
 *              return false;
 *          }
 *          mPipe.write(value, false);
 *          auto readflag = mPipe.flush();
 *          if (!readflag) {
 *              {
 *                  std::unique_lock lock(mMutex);
 *                  mHasNotify = true;
 *              }
 *              mCond.notify_one();
 *          }
 *          return true;
 *      }

 *      bool pop(T& value) {
 *          bool readflag = true;
 *          do {
 *              if (!readflag) {
 *                  std::unique_lock lock(mMutex);
 *                  mCond.wait(lock, [this]() { return mHasNotify || !mRunning; });
 *                  mHasNotify = false;
 *                  if (!mRunning) break;
 *              }
 *              readflag = mPipe.read(value);
 *          } while (!readflag);
 *          return readflag;
 *      }

 *      void ASyncSocket::close() {
 *          {
 *              std::unique_lock lock(mMutex);
 *              mRunning = false;
 *          }
 *          mCond.notify_one();
 *      }
 *  private:
 *      Pipe<T, N, S> mPipe;
 *      bool mRunning{ true };
 *      bool mHasNotify{ false };
 *      std::mutex mMutex;
 *      std::condition_variable mCond;
 *  };
 *
 * @copyright Copyright (c) 2026 bit
 * ************************************************************************
 */
#ifndef _LOCK_FREE_PIPE_HPP__
#define _LOCK_FREE_PIPE_HPP__

#include "lf_chunk_list.hpp"

namespace bit {
namespace utility {
namespace lock_free {

/**
 *************************************************************************
 * @brief 非阻塞的一读一写无锁队列
 * @tparam T 队列元素类型
 * @tparam N Chunk 大小
 * @tparam S 缓存大小
 * ************************************************************************
 */
template <typename T, uint32_t N = 128u, uint32_t S = 1u>
class Pipe {
public:
    /**
     *************************************************************************
     * @brief 构造 Pipe 对象
     * ************************************************************************
     */
    inline Pipe() noexcept {
        // chunk_list 的尾指针加1（当前为1）
        // 开始back_chunk为空，现在 back_chunk 指向第一个 chunk_t 块的第一个位置
        // 即现在 back 指向一个未初始化的值
        mChunkList.push();

        // 让 r、w、f、c 四个指针都指向 back，注意此时 back 为脏值
        mReadEnd = mLastFlushEnd = mFlushEnd = &mChunkList.back();
        mCommitEnd.store(&mChunkList.back());
    }

    /**
     *************************************************************************
     * @brief 析构  Pipe 对象
     * ************************************************************************
     */
    inline ~Pipe() noexcept = default;

    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    Pipe(Pipe&&) = delete;
    Pipe& operator=(Pipe&&) = delete;

    /**
     *************************************************************************
     * @brief 写入数据
     * @param[in] value_ 需要拷贝的数据
     * @param[in] incomplete_ 写入是否还没完成。如果为 true，不会修改flush指针，即这部分数据不会让读线程看到
     * ************************************************************************
     */
    inline void write(const T& value_, bool incomplete_) {
        write(incomplete_, value_);
    }

    /**
     *************************************************************************
     * @brief 写入数据
     * @param[in] value_ 需要移动的数据
     * @param[in] incomplete_ 写入是否还没完成。如果为 true，不会修改flush指针，即这部分数据不会让读线程看到
     * ************************************************************************
     */
    inline void write(T&& value_, bool incomplete_) {
        write(incomplete_, std::move(value_));
    }

    /**
     *************************************************************************
     * @brief 写入数据
     * @tparam _Args 参数类型
     * @param[in] incomplete_ 写入是否还没完成。如果为 true，不会修改flush指针，即这部分数据不会让读线程看到
     * @param[in] args 构造使用的参数
     * ************************************************************************
     */
    template <typename ... _Args>
    inline void write(bool incomplete_, _Args&& ... args) {
        // 将数据写入队尾并更新指针
        ctor(&mChunkList.back(), std::forward<_Args>(args)...);
        mChunkList.push();

        if (!incomplete_) { // 如果 mFlushEnd 不更新，flush 不会更新对应数据
            mFlushEnd = &mChunkList.back(); // 记录要刷新的位置
        }
    }

    /**
     *************************************************************************
     * @brief 推出尾部未刷新的数据
     * @param[out] value_ 尾部推出的数据
     * @return 如果有数据推出，返回 true；否则返回 false
     * ************************************************************************
     */
    inline bool unwrite(T& value_) {
        if (mFlushEnd == &mChunkList.back()) {
            return false;
        }
        mChunkList.unpush();
        value_ = std::move(mChunkList.back());
        dtor(&mChunkList.back());
        return true;
    }

    /**
     *************************************************************************
     * @brief 刷新写入并提交的数据，使其能被读取
     * @return 如果 flush 前 读线程 尝试 read 并失败，返回 false；否则返回 true
     * @note 返回 false 通常意味读线程在休眠，此时需要唤醒读线程
     * ************************************************************************
     */
    inline bool flush() noexcept {
        // 不需要刷新，即是还没有新元素加入
        if (mLastFlushEnd == mFlushEnd) {
            return true;
        }

        if (mCommitEnd.compare_exchange_strong(mLastFlushEnd, mFlushEnd)) {
            // 读端还有数据可读取
            mLastFlushEnd = mFlushEnd;
            return true;
        } else {
            // mCommitEnd != mLastFlushEnd，认为 mCommitEnd 被设为 NULL，读端已经 sleep
            mCommitEnd.store(mFlushEnd);
            mLastFlushEnd = mFlushEnd;

            // 线程看到flush返回false之后会发送一个消息给读线程，这需要写业务去做处理
            return false;
        }
    }

    /**
     *************************************************************************
     * @brief 查看是否有可读的数据
     * @return 有可读数据返回 true，否则返回 false
     * ************************************************************************
     */
    inline bool check_read() noexcept {
        // 判断是否在前几次调用read函数时已经预取数据了
        // 注意，r 指向第一个有效数据的下一位，因此 &mChunkList.front() == mReadEnd 实际上是没有数据的
        if (&mChunkList.front() != mReadEnd && mReadEnd) {
            return true;
        }

        // 此时 mReadEnd 和 mChunkList.front() 相等
        // 如果 mCommitEnd 和 mChunkList.front() 相等，则无法预取数据
        // 如果 mCommitEnd 和 mChunkList.front() 不相等，则能够预取数据，更新 mReadEnd
        if (mCommitEnd.compare_exchange_strong(mReadEnd, nullptr)) {
            return false;
        }

        return true;
    }

    /**
     *************************************************************************
     * @brief 读取数据
     * @param[out] value_ 读取的数据
     * @return 成功读取数据返回 true，否则返回 false
     * ************************************************************************
     */
    inline bool read(T& value_) {
        // 预读数据
        if (!check_read()) {
            return false;
        }

        value_ = std::move(mChunkList.front());
        dtor(&mChunkList.front());
        mChunkList.pop();
        return true;
    }
private:
    /**
     *************************************************************************
     * @brief 在指定内存块构造对象
     * @param[in] memory 内存块地址
     * @param[in] args 构造参数
     * ************************************************************************
     */
    template <typename ... _Args>
    void ctor(T* memory, _Args&& ... args) noexcept(std::is_nothrow_constructible<T, _Args...>::value) {
        if (memory) new (memory) T(std::forward<_Args>(args)...);
    }

    /**
     *************************************************************************
     * @brief 析构指定对象
     * @param[in] ptr 需析构的对象
     * ************************************************************************
     */
    void dtor(T* ptr) noexcept(std::is_nothrow_destructible<T>::value) {
        if constexpr (!std::is_fundamental<T>::value) {
            if (ptr) ptr->~T();
        }
    }
private:
    // 内存分配层
    ChunkList<T, N, S> mChunkList;

    // 指向第一个未刷新的元素，只被写线程使用
    T* mLastFlushEnd;

    // 指向第一个还没预提取的元素，只被读线程使用
    T* mReadEnd;

    // 指向下一轮要被刷新的一批元素中的最后一个
    T* mFlushEnd;

    // mCommitEnd 前的数据均为可读（已提交）的数据
    // 读写线程共享的指针，指向每一轮刷新的起点（与预读相关）
    // 当 mCommitEnd 为空时，表示读线程睡眠（只会在读线程中被设置为空）
    AtomicPtr<T> mCommitEnd;
};

} // namespace lock_free
} // namespace utility
} // namespace bit

#endif // _LOCK_FREE_PIPE_HPP__