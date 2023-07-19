/**
 * @file
 * @brief XWOS MP内核：等待队列节点
 * @author
 * + 隐星魂 (Roy Sun) <xwos@xwos.tech>
 * @copyright
 * + Copyright © 2015 xwos.tech, All Rights Reserved.
 * > This Source Code Form is subject to the terms of the Mozilla Public
 * > License, v. 2.0. If a copy of the MPL was not distributed with this
 * > file, You can obtain one at <http://mozilla.org/MPL/2.0/>.
 * @note
 * - 锁的顺序：
 *   + ① xwmp_rtwq.lock
 *     + ② xwmp_wqn.lock
 */

#ifndef __xwos_mp_wqn_h__
#define __xwos_mp_wqn_h__

#include <xwos/standard.h>
#include <xwos/lib/rbtree.h>
#include <xwos/lib/bclst.h>
#include <xwos/mp/skd.h>
#include <xwos/mp/lock/spinlock.h>

/**
 * @brief 等待队列类型枚举
 */
enum xwmp_wqtype_em {
        XWMP_WQTYPE_UNKNOWN = 0U,
        XWMP_WQTYPE_NULL = 1U,
        XWMP_WQTYPE_PLSEM = 2U,
        XWMP_WQTYPE_RTSEM = 3U,
        XWMP_WQTYPE_COND = 4U,
        XWMP_WQTYPE_EVENT = 5U,
        XWMP_WQTYPE_MTX = 6U,
};

/**
 * @brief 等待队列节点唤醒原因枚举
 */
enum xwmp_wqn_reason_em {
        XWMP_WQN_REASON_UNKNOWN = 0U,
        XWMP_WQN_REASON_UP = 1U, /**< 资源可用 */
        XWMP_WQN_REASON_INTR = 2U, /**< 等待被中断 */
};

struct xwmp_wqn;

/**
 * @brief 等待队列节点回调函数指针类型
 */
typedef void (* xwmp_wqn_f)(struct xwmp_wqn *);

/**
 * @brief 等待队列节点
 */
struct xwmp_wqn {
        xwsq_t type; /**< 等待队列类型（信号量、互斥锁） */
        atomic_xwsq_t reason; /**< 唤醒原因 */
        void * wq; /**< 指向所属的等待队列的指针 */
        xwmp_wqn_f cb; /**< 被唤醒时的回调函数 */
        struct xwmp_splk lock; /**< 保护此结构体的锁 */
        xwpr_t prio; /**< 优先级 */
        struct xwlib_rbtree_node rbn; /**> 红黑树节点 */
        union {
                struct xwlib_bclst_node rbb; /**< 当等待队列为 `rtwq` 时的链表节点 */
                struct xwlib_bclst_node pl; /**< 当等待队列为 `plwq` 时的链表节点 */
        } cln; /**< 双循环链表节点 */
};

void xwmp_wqn_init(struct xwmp_wqn * wqn);

#endif /* xwmp/mp/wqn.h */
