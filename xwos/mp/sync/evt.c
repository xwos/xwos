/**
 * @file
 * @brief 玄武OS MP内核同步机制：事件
 * @author
 * + 隐星魂 (Roy.Sun) <https://xwos.tech>
 * @copyright
 * + (c) 2015 隐星魂 (Roy.Sun) <https://xwos.tech>
 * > This Source Code Form is subject to the terms of the Mozilla Public
 * > License, v. 2.0. If a copy of the MPL was not distributed with this
 * > file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * @note
 * - 锁的顺序：
 *   + ① plwq.lock
 *   + ① rtwq.lock
 *     + ② evt.lock
 */

#include <xwos/standard.h>
#include <string.h>
#include <xwos/lib/xwbop.h>
#include <xwos/mm/common.h>
#include <xwos/mm/kma.h>
#if defined(XWMPCFG_SYNC_EVT_STDC_MM) && (1 == XWMPCFG_SYNC_EVT_STDC_MM)
  #include <stdlib.h>
#endif /* XWMPCFG_SYNC_EVT_STDC_MM */
#include <xwos/ospl/irq.h>
#include <xwos/mp/thd.h>
#include <xwos/mp/lock/spinlock.h>
#include <xwos/mp/sync/obj.h>
#include <xwos/mp/sync/cond.h>
#include <xwos/mp/sync/evt.h>

static __xwmp_code
struct xwmp_evt * xwmp_evt_alloc(xwsz_t num);

static __xwmp_code
void xwmp_evt_free(struct xwmp_evt * evt);

static __xwmp_code
void xwmp_evt_construct(struct xwmp_evt * evt, xwsz_t num,
                        xwbmp_t * bmp, xwbmp_t * msk);

static __xwmp_code
void xwmp_evt_destruct(struct xwmp_evt * evt);

static __xwmp_code
xwer_t xwmp_evt_gc(void * evt);

static __xwmp_code
xwer_t xwmp_evt_activate(struct xwmp_evt * evt, xwsq_t type, xwobj_gc_f gcfunc);

static __xwmp_code
xwer_t xwmp_flg_trywait_level(struct xwmp_evt * evt,
                              xwsq_t trigger, xwsq_t action,
                              xwbmp_t origin[], xwbmp_t msk[]);

static __xwmp_code
xwer_t xwmp_flg_trywait_edge(struct xwmp_evt * evt, xwsq_t trigger,
                             xwbmp_t origin[], xwbmp_t msk[]);

static __xwmp_code
xwer_t xwmp_flg_timedwait_level(struct xwmp_evt * evt,
                                xwsq_t trigger, xwsq_t action,
                                xwbmp_t origin[], xwbmp_t msk[],
                                xwtm_t * xwtm);

static __xwmp_code
xwer_t xwmp_flg_timedwait_edge(struct xwmp_evt * evt, xwsq_t trigger,
                               xwbmp_t origin[], xwbmp_t msk[],
                               xwtm_t * xwtm);

/**
 * @brief 动态创建一个对象
 * @param num: (I) 事件位图中位的个数
 * @return 事件对象的指针
 */
static __xwmp_code
struct xwmp_evt * xwmp_evt_alloc(xwsz_t num)
{
#if defined(XWMPCFG_SYNC_EVT_STDC_MM) && (1 == XWMPCFG_SYNC_EVT_STDC_MM)
        struct xwmp_evt * evt;
        xwbmp_t * bmp, * msk;
        xwsz_t bmpnum, bmpsize;

        bmpnum = BITS_TO_XWBMP_T(num);
        bmpsize = bmpnum * sizeof(xwbmp_t);
        evt = malloc(sizeof(struct xwmp_evt) + bmpsize + bmpsize);
        if (NULL == evt) {
                evt = err_ptr(-ENOMEM);
        } else {
                bmp = (void *)&evt[1];
                msk = &bmp[bmpnum];
                xwmp_evt_construct(evt, num, bmp, msk);
        }
        return evt;
#else /* XWMPCFG_SYNC_EVT_STDC_MM */
        union {
                struct xwmp_evt * evt;
                void * anon;
        } mem;
        xwbmp_t * bmp, * msk;
        xwsz_t bmpnum, bmpsize;
        xwer_t rc;

        bmpnum = BITS_TO_XWBMP_T(num);
        bmpsize = bmpnum * sizeof(xwbmp_t);
        rc = xwmm_kma_alloc(sizeof(struct xwmp_evt) + bmpsize + bmpsize,
                            XWMM_ALIGNMENT, &mem.anon);
        if (XWOK == rc) {
                bmp = (void *)&mem.evt[1];
                msk = &bmp[bmpnum];
                xwmp_evt_construct(mem.evt, num, bmp, msk);
        } else {
                mem.evt = err_ptr(rc);
        }
        return mem.evt;
#endif /* !XWMPCFG_SYNC_EVT_STDC_MM */
}

/**
 * @brief 释放事件对象
 * @param evt: (I) 事件对象的指针
 */
static __xwmp_code
void xwmp_evt_free(struct xwmp_evt * evt)
{
#if defined(XWMPCFG_SYNC_EVT_STDC_MM) && (1 == XWMPCFG_SYNC_EVT_STDC_MM)
        xwmp_evt_destruct(evt);
        free(evt);
#else /* XWMPCFG_SYNC_EVT_STDC_MM */
        xwmp_evt_destruct(evt);
        xwmm_kma_free(evt);
#endif /* !XWMPCFG_SYNC_EVT_STDC_MM */
}

/**
 * @brief 事件对象的构造函数
 * @param evt: (I) 事件对象的指针
 * @param num: (I) 事件的数量
 * @param bmp: (I) 事件对象记录事件状态的位图数组缓冲区
 * @param msk: (I) 事件对象记录掩码状态的位图数组缓冲区
 */
static __xwmp_code
void xwmp_evt_construct(struct xwmp_evt * evt, xwsz_t num,
                        xwbmp_t * bmp, xwbmp_t * msk)
{
        xwmp_synobj_construct(&evt->cond.synobj);
        evt->num = num;
        evt->bmp = bmp;
        evt->msk = msk;
}

/**
 * @brief 事件对象的析构函数
 * @param evt: (I) 事件对象的指针
 */
static __xwmp_code
void xwmp_evt_destruct(struct xwmp_evt * evt)
{
        xwmp_cond_destruct(&evt->cond);
}

/**
 * @brief 事件对象的垃圾回收函数
 * @param evt: (I) 事件对象的指针
 * @return 错误码
 */
static __xwmp_code
xwer_t xwmp_evt_gc(void * evt)
{
        xwmp_evt_free((struct xwmp_evt *)evt);
        return XWOK;
}

