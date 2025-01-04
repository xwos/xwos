/**
 * @file
 * @brief xwos::sync::DSem
 * @author
 * + 隐星魂 (Roy Sun) <xwos@xwos.tech>
 * @copyright
 * + Copyright © 2015 xwos.tech, All Rights Reserved.
 * > This Source Code Form is subject to the terms of the Mozilla Public
 * > License, v. 2.0. If a copy of the MPL was not distributed with this
 * > file, You can obtain one at <http://mozilla.org/MPL/2.0/>.
 */

#ifndef __xwos_cxx_sync_DSem_hxx__
#define __xwos_cxx_sync_DSem_hxx__

#include <xwos/osal/sync/sem.hxx>
#include <xwos/cxx/sync/Sem.hxx>

namespace xwos {
namespace sync {

/**
 * @defgroup xwos_cxx_sync_DSem 动态信号量
 * @ingroup xwos_cxx_sync
 *
 *
 * ## C++ API
 *
 * 头文件： @ref xwos/cxx/sync/DSem.hxx
 *
 * @{
 */

/**
 * @brief 动态信号量
 */
class DSem
    : public Sem
{
  public:
    /**
     * @brief 动态信号量的等待模式
     */
    enum WaitMode : xwu32_t {
        DSemWait = 0, /**< 等待模式 */
        DSemWaitTimed, /**< 定时等待模式 */
        DSemWaitUninterruptable, /**< 不可中断等待模式 */
        DSemTryWait, /**< 尝试模式（只测试不等待） */
    };

  private:
    xwos_sem_d mSemDesc; /**< 信号量对象描述符 */
    xwer_t mCtorRc; /**< 信号量构造的结果 */

  public:
    /**
     * @brief 构造函数
     */
    DSem(xwssq_t val = 0, xwssq_t max = XWSSQ_MAX) : Sem(nullptr) {
        mCtorRc = xwos_sem_create(&mSemDesc, val, max);
        if (XWOK == mCtorRc) {
            Sem::mSemPtr = mSemDesc.sem;
        }
    }
    ~DSem() { xwos_sem_delete(mSemDesc); } /**< 析构函数 */
    xwer_t getCtorRc() { return mCtorRc; } /**< 获取动态信号量构造的结果 */

    /**
     * @brief 发布信号量
     * @return 错误码
     * @retval XWOK: 没有错误
     * @retval -EFAULT: 空指针
     * @retval -ENEGATIVE: 信号量对象已被冻结
     * @retval -ERANGE: 信号量对象的值已经最大
     * @note
     * + 上下文：任意
     * @details
     * + 此C++API只对未冻结的信号量对象起作用，已冻结的信号量对象将得到错误码 `-ENEGATIVE` 。
     */
    xwer_t post() { return Sem::post(); }

    /**
     * @brief 等待并获取信号量
     * @param[in] mode: 锁模式，取值 @ref DSem::WaitMode
     *   @arg DSem::WaitMode::DSemWait
     *   @arg DSem::WaitMode::DSemWaitTimed
     *   @arg DSem::WaitMode::DSemWaitUninterruptable
     *   @arg DSem::WaitMode::DSemTryWait
     * @param[in] to: 期望唤醒的时间点，
     *                仅当 `mode == DSem::WaitMode::DSemWaitTimed` 时有效。
     * @return 错误码
     * @retval XWOK: 没有错误
     * @retval -EFAULT: 空指针
     * @retval -ETIMEDOUT: 超时
     * @retval -EINTR: 等待被中断
     * @retval -ENOTTHDCTX: 不在线程上下文中
     * @retval -EDSPMPT: 抢占被关闭
     * @retval -EDSBH: 中断底半部被关闭
     * @note
     * + 上下文：
     *   + `mode == DSem::WaitMode::DSemTryWait` : 任意
     *   + `mode != DSem::WaitMode::DSemTryWait` : 线程
     * @details
     * + 等待分为下列几种模式：
     *   + `DSem::WaitMode::DSemWait`: 调用线程会阻塞等待上锁。
     *   + `DSem::WaitMode::DSemWaitTimed` 定时等待模式， `to` 表示等待超时的时间点：
     *     + `to` 通常是未来时间点，即 **当前系统时间** + `delta` ，可以使用 `xwtm_ft(delta)` 表示；
     *     + 如果 `to` 是过去的时间点，将直接返回 `-ETIMEDOUT` 。
     *   + `DSem::WaitMode::DSemWaitUninterruptable` 不可中断等待模式。
     *   + `DSem::WaitMode::DSemTryWait` 尝试模式（只测试不等待）。
     */
    xwer_t wait(enum WaitMode mode = WaitMode::DSemWait, xwtm_t to = XWTM_MAX) {
        xwer_t rc;
        switch (mode) {
            case WaitMode::DSemWait:
                rc = Sem::wait();
                break;
            case WaitMode::DSemWaitTimed:
                rc = Sem::waitTo(to);
                break;
            case WaitMode::DSemWaitUninterruptable:
                rc = Sem::waitUnintr();
                break;
            case WaitMode::DSemTryWait:
                rc = Sem::tryWait();
                break;
            default:
                rc = Sem::wait();
                break;
        }
        return rc;
    }

    /**
     * @brief 冻结信号量
     * @return 错误码
     * @retval XWOK: 没有错误
     * @retval -EFAULT: 空指针
     * @retval -EALREADY: 信号量对象已被冻结
     * @note
     * + 上下文：任意
     * @details
     * + 被冻结的信号量对象不允许增加(V操作)，但可以被测试(P操作)。
     *   测试信号量对象的线程会被加入到信号量对象的等待队列。
     * + 对已经冻结的信号量再次冻结，将返回 `-EALREADY` 。
     */
    xwer_t freeze() { return Sem::freeze(); }

    /**
     * @brief 解冻信号量
     * @return 错误码
     * @retval XWOK: 没有错误
     * @retval -EFAULT: 无效的指针或空指针
     * @retval -EALREADY: 信号量对象未被冻结
     * @note
     * + 上下文：任意
     * @details
     * + 此C++API只对已冻结的信号量对象起作用，
     *   对未冻结的信号量对象调用此函数将返回错误码 `-EALREADY` 。
     */
    xwer_t thaw() { return Sem::thaw(); }

    /**
     * @brief 获取信号量计数器的最大值
     * @return 最大值
     * @note
     * + 上下文：任意
     */
    xwssq_t max() { return Sem::max(); }

    /**
     * @brief 获取信号量计数器的值
     * @return 最大值
     * @note
     * + 上下文：任意
     */
    xwssq_t value() { return Sem::value(); }

    /* 生命周期管理 */
    xwer_t acquire() { return xwos_sem_acquire(mSemDesc); } /**< 增加引用计数 */
    xwer_t release() { return xwos_sem_release(mSemDesc); } /**< 减少引用计数 */
};

/**
 * @} xwos_cxx_sync_DSem
 */

} // namespace sync
} // namespace xwos

#endif /* xwos/cxx/sync/DSem.hxx */
