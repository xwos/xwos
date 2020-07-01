/**
 * @file
 * @brief XuanWuOS的锁机制：互斥锁
 * @author
 * + 隐星魂 (Roy.Sun) <https://xwos.tech>
 * @copyright
 * + (c) 2015 隐星魂 (Roy.Sun) <https://xwos.tech>
 * > This Source Code Form is subject to the terms of the Mozilla Public
 * > License, v. 2.0. If a copy of the MPL was not distributed with this
 * > file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef __xwos_smp_lock_mutex_h__
#define __xwos_smp_lock_mutex_h__

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********      include      ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
#include <xwos/standard.h>
#include <xwos/object.h>
#include <xwos/lib/xwbop.h>
#include <xwos/lib/bclst.h>
#include <xwos/lib/rbtree.h>
#include <xwos/smp/rtwq.h>

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********       macros      ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********       types       ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
struct xwos_mtxtree;

/**
 * @brief 互斥锁
 * - 上锁的顺序:
 * -- waitqueue.lock
 * -- mutextree.lock/thread.stlock
 */
struct xwlk_mtx {
        struct xwos_object xwobj; /**< C语言面向对象：继承struct xwos_object */

        struct xwos_mtxtree * ownertree; /**< 获得此互斥锁的线程的互斥锁树：
                                              如果为空(NULL)，互斥锁处于未加锁状态。
                                              此成员被锁rtwq.lock保护。*/
        xwsq_t reentrant; /**< 同一线程重复获得此互斥锁的计数器，
                               此成员被锁rtwq.lock保护。*/
        struct xwlib_rbtree_node rbnode; /**< 互斥锁树的链表节点：
                                              此成员被锁ownertree->lock保护。*/
        struct xwlib_bclst_node rbbuddy; /**< 互斥锁树的红黑树节点：
                                              此成员被锁ownertree->lock保护。*/

        struct xwos_rtwq rtwq; /**< 实时等待队列：所有节点按优先级在队列中排队。*/
        xwpr_t sprio; /**< 静态优先级 */
        xwpr_t dprio; /**< 动态优先级：此成员被锁rtwq.lock保护。*/
};

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ********     internal function prototypes    ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
__xwos_code
xwer_t xwlk_mtx_intr(struct xwlk_mtx * mtx, struct xwos_tcb * tcb);

/******** ******** ******** ******** ******** ******** ******** ********
 ********       internal inline function implementations        ********
 ******** ******** ******** ******** ******** ******** ******** ********/
/**
 * @brief 增加对象的引用计数
 * @param mtx: (I) 互斥锁对象指针
 * @return 错误码
 */
static __xw_inline
xwer_t xwlk_mtx_grab(struct xwlk_mtx * mtx)
{
        return xwos_object_grab(&mtx->xwobj);
}

/**
 * @brief 减少对象的引用计数
 * @param mtx: (I) 互斥锁对象指针
 * @return 错误码
 */
static __xw_inline
xwer_t xwlk_mtx_put(struct xwlk_mtx * mtx)
{
        return xwos_object_put(&mtx->xwobj);
}

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ********       API function prototypes       ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
#if defined(XWSMPCFG_LOCK_MTX_MEMSLICE) && (1 == XWSMPCFG_LOCK_MTX_MEMSLICE)
__xwos_init_code
xwer_t xwlk_mtx_cache_init(xwptr_t zone_origin, xwsz_t zone_size);
#endif /* XWSMPCFG_LOCK_MTX_MEMSLICE */

__xwos_api
xwer_t xwlk_mtx_init(struct xwlk_mtx * mtx, xwpr_t sprio);

__xwos_api
xwer_t xwlk_mtx_destroy(struct xwlk_mtx * mtx);

__xwos_api
xwer_t xwlk_mtx_create(struct xwlk_mtx ** ptrbuf, xwpr_t sprio);

__xwos_api
xwer_t xwlk_mtx_delete(struct xwlk_mtx * mtx);

__xwos_api
xwer_t xwlk_mtx_unlock(struct xwlk_mtx * mtx);

__xwos_api
xwer_t xwlk_mtx_trylock(struct xwlk_mtx * mtx);

__xwos_api
xwer_t xwlk_mtx_timedlock(struct xwlk_mtx * mtx, xwtm_t * xwtm);

__xwos_api
xwer_t xwlk_mtx_lock_unintr(struct xwlk_mtx * mtx);

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ********      inline API implementations     ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
/**
 * @brief XWOS API：等待并上锁互斥锁
 * @param mtx: (I) 互斥锁对象指针
 * @return 错误码
 * @retval OK: OK
 * @retval -EINVAL: 无效参数
 * @retval -EDEADLOCK: 死锁
 * @retval -ENOTINTHRD: 不在线程上下文中
 * @note
 * - 同步/异步：同步
 * - 上下文：线程
 * - 重入性：不可重入
 */
static __xwos_inline_api
xwer_t xwlk_mtx_lock(struct xwlk_mtx * mtx)
{
        xwtm_t expected;

        expected = XWTM_MAX;
        return xwlk_mtx_timedlock(mtx, &expected);
}

#endif /* xwos/smp/lock/mutex.h */