/**
 * @brief XWMP API：检查事件对象的标签并增加引用计数
 * @param evt: (I) 事件对象指针
 * @param tik: (I) 标签
 * @return 错误码
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_evt_acquire(struct xwmp_evt * evt, xwsq_t tik)
{
        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        return xwmp_synobj_acquire(&evt->cond.synobj, tik);
}

/**
 * @brief XWMP API：检查事件对象的标签并增加引用计数
 * @param evt: (I) 事件对象指针
 * @param tik: (I) 标签
 * @return 错误码
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_evt_release(struct xwmp_evt * evt, xwsq_t tik)
{
        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        return xwmp_synobj_release(&evt->cond.synobj, tik);
}

/**
 * @brief XWMP API：增加事件对象的引用计数
 * @param evt: (I) 事件对象指针
 * @return 错误码
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_evt_grab(struct xwmp_evt * evt)
{
        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        return xwmp_synobj_grab(&evt->cond.synobj);
}

/**
 * @brief XWMP API：减少事件对象的引用计数
 * @param evt: (I) 事件对象指针
 * @return 错误码
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_evt_put(struct xwmp_evt * evt)
{
        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        return xwmp_cond_put(&evt->cond);
}

/**
 * @brief XWMP API：激活并初始化事件对象
 * @param evt: (I) 事件对象的指针
 * @param type: (I) 事件的类型，取值 @ref xwmp_evt_type_em
 * @param gcfunc: (I) 垃圾回收函数的指针
 * @return 错误码
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：对于同一个事件对象，不可重入
 * @note
 * - 由于静态初始化的对象所有资源都是由用户自己提供的，
 *   因此当信号量不使用时，回收资源的函数也需要用户自己提供。
 */
static __xwmp_code
xwer_t xwmp_evt_activate(struct xwmp_evt * evt, xwsq_t type, xwobj_gc_f gcfunc)
{
        xwer_t rc;
        xwsz_t size;

        size = BITS_TO_XWBMP_T(evt->num);
        rc = xwmp_cond_activate(&evt->cond, gcfunc);
        if (__xwcc_likely(XWOK == rc)) {
                evt->type = type;
                switch (type & XWMP_EVT_TYPE_MASK) {
                case XWMP_EVT_TYPE_FLG:
                        memset(evt->msk, 0xFF, size);
                        break;
                case XWMP_EVT_TYPE_BR:
                        memset(evt->msk, 0, size);
                        break;
                case XWMP_EVT_TYPE_SEL:
                        memset(evt->msk, 0, size);
                        break;
                default:
                        memset(evt->msk, 0xFF, size);
                        break;
                }
                memset(evt->bmp, 0, size);
                xwmp_splk_init(&evt->lock);
        }
        return rc;
}

/**
 * @brief XWMP API：动态创建事件对象
 * @param ptrbuf: (O) 指向缓冲区的指针，通过此缓冲区返回对象的指针
 * @param type: (I) 事件的类型，取值范围 @ref xwmp_evt_type_em
 * @param num: (I) 事件的数量
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -EINVAL: 无效参数
 * @retval -ENOMEM: 内存不足
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_evt_create(struct xwmp_evt ** ptrbuf, xwsq_t type, xwsz_t num)
{
        struct xwmp_evt * evt;
        xwer_t rc;

        XWOS_VALIDATE((ptrbuf), "nullptr", -EFAULT);
        XWOS_VALIDATE(((type & XWMP_EVT_TYPE_MASK) < XWMP_EVT_TYPE_MAX),
                      "type-error", -EINVAL);

        *ptrbuf = NULL;
        evt = xwmp_evt_alloc(num);
        if (__xwcc_unlikely(is_err(evt))) {
                rc = ptr_err(evt);
        } else {
                rc = xwmp_evt_activate(evt, type, xwmp_evt_gc);
                if (__xwcc_unlikely(rc < 0)) {
                        xwmp_evt_free(evt);
                } else {
                        *ptrbuf = evt;
                }
        }
        return rc;
}

/**
 * @brief XWMP API：删除动态创建的事件对象
 * @param evt: (I) 事件对象的指针
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：对于同一个事件对象，不可重入
 */
__xwmp_api
xwer_t xwmp_evt_delete(struct xwmp_evt * evt)
{
        XWOS_VALIDATE((evt), "nullptr", -EFAULT);

        return xwmp_evt_put(evt);
}

/**
 * @brief XWMP API：静态初始化事件对象
 * @param evt: (I) 事件对象的指针
 * @param type: (I) 事件的类型，取值范围 @ref xwmp_evt_type_em
 * @param num: (I) 事件的数量
 * @param bmp: (I) 事件对象用来记录事件状态的位图缓冲区
 * @param msk: (I) 事件对象用来记录掩码状态的位图缓冲区
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -EINVAL: 无效参数
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：对于同一个事件对象，不可重入
 */
__xwmp_api
xwer_t xwmp_evt_init(struct xwmp_evt * evt, xwsq_t type, xwsz_t num,
                     xwbmp_t * bmp, xwbmp_t * msk)
{
        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE((bmp), "nullptr", -EFAULT);
        XWOS_VALIDATE((msk), "nullptr", -EFAULT);
        XWOS_VALIDATE(((type & XWMP_EVT_TYPE_MASK) < XWMP_EVT_TYPE_MAX),
                      "type-error", -EINVAL);

        xwmp_evt_construct(evt, num, bmp, msk);
        return xwmp_evt_activate(evt, type, NULL);
}

/**
 * @brief XWMP API：销毁静态方式初始化的事件对象
 * @param evt: (I) 事件对象的指针
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @note
 * - 同步/异步：异步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：对于同一个事件对象，不可重入
 */
__xwmp_api
xwer_t xwmp_evt_destroy(struct xwmp_evt * evt)
{
        XWOS_VALIDATE((evt), "nullptr", -EFAULT);

        return xwmp_evt_put(evt);
}

/**
 * @brief XWMP API：绑定事件对象(evt)到另一个事件对象(sel)，
 *                  另一个事件对象类型为XWMP_EVT_TYPE_SEL
 * @param evt: (I) 事件对象的指针
 * @param sel: (I) 类型为XWMP_EVT_TYPE_SEL的事件对象的指针
 * @param pos: (I) 事件对象对象映射到位图中的位置
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 事件对象类型错误
 * @retval -ECHRNG: 位置超出范围
 * @retval -EALREADY: 同步对象已经绑定到事件对象
 * @retval -EBUSY: 通道已经被其他同步对象独占
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：对于同一个事件对象，不可重入
 */
__xwmp_api
xwer_t xwmp_evt_bind(struct xwmp_evt * evt, struct xwmp_evt * sel, xwsq_t pos)
{
        return xwmp_cond_bind(&evt->cond, sel, pos);
}

/**
 * @brief XWMP API：从另一个事件对象(sel)上解绑事件对象(evt)，
 *                  另一个事件对象类型为XWMP_EVT_TYPE_SEL
 * @param evt: (I) 事件对象的指针
 * @param sel: (I) 类型为XWMP_EVT_TYPE_SEL的事件对象的指针
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -ETYPE: 事件对象类型错误
 * @retval -EFAULT: 空指针
 * @retval -ENOTCONN: 同步对象没有绑定到事件对象上
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：对于同一个事件对象，不可重入
 */
__xwmp_api
xwer_t xwmp_evt_unbind(struct xwmp_evt * evt, struct xwmp_evt * sel)
{
        return xwmp_cond_unbind(&evt->cond, sel);
}

/**
 * @brief XWMP API：中断事件对象等待队列中的所有节点
 * @param evt: (I) 事件对象的指针
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 类型不匹配
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_evt_intr_all(struct xwmp_evt * evt)
{
        XWOS_VALIDATE((evt), "nullptr", -EFAULT);

        return xwmp_cond_intr_all(&evt->cond);
}

/**
 * @brief XWMP API：获取事件对象中事件的数量
 * @param evt: (I) 事件对象的指针
 * @param numbuf: (O) 指向缓冲区的指针，通过此缓冲区返回事件的数量
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_evt_get_num(struct xwmp_evt * evt, xwsz_t * numbuf)
{
        xwer_t rc;
        XWOS_VALIDATE((evt), "nullptr", -EFAULT);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        *numbuf = evt->num;
        xwmp_evt_put(evt);
        return XWOK;

err_evt_grab:
        return rc;
}

/******** type:XWMP_EVT_TYPE_FLG ********/
/**
 * @brief XWMP API：在事件对象中同时设置多个位图的位，
 *                  事件对象类型为XWMP_EVT_TYPE_FLG
 * @param evt: (I) 事件对象的指针
 * @param msk: (I) 事件对象的位图掩码
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 事件对象类型错误
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_flg_s1m(struct xwmp_evt * evt, xwbmp_t msk[])
{
        xwer_t rc;
        xwreg_t cpuirq;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE((msk), "nullptr", -EFAULT);
        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_FLG),
                      "type-error", -ETYPE);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        xwbmpop_s1m(evt->bmp, msk, evt->num);
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_cond_broadcast(&evt->cond);
        xwmp_evt_put(evt);
        return XWOK;

err_evt_grab:
        return rc;
}

/**
 * @brief XWMP API：在事件对象中设置单个位图的位，
 *                  事件对象类型为XWMP_EVT_TYPE_FLG
 * @param evt: (I) 事件对象的指针
 * @param pos: (I) 位的序号
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ECHRNG: 位置超出范围
 * @retval -ETYPE: 事件对象类型错误
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_flg_s1i(struct xwmp_evt * evt, xwsq_t pos)
{
        xwer_t rc;
        xwreg_t cpuirq;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_FLG),
                      "type-error", -ETYPE);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        if (pos >= evt->num) {
                rc = -ECHRNG;
                goto err_pos_range;
        }
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        xwbmpop_s1i(evt->bmp, pos);
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_cond_broadcast(&evt->cond);
        xwmp_evt_put(evt);
        return XWOK;

err_pos_range:
        xwmp_evt_put(evt);
err_evt_grab:
        return rc;
}

/**
 * @brief XWMP API：在事件对象中同时清除多个位图的位，
 *                  事件对象类型为XWMP_EVT_TYPE_FLG
 * @param evt: (I) 事件对象的指针
 * @param msk: (I) 事件对象的位图掩码
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 事件对象类型错误
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_flg_c0m(struct xwmp_evt * evt, xwbmp_t msk[])
{
        xwer_t rc;
        xwreg_t cpuirq;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE((msk), "nullptr", -EFAULT);
        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_FLG),
                      "type-error", -ETYPE);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        xwbmpop_c0m(evt->bmp, msk, evt->num);
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_cond_broadcast(&evt->cond);
        xwmp_evt_put(evt);
        return XWOK;

err_evt_grab:
        return rc;
}

/**
 * @brief XWMP API：在事件对象中清除单个位图的位，
 *                  事件对象类型为XWMP_EVT_TYPE_FLG
 * @param evt: (I) 事件对象的指针
 * @param pos: (I) 位的序号
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ECHRNG: 位置超出范围
 * @retval -ETYPE: 事件对象类型错误
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_flg_c0i(struct xwmp_evt * evt, xwsq_t pos)
{
        xwer_t rc;
        xwreg_t cpuirq;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_FLG),
                      "type-error", -ETYPE);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        if (pos >= evt->num) {
                rc = -ECHRNG;
                goto err_pos_range;
        }
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        xwbmpop_c0i(evt->bmp, pos);
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_cond_broadcast(&evt->cond);
        xwmp_evt_put(evt);
        return XWOK;

err_pos_range:
        xwmp_evt_put(evt);
err_evt_grab:
        return rc;
}

/**
 * @brief XWMP API：在事件对象中同时翻转多个位图的位，
 *                  事件对象类型为XWMP_EVT_TYPE_FLG
 * @param evt: (I) 事件对象的指针
 * @param msk: (I) 事件对象的位图掩码
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 事件对象类型错误
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_flg_x1m(struct xwmp_evt * evt, xwbmp_t msk[])
{
        xwer_t rc;
        xwreg_t cpuirq;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE((msk), "nullptr", -EFAULT);
        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_FLG),
                      "type-error", -ETYPE);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        xwbmpop_x1m(evt->bmp, msk, evt->num);
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_cond_broadcast(&evt->cond);
        xwmp_evt_put(evt);
        return XWOK;

err_evt_grab:
        return rc;
}

/**
 * @brief XWMP API：在事件对象中翻转单个位图的位，
 *                  事件对象类型为XWMP_EVT_TYPE_FLG
 * @param evt: (I) 事件对象的指针
 * @param pos: (I) 位的序号
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ECHRNG: 位置超出范围
 * @retval -ETYPE: 事件对象类型错误
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_flg_x1i(struct xwmp_evt * evt, xwsq_t pos)
{
        xwer_t rc;
        xwreg_t cpuirq;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_FLG),
                      "type-error", -ETYPE);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        if (pos >= evt->num) {
                rc = -ECHRNG;
                goto err_pos_range;
        }
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        xwbmpop_x1i(evt->bmp, pos);
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_cond_broadcast(&evt->cond);
        xwmp_evt_put(evt);
        return XWOK;

err_pos_range:
        xwmp_evt_put(evt);
err_evt_grab:
        return rc;
}

/**
 * @brief XWMP API：读取事件对象位图的值
 * @param evt: (I) 事件对象的指针
 * @param out: (O) 指向缓冲区的指针，通过此缓冲区返回事件对象位图的值
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_flg_read(struct xwmp_evt * evt, xwbmp_t out[])
{
        xwer_t rc;
        xwreg_t cpuirq;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE((out), "nullptr", -EFAULT);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        xwbmpop_assign(out, evt->bmp, evt->num);
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_evt_put(evt);
        return XWOK;

err_evt_grab:
        return rc;
}

static __xwmp_code
xwer_t xwmp_flg_trywait_level(struct xwmp_evt * evt,
                              xwsq_t trigger, xwsq_t action,
                              xwbmp_t origin[], xwbmp_t msk[])
{
        xwreg_t cpuirq;
        bool triggered;
        xwer_t rc;

        XWOS_VALIDATE((trigger <= XWMP_FLG_TRIGGER_CLR_ANY),
                      "illegal-trigger", -EINVAL);
        XWOS_VALIDATE((action < XWMP_FLG_ACTION_NUM),
                      "illegal-action", -EINVAL);

        rc = XWOK;
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        if (origin) {
                xwbmpop_assign(origin, evt->bmp, evt->num);
        }
        if (XWMP_FLG_ACTION_CONSUMPTION == action) {
                switch (trigger) {
                case XWMP_FLG_TRIGGER_SET_ALL:
                        triggered = xwbmpop_t1ma_then_c0m(evt->bmp, msk, evt->num);
                        break;
                case XWMP_FLG_TRIGGER_SET_ANY:
                        triggered = xwbmpop_t1mo_then_c0m(evt->bmp, msk, evt->num);
                        break;
                case XWMP_FLG_TRIGGER_CLR_ALL:
                        triggered = xwbmpop_t0ma_then_s1m(evt->bmp, msk, evt->num);
                        break;
                case XWMP_FLG_TRIGGER_CLR_ANY:
                        triggered = xwbmpop_t0mo_then_s1m(evt->bmp, msk, evt->num);
                        break;
                default:
                        triggered = true;
                        rc = -EINVAL;
                        break;
                }
        } else {
                switch (trigger) {
                case XWMP_FLG_TRIGGER_SET_ALL:
                        triggered = xwbmpop_t1ma(evt->bmp, msk, evt->num);
                        break;
                case XWMP_FLG_TRIGGER_SET_ANY:
                        triggered = xwbmpop_t1mo(evt->bmp, msk, evt->num);
                        break;
                case XWMP_FLG_TRIGGER_CLR_ALL:
                        triggered = xwbmpop_t0ma(evt->bmp, msk, evt->num);
                        break;
                case XWMP_FLG_TRIGGER_CLR_ANY:
                        triggered = xwbmpop_t0mo(evt->bmp, msk, evt->num);
                        break;
                default:
                        triggered = true;
                        rc = -EINVAL;
                        break;
                }
        }
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        if (!triggered) {
                rc = -ENODATA;
        }
        return rc;
}

static __xwmp_code
xwer_t xwmp_flg_trywait_edge(struct xwmp_evt * evt, xwsq_t trigger,
                             xwbmp_t origin[], xwbmp_t msk[])
{
        xwreg_t cpuirq;
        xwssq_t cmprc;
        bool triggered;
        xwer_t rc;
        xwbmpop_declare(cur, evt->num);
        xwbmpop_declare(tmp, evt->num);

        XWOS_VALIDATE((origin), "nullptr", -EFAULT);

        xwbmpop_and(origin, msk, evt->num);
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        xwbmpop_assign(cur, evt->bmp, evt->num);
        xwbmpop_and(cur, msk, evt->num);
        if (XWMP_FLG_TRIGGER_TGL_ALL == trigger) {
                xwbmpop_assign(tmp, cur, evt->num);
                xwbmpop_xor(tmp, origin, evt->num);
                cmprc = xwbmpop_cmp(tmp, msk, evt->num);
                if (0 == cmprc) {
                        triggered = true;
                        rc = XWOK;
                } else {
                        triggered = false;
                        rc = -ENODATA;
                }
        } else if (XWMP_FLG_TRIGGER_TGL_ANY == trigger) {
                cmprc = xwbmpop_cmp(origin, cur, evt->num);
                if (0 == cmprc) {
                        triggered = false;
                        rc = -ENODATA;
                } else {
                        triggered = true;
                        rc = XWOK;
                }
        } else {
                triggered = true;
                rc = -EINVAL;
        }
        if (triggered) {
                xwbmpop_assign(origin, cur, evt->num);
                xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        } else {
                xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        }
        return rc;
}

/**
 * @brief XWMP API：等待事件对象中的触发信号，事件对象类型为XWMP_EVT_TYPE_FLG
 * @param evt: (I) 事件对象的指针
 * @param trigger: (I) 事件触发条件，取值 @ref xwmp_flg_trigger_em
 * @param action: (I) 事件触发后的动作，取值 @ref xwmp_flg_action_em，
 *                    仅当trigger取值
 *                    @ref XWMP_FLG_TRIGGER_SET_ALL
 *                    @ref XWMP_FLG_TRIGGER_SET_ANY
 *                    @ref XWMP_FLG_TRIGGER_CLR_ALL
 *                    @ref XWMP_FLG_TRIGGER_CLR_ALL
 *                    时有效，其他情况不使用此参数，可填 @ref XWMP_UNUSED_ARGUMENT
 * @param origin: 指向缓冲区的指针：
 *                - 当trigger取值
 *                  @ref XWMP_FLG_TRIGGER_SET_ALL
 *                  @ref XWMP_FLG_TRIGGER_SET_ANY
 *                  @ref XWMP_FLG_TRIGGER_CLR_ALL
 *                  @ref XWMP_FLG_TRIGGER_CLR_ANY
 *                  (O) 返回触发时事件对象中的位图状态（action之前）
 *                - 当trigger取值
 *                  @ref XWMP_FLG_TRIGGER_TGL_ALL
 *                  @ref XWMP_FLG_TRIGGER_TGL_ANY
 *                  (I) 作为输入时，作为用于比较的初始值
 *                  (O) 作为输出时，返回事件对象中位图状态
 *                      （可作为下一次调用的初始值）
 * @param msk: (I) 事件对象的位图掩码，表示只关注掩码部分的事件
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 事件对象类型错误
 * @retval -EINVAL: 参数无效
 * @retval -EINTR: 等待被中断
 * @retval -ENOTINTHD: 不在线程上下文中
 * @note
 * - 同步/异步：同步
 * - 上下文：线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_flg_wait(struct xwmp_evt * evt,
                     xwsq_t trigger, xwsq_t action,
                     xwbmp_t origin[], xwbmp_t msk[])
{
        xwtm_t expected = XWTM_MAX;
        return xwmp_flg_timedwait(evt, trigger, action, origin, msk, &expected);
}

/**
 * @brief XWMP API：检测一下事件对象中的触发信号，不会阻塞调用者，
 *                  事件对象类型为XWMP_EVT_TYPE_FLG
 * @param evt: (I) 事件对象的指针
 * @param trigger: (I) 事件触发条件，取值 @ref xwmp_flg_trigger_em
 * @param action: (I) 事件触发后的动作，取值 @ref xwmp_flg_action_em
 *                    仅当trigger取值
 *                    @ref XWMP_FLG_TRIGGER_SET_ALL
 *                    @ref XWMP_FLG_TRIGGER_SET_ANY
 *                    @ref XWMP_FLG_TRIGGER_CLR_ALL
 *                    @ref XWMP_FLG_TRIGGER_CLR_ALL
 *                    时有效，其他情况不使用此参数，可填 @ref XWMP_UNUSED_ARGUMENT
 * @param origin: 指向缓冲区的指针：
 *                - 当trigger取值
 *                  @ref XWMP_FLG_TRIGGER_SET_ALL
 *                  @ref XWMP_FLG_TRIGGER_SET_ANY
 *                  @ref XWMP_FLG_TRIGGER_CLR_ALL
 *                  @ref XWMP_FLG_TRIGGER_CLR_ANY
 *                  (O) 返回触发时返回事件对象中的位图状态（action之前）
 *                - 当trigger取值
 *                  @ref XWMP_FLG_TRIGGER_TGL_ALL
 *                  @ref XWMP_FLG_TRIGGER_TGL_ANY
 *                  (I) 作为输入时，作为用于比较的初始值
 *                  (O) 作为输出时，返回事件对象中位图状态
 *                      （可作为下一次调用的初始值）
 * @param msk: (I) 事件对象的位图掩码，表示只关注掩码部分的事件
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 事件对象类型错误
 * @retval -EINVAL: 参数无效
 * @retval -ENODATA: 没有任何事件触发
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_flg_trywait(struct xwmp_evt * evt,
                        xwsq_t trigger, xwsq_t action,
                        xwbmp_t origin[], xwbmp_t msk[])
{
        xwer_t rc;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE((msk), "nullptr", -EFAULT);
        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_FLG),
                      "type-error", -ETYPE);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        if (trigger <= XWMP_FLG_TRIGGER_CLR_ANY) {
                rc = xwmp_flg_trywait_level(evt, trigger, action,
                                            origin, msk);
        } else {
                rc = xwmp_flg_trywait_edge(evt, trigger, origin, msk);
        }
        xwmp_evt_put(evt);

err_evt_grab:
        return rc;
}

static __xwmp_code
xwer_t xwmp_flg_timedwait_level(struct xwmp_evt * evt,
                                xwsq_t trigger, xwsq_t action,
                                xwbmp_t origin[], xwbmp_t msk[],
                                xwtm_t * xwtm)
{
        xwreg_t cpuirq;
        bool triggered;
        xwsq_t lkst;
        xwer_t rc;

        XWOS_VALIDATE((trigger <= XWMP_FLG_TRIGGER_CLR_ANY),
                      "illegal-trigger", -EINVAL);
        XWOS_VALIDATE((action < XWMP_FLG_ACTION_NUM),
                      "illegal-action", -EINVAL);

        rc = XWOK;
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        while (true) {
                if (origin) {
                        xwbmpop_assign(origin, evt->bmp, evt->num);
                }
                if (XWMP_FLG_ACTION_CONSUMPTION == action) {
                        switch (trigger) {
                        case XWMP_FLG_TRIGGER_SET_ALL:
                                triggered = xwbmpop_t1ma_then_c0m(evt->bmp, msk,
                                                                  evt->num);
                                break;
                        case XWMP_FLG_TRIGGER_SET_ANY:
                                triggered = xwbmpop_t1mo_then_c0m(evt->bmp, msk,
                                                                  evt->num);
                                break;
                        case XWMP_FLG_TRIGGER_CLR_ALL:
                                triggered = xwbmpop_t0ma_then_s1m(evt->bmp, msk,
                                                                  evt->num);
                                break;
                        case XWMP_FLG_TRIGGER_CLR_ANY:
                                triggered = xwbmpop_t0mo_then_s1m(evt->bmp, msk,
                                                                  evt->num);
                                break;
                        default:
                                triggered = true;
                                rc = -EINVAL;
                                break;
                        }
                } else {
                        switch (trigger) {
                        case XWMP_FLG_TRIGGER_SET_ALL:
                                triggered = xwbmpop_t1ma(evt->bmp, msk, evt->num);
                                break;
                        case XWMP_FLG_TRIGGER_SET_ANY:
                                triggered = xwbmpop_t1mo(evt->bmp, msk, evt->num);
                                break;
                        case XWMP_FLG_TRIGGER_CLR_ALL:
                                triggered = xwbmpop_t0ma(evt->bmp, msk, evt->num);
                                break;
                        case XWMP_FLG_TRIGGER_CLR_ANY:
                                triggered = xwbmpop_t0mo(evt->bmp, msk, evt->num);
                                break;
                        default:
                                triggered = true;
                                rc = -EINVAL;
                                break;
                        }
                }
                if (triggered) {
                        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
                        break;
                } else {
                        rc = xwmp_cond_timedwait(&evt->cond,
                                                 &evt->lock, XWOS_LK_SPLK, NULL,
                                                 xwtm, &lkst);
                        if (XWOK == rc) {
                                if (XWOS_LKST_UNLOCKED == lkst) {
                                        xwmp_splk_lock(&evt->lock);
                                }
                        } else {
                                if (XWOS_LKST_LOCKED == lkst) {
                                        xwmp_splk_unlock(&evt->lock);
                                }
                                xwospl_cpuirq_restore_lc(cpuirq);
                                break;
                        }
                }
        }
        return rc;
}

static __xwmp_code
xwer_t xwmp_flg_timedwait_edge(struct xwmp_evt * evt, xwsq_t trigger,
                               xwbmp_t origin[], xwbmp_t msk[],
                               xwtm_t * xwtm)
{
        xwreg_t cpuirq;
        xwsq_t lkst;
        xwssq_t cmprc;
        bool triggered;
        xwer_t rc;
        xwbmpop_declare(cur, evt->num);
        xwbmpop_declare(tmp, evt->num);

        XWOS_VALIDATE((origin), "nullptr", -EFAULT);

        xwbmpop_and(origin, msk, evt->num);
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        while (true) {
                xwbmpop_assign(cur, evt->bmp, evt->num);
                xwbmpop_and(cur, msk, evt->num);
                if (XWMP_FLG_TRIGGER_TGL_ALL == trigger) {
                        xwbmpop_assign(tmp, cur, evt->num);
                        xwbmpop_xor(tmp, origin, evt->num);
                        cmprc = xwbmpop_cmp(tmp, msk, evt->num);
                        if (0 == cmprc) {
                                triggered = true;
                                rc = XWOK;
                        } else {
                                triggered = false;
                        }
                } else if (XWMP_FLG_TRIGGER_TGL_ANY == trigger) {
                        cmprc = xwbmpop_cmp(origin, cur, evt->num);
                        if (0 == cmprc) {
                                triggered = false;
                        } else {
                                triggered = true;
                                rc = XWOK;
                        }
                } else {
                        triggered = true;
                        rc = -EINVAL;
                }
                if (triggered) {
                        xwbmpop_assign(origin, cur, evt->num);
                        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
                        break;
                } else {
                        rc = xwmp_cond_timedwait(&evt->cond,
                                                 &evt->lock, XWOS_LK_SPLK, NULL,
                                                 xwtm, &lkst);
                        if (XWOK == rc) {
                                if (XWOS_LKST_UNLOCKED == lkst) {
                                        xwmp_splk_lock(&evt->lock);
                                }
                        } else {
                                if (XWOS_LKST_LOCKED == lkst) {
                                        xwmp_splk_unlock(&evt->lock);
                                }
                                xwospl_cpuirq_restore_lc(cpuirq);
                                break;
                        }
                }
        }
        return rc;
}

/**
 * @brief XWMP API：限时等待事件对象中的触发信号，事件对象类型为XWMP_EVT_TYPE_FLG
 * @param evt: (I) 事件对象的指针
 * @param trigger: (I) 事件触发条件，取值 @ref xwmp_flg_trigger_em
 * @param action: (I) 事件触发后的动作，取值 @ref xwmp_flg_action_em
 *                    仅当trigger取值
 *                    @ref XWMP_FLG_TRIGGER_SET_ALL
 *                    @ref XWMP_FLG_TRIGGER_SET_ANY
 *                    @ref XWMP_FLG_TRIGGER_CLR_ALL
 *                    @ref XWMP_FLG_TRIGGER_CLR_ALL
 *                    时有效，其他情况不使用此参数，可填 @ref XWMP_UNUSED_ARGUMENT
 * @param origin: 指向缓冲区的指针：
 *                - 当trigger取值
 *                  @ref XWMP_FLG_TRIGGER_SET_ALL
 *                  @ref XWMP_FLG_TRIGGER_SET_ANY
 *                  @ref XWMP_FLG_TRIGGER_CLR_ALL
 *                  @ref XWMP_FLG_TRIGGER_CLR_ANY
 *                  (O) 返回触发时事件对象中的位图状态（action之前）
 *                - 当trigger取值
 *                  @ref XWMP_FLG_TRIGGER_TGL_ALL
 *                  @ref XWMP_FLG_TRIGGER_TGL_ANY
 *                  (I) 作为输入时，作为用于比较的初始值
 *                  (O) 作为输出时，返回事件对象中位图状态
 *                      （可作为下一次调用的初始值）
 * @param msk: (I) 事件对象的位图掩码，表示只关注掩码部分的事件
 * @param xwtm: 指向缓冲区的指针，此缓冲区：
 *              (I) 作为输入时，表示期望的阻塞等待时间
 *              (O) 作为输出时，返回剩余的期望时间
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 事件对象类型错误
 * @retval -EINVAL: 参数无效
 * @retval -ETIMEDOUT: 超时
 * @retval -EINTR: 等待被中断
 * @retval -ENOTINTHD: 不在线程上下文中
 * @note
 * - 同步/异步：同步
 * - 上下文：线程
 * - 重入性：可重入
 * @note
 * - 函数返回返回*-ETIMEDOUT*时，*xwtm*指向的缓冲区内的期望时间会减为0。
 */
__xwmp_api
xwer_t xwmp_flg_timedwait(struct xwmp_evt * evt,
                          xwsq_t trigger, xwsq_t action,
                          xwbmp_t origin[], xwbmp_t msk[],
                          xwtm_t * xwtm)
{
        xwer_t rc;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE((msk), "nullptr", -EFAULT);
        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_FLG),
                      "type-error", -ETYPE);
        XWOS_VALIDATE((xwtm), "nullptr", -EFAULT);
        XWOS_VALIDATE((-EINTHD == xwmp_irq_get_id(NULL)),
                      "not-in-thd", -ENOTINTHD);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        if (trigger <= XWMP_FLG_TRIGGER_CLR_ANY) {
                rc = xwmp_flg_timedwait_level(evt, trigger, action, origin, msk, xwtm);
        } else {
                rc = xwmp_flg_timedwait_edge(evt, trigger, origin, msk, xwtm);
        }
        xwmp_evt_put(evt);

err_evt_grab:
        return rc;
}

/******** type:XWMP_EVT_TYPE_SEL ********/
/**
 * @brief 绑定同步对象到事件对象，事件对象类型为XWMP_EVT_TYPE_SEL
 * @param evt: (I) 事件对象的指针
 * @param obj: (I) 同步对象的指针
 * @param pos: (I) 同步对象映射到位图中的位置
 * @param exclusive: (I) 是否为独占绑定
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -ETYPE: 事件对象类型错误
 * @retval -ECHRNG: 位置超出范围
 * @retval -EALREADY: 同步对象已经绑定到事件对象
 * @retval -EBUSY: 通道已经被其他同步对象独占
 */
__xwmp_code
xwer_t xwmp_sel_obj_bind(struct xwmp_evt * evt,
                         struct xwmp_synobj * synobj,
                         xwsq_t pos,
                         bool exclusive)
{
        struct xwmp_evt * owner;
        xwreg_t cpuirq;
        bool existed;
        xwer_t rc;

        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_SEL),
                      "type-error", -ETYPE);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        if (pos >= evt->num) {
                rc = -ECHRNG;
                goto err_pos_range;
        }
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        owner = synobj->sel.evt;
        if (NULL != owner) {
                rc = -EALREADY;
                goto err_already;
        }
        existed = xwbmpop_t1i(evt->msk, pos);
        if (existed) {
                rc = -EBUSY;
                goto err_busy;
        }
        if (exclusive) {
                xwbmpop_s1i(evt->msk, pos);
        }
        synobj->sel.evt = evt;
        synobj->sel.pos = pos;
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        return XWOK;

err_busy:
err_already:
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
err_pos_range:
        xwmp_evt_put(evt);
err_evt_grab:
        return rc;
}

/**
 * @brief XWMP API：从事件对象上解绑同步对象，事件对象类型为XWMP_EVT_TYPE_SEL
 * @param evt: (I) 事件对象的指针
 * @param obj: (I) 同步对象的指针
 * @param exclusive: (I) 是否为独占绑定
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -ETYPE: 事件对象类型错误
 * @retval -ENOTCONN: 同步对象没有绑定到事件对象上
 */
__xwmp_code
xwer_t xwmp_sel_obj_unbind(struct xwmp_evt * evt,
                           struct xwmp_synobj * synobj,
                           bool exclusive)
{
        struct xwmp_evt * owner;
        xwreg_t cpuirq;
        xwer_t rc;

        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_SEL),
                      "type-error", -ETYPE);

        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        owner = synobj->sel.evt;
        if (evt != owner) {
                rc = -ENOTCONN;
                goto err_notconn;
        }
        if (exclusive) {
                xwbmpop_c0i(evt->msk, synobj->sel.pos);
        }
        synobj->sel.evt = NULL;
        synobj->sel.pos = 0;
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_evt_put(evt);
        return XWOK;

err_notconn:
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        return rc;
}

/**
 * @brief 在事件对象上设置同步对象的标志位，事件对象类型为XWMP_EVT_TYPE_SEL
 * @param evt: (I) 事件对象的指针
 * @param obj: (I) 同步对象的指针
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -ENOTCONN: 同步对象没有绑定到事件对象上
 */
__xwmp_code
xwer_t xwmp_sel_obj_s1i(struct xwmp_evt * evt, struct xwmp_synobj * synobj)
{
        struct xwmp_evt * owner;
        xwreg_t cpuirq;
        xwer_t rc;

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }

        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        owner = synobj->sel.evt;
        if (evt != owner) {
                rc = -ENOTCONN;
                goto err_notconn;
        }
        xwbmpop_s1i(evt->bmp, synobj->sel.pos);
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_cond_broadcast(&evt->cond);
        xwmp_evt_put(evt);
        return XWOK;

err_notconn:
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_evt_put(evt);
err_evt_grab:
        return rc;
}

/**
 * @brief 在事件对象上清除同步对象的标志位，事件对象类型为XWMP_EVT_TYPE_SEL
 * @param evt: (I) 事件对象的指针
 * @param obj: (I) 同步对象的指针
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -ENOTCONN: 同步对象没有绑定到事件对象上
 */
__xwmp_code
xwer_t xwmp_sel_obj_c0i(struct xwmp_evt * evt, struct xwmp_synobj * synobj)
{
        struct xwmp_evt * owner;
        xwreg_t cpuirq;
        xwer_t rc;

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }

        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        owner = synobj->sel.evt;
        if (evt != owner) {
                rc = -ENOTCONN;
                goto err_notconn;
        }
        xwbmpop_c0i(evt->bmp, synobj->sel.pos);
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_evt_put(evt);
        return XWOK;

err_notconn:
        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        xwmp_evt_put(evt);
err_evt_grab:
        return rc;
}

/**
 * @brief XWMP API：等待事件对象中绑定的同步对象，
 *                  事件对象类型为XWMP_EVT_TYPE_SEL
 * @param evt: (I) 事件对象的指针
 * @param msk: (I) 待触发的同步对象位图掩码
 * @param trg: (O) 指向缓冲区的指针，通过此缓冲区返回已触发的同步对象位图掩码
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 事件对象类型错误
 * @retval -EINTR: 等待被中断
 * @retval -ENOTINTHD: 不在线程上下文中
 * @note
 * - 同步/异步：同步
 * - 上下文：线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_sel_select(struct xwmp_evt * evt, xwbmp_t msk[], xwbmp_t trg[])
{
        xwtm_t expected = XWTM_MAX;
        return xwmp_sel_timedselect(evt, msk, trg, &expected);
}

/**
 * @brief XWMP API：检测一下事件对象中绑定的同步对象，不会阻塞调用者
 * @param evt: (I) 事件对象的指针
 * @param msk: (I) 待触发的同步对象位图掩码
 * @param trg: (O) 指向缓冲区的指针，通过此缓冲区返回已触发的同步对象位图掩码
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 事件对象类型错误
 * @retval -ENODATA: 没有任何信号或事件
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_sel_tryselect(struct xwmp_evt * evt, xwbmp_t msk[], xwbmp_t trg[])
{
        xwer_t rc;
        xwreg_t cpuirq;
        bool triggered;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE((msk), "nullptr", -EFAULT);
        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_SEL),
                      "type-error", -ETYPE);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        triggered = xwbmpop_t1mo(evt->bmp, msk, evt->num);
        if (triggered) {
                if (NULL != trg) {
                        xwbmpop_assign(trg, evt->bmp, evt->num);
                        /* Clear non-exclusive bits */
                        xwbmpop_and(evt->bmp, evt->msk, evt->num);
                        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
                        xwbmpop_and(trg, msk, evt->num);
                } else {
                        /* Clear non-exclusive bits */
                        xwbmpop_and(evt->bmp, evt->msk, evt->num);
                        xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
                }
                rc = XWOK;
        } else {
                rc = -ENODATA;
                xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        }
        xwmp_evt_put(evt);
        return XWOK;

err_evt_grab:
        return rc;
}

/**
 * @brief XWMP API：限时等待事件对象中绑定的同步对象，
 *                  事件对象类型为XWMP_EVT_TYPE_SEL
 * @param evt: (I) 事件对象的指针
 * @param msk: (I) 待触发的同步对象位图掩码
 * @param trg: (O) 指向缓冲区的指针，通过此缓冲区返回已触发的同步对象位图掩码
 * @param xwtm: 指向缓冲区的指针，此缓冲区：
 *              (I) 作为输入时，表示期望的阻塞等待时间
 *              (O) 作为输出时，返回剩余的期望时间
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 事件对象类型错误
 * @retval -ETIMEDOUT: 超时
 * @retval -EINTR: 等待被中断
 * @retval -ENOTINTHD: 不在线程上下文中
 * @note
 * - 同步/异步：同步
 * - 上下文：线程
 * - 重入性：可重入
 * @note
 * - 函数返回返回*-ETIMEDOUT*时，*xwtm*指向的缓冲区内的期望时间会减为0。
 */
__xwmp_api
xwer_t xwmp_sel_timedselect(struct xwmp_evt * evt, xwbmp_t msk[], xwbmp_t trg[],
                            xwtm_t * xwtm)
{
        xwer_t rc;
        xwreg_t cpuirq;
        bool triggered;
        xwsq_t lkst;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE((msk), "nullptr", -EFAULT);
        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_SEL),
                      "type-error", -ETYPE);
        XWOS_VALIDATE((xwtm), "nullptr", -EFAULT);
        XWOS_VALIDATE((-EINTHD == xwmp_irq_get_id(NULL)),
                      "not-in-thd", -ENOTINTHD);

        rc = xwmp_evt_grab(evt);
        if (rc < 0) {
                goto err_evt_grab;
        }
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        while (true) {
                triggered = xwbmpop_t1mo(evt->bmp, msk, evt->num);
                if (triggered) {
                        if (NULL != trg) {
                                xwbmpop_assign(trg, evt->bmp, evt->num);
                                /* Clear non-exclusive bits */
                                xwbmpop_and(evt->bmp, evt->msk, evt->num);
                                xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
                                xwbmpop_and(trg, msk, evt->num);
                        } else {
                                /* Clear non-exclusive bits */
                                xwbmpop_and(evt->bmp, evt->msk, evt->num);
                                xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
                        }
                        rc = XWOK;
                        break;
                } else {
                        /* Clear non-exclusive bits */
                        xwbmpop_and(evt->bmp, evt->msk, evt->num);
                        rc = xwmp_cond_timedwait(&evt->cond,
                                                 &evt->lock, XWOS_LK_SPLK, NULL,
                                                 xwtm, &lkst);
                        if (XWOK == rc) {
                                if (XWOS_LKST_UNLOCKED == lkst) {
                                        xwmp_splk_lock(&evt->lock);
                                }
                        } else {
                                if (XWOS_LKST_LOCKED == lkst) {
                                        xwmp_splk_unlock(&evt->lock);
                                }
                                xwospl_cpuirq_restore_lc(cpuirq);
                                if (NULL != trg) {
                                        xwbmpop_c0all(trg, evt->num);
                                }
                                break;
                        }
                }
        }
        xwmp_evt_put(evt);
        return XWOK;

err_evt_grab:
        return rc;
}

/******** type:XWMP_EVT_TYPE_BR ********/
/**
 * @brief XWMP API：等待所有线程到达栅栏，事件对象类型为XWMP_EVT_TYPE_BR
 * @param evt: (I) 事件对象的指针
 * @param pos: (I) 当前线程的位图位置
 * @param msk: (I) 需要同步的线程位图掩码
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ECHRNG: 位置超出范围
 * @retval -ETYPE: 事件对象类型错误
 * @retval -EINTR: 等待被中断
 * @retval -ENOTINTHD: 不在线程上下文中
 * @note
 * - 同步/异步：同步
 * - 上下文：线程
 * - 重入性：可重入
 */
__xwmp_api
xwer_t xwmp_br_sync(struct xwmp_evt * evt, xwsq_t pos, xwbmp_t msk[])
{
        xwtm_t expected = XWTM_MAX;
        return xwmp_br_timedsync(evt, pos, msk, &expected);
}

/**
 * @brief XWMP API：等待所有线程到达栅栏，事件对象类型为XWMP_EVT_TYPE_BR
 * @param evt: (I) 事件对象的指针
 * @param pos: (I) 当前线程的位图位置
 * @param msk: (I) 需要同步的线程位图掩码
 * @param xwtm: 指向缓冲区的指针，此缓冲区：
 *              (I) 作为输入时，表示期望的阻塞等待时间
 *              (O) 作为输出时，返回剩余的期望时间
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EFAULT: 空指针
 * @retval -ETYPE: 事件对象类型错误
 * @retval -ECHRNG: 位置超出范围
 * @retval -ETIMEDOUT: 超时
 * @retval -EINTR: 等待被中断
 * @retval -ENOTINTHD: 不在线程上下文中
 * @note
 * - 同步/异步：同步
 * - 上下文：线程
 * - 重入性：可重入
 * @note
 * - 函数返回返回*-ETIMEDOUT*时，*xwtm*指向的缓冲区内的期望时间会减为0。
 */
__xwmp_api
xwer_t xwmp_br_timedsync(struct xwmp_evt * evt, xwsq_t pos, xwbmp_t msk[],
                         xwtm_t * xwtm)
{
        xwreg_t cpuirq;
        bool triggered;
        xwsq_t lkst;
        xwer_t rc;

        XWOS_VALIDATE((evt), "nullptr", -EFAULT);
        XWOS_VALIDATE((msk), "nullptr", -EFAULT);
        XWOS_VALIDATE(((evt->type & XWMP_EVT_TYPE_MASK) == XWMP_EVT_TYPE_BR),
                      "type-error", -ETYPE);
        XWOS_VALIDATE((xwtm), "nullptr", -EFAULT);
        XWOS_VALIDATE((-EINTHD == xwmp_irq_get_id(NULL)),
                      "not-in-thd", -ENOTINTHD);

        rc = xwmp_evt_grab(evt);
        if (__xwcc_unlikely(rc < 0)) {
                goto err_evt_grab;
        }
        if (pos >= evt->num) {
                rc = -ECHRNG;
                goto err_pos_range;
        }
        xwmp_splk_lock_cpuirqsv(&evt->lock, &cpuirq);
        xwbmpop_s1i(evt->bmp, pos);
        triggered = xwbmpop_t1ma(evt->bmp, msk, evt->num);
        if (triggered) {
                xwbmpop_c0i(evt->bmp, pos);
                xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
                xwmp_cond_broadcast(&evt->cond);
                xwmp_cthd_yield();
                rc = XWOK;
        } else {
                rc = xwmp_cond_timedwait(&evt->cond,
                                         &evt->lock, XWOS_LK_SPLK, NULL,
                                         xwtm, &lkst);
                if (XWOS_LKST_UNLOCKED == lkst) {
                        xwmp_splk_lock(&evt->lock);
                }
                xwbmpop_c0i(evt->bmp, pos);
                xwmp_splk_unlock_cpuirqrs(&evt->lock, cpuirq);
        }
        xwmp_evt_put(evt);
        return XWOK;

err_pos_range:
        xwmp_evt_put(evt);
err_evt_grab:
        return rc;
}
