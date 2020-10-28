/**
 * @file
 * @brief XuanWuOS内核：调度器
 * @author
 * + 隐星魂 (Roy.Sun) <https://xwos.tech>
 * @copyright
 * + (c) 2015 隐星魂 (Roy.Sun) <https://xwos.tech>
 * > This Source Code Form is subject to the terms of the Mozilla Public
 * > License, v. 2.0. If a copy of the MPL was not distributed with this
 * > file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * @note
 * - XWOS的调度器是一个每CPU变量，为了提高程序的效率，减少cache miss，
 *   通常只由CPU才可访问自己的调度器控制块结构体。其他CPU需要操作当前
 *   CPU的调度器时，需要挂起当前CPU的调度器服务中断，由当前CPU在ISR
 *   中进行操作。
 * - 调度器服务中断处理以下操作：
 *   + 调度器暂停：xwos_scheduler_suspend_lic()
 *                   |--> xwos_scheduler_notify_allfrz_lic()
 *   + 线程退出：xwos_thrd_exit_lic()
 *                 |--> xwos_scheduler_notify_allfrz_lic()
 *   + 线程冻结：xwos_thrd_freeze_lic()
 *                 |--> xwos_scheduler_notify_allfrz_lic()
 *   + 线程解冻：xwos_thrd_thaw_lic()
 *   + 线程迁出：xwos_thrd_outmigrate_lic()
 *   + 线程迁入：xwos_thrd_immigrate_lic()
 * - 调度器服务中断的中断优先级必须为系统最高优先级。
 * - 调度时锁的顺序：同级的锁不可同时获得
 *   + ① xwos_scheduler.cxlock
 *     + ② xwos_scheduler.rq.rt.lock
 *       + ③ xwos_tcb.stlock
 * - 休眠时锁的顺序：同级的锁不可同时获得
 *   + ① xwos_scheduler.pm.lock
 *     + ② xwos_tcb.stlock
 *     + ② xwos_scheduler.tcblistlock
 * - 函数suffix意义：
 *   1. _lc: Local Context，是指只能在“本地”CPU的中执行的代码。
 *   2. _lic: Local ISR Context，是指只能在“本地”CPU的中断上下文中执行的代码。
 *   3. _pmlk: 是只持有锁xwos_scheduler.pm.lock才可调用的函数。
 *   3. _tllk: 是只持有锁xwos_scheduler.tcblistlock才可调用的函数。
 */

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********      include      ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
#include <xwos/standard.h>
#include <string.h>
#include <xwos/lib/xwbop.h>
#include <xwos/lib/bclst.h>
#include <xwos/lib/xwaop.h>
#include <xwos/mm/common.h>
#include <xwos/mm/kma.h>
#include <soc_sched.h>
#include <xwos/smp/irq.h>
#include <xwos/smp/lock/spinlock.h>
#include <xwos/smp/cpu.h>
#include <xwos/smp/pm.h>
#include <xwos/smp/thread.h>
#include <xwos/smp/rtrq.h>
#include <xwos/smp/tt.h>
#if defined(XWSMPCFG_SD_BH) && (1 == XWSMPCFG_SD_BH)
  #include <xwos/smp/bh.h>
#endif /* XWSMPCFG_SD_BH */
#include <xwos/smp/scheduler.h>

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********       macros      ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********       .data       ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
/**
 * @brief 每CPU的调度器
 */
__xwos_data
struct xwos_scheduler xwos_scheduler[CPUCFG_CPU_NUM];

/**
 * @brief 每CPU的调度器的空闲线程栈
 */
__xwos_data __xwcc_aligned_l1cacheline
xwu8_t xwos_scheduler_idled_stack[CPUCFG_CPU_NUM][XWSMPCFG_SD_IDLE_STACK_SIZE];

#if defined(XWSMPCFG_SD_BH) && (1 == XWSMPCFG_SD_BH)
/**
 * @brief 每CPU的调度器的中断底半部栈
 */
__xwos_data __xwcc_aligned_l1cacheline
xwu8_t xwos_scheduler_bhd_stack[CPUCFG_CPU_NUM][XWSMPCFG_SD_BH_STACK_SIZE];
#endif /* XWSMPCFG_SD_BH */

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ********         function prototypes         ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
extern
void bdl_xwsd_idle_hook(struct xwos_scheduler * xwsd);

extern __xwos_code
void xwos_pmdm_report_xwsd_suspended(struct xwos_pmdm * pmdm);

extern __xwos_code
void xwos_pmdm_report_xwsd_resuming(struct xwos_pmdm * pmdm);

static __xwos_code
struct xwos_tcb * xwos_scheduler_rtrq_choose(struct xwos_scheduler * xwsd);

static __xwos_code
xwer_t xwos_scheduler_idled(struct xwos_scheduler * xwsd);

static __xwos_code
void xwos_scheduler_init_idled(struct xwos_scheduler * xwsd);

#if defined(XWSMPCFG_SD_BH) && (1 == XWSMPCFG_SD_BH)
static __xwos_code
xwer_t xwos_scheduler_bhd(struct xwos_scheduler * xwsd);

static __xwos_code
void xwos_scheduler_init_bhd(struct xwos_scheduler * xwsd);

static __xwos_code
xwer_t xwos_scheduler_sw_bh(struct xwos_scheduler * xwsd);

static __xwos_code
void xwos_scheduler_bh_yield(struct xwos_scheduler * xwsd);
#endif /* XWSMPCFG_SD_BH */

static __xwos_code
bool xwos_scheduler_do_chkpmpt(struct xwos_scheduler * xwsd, struct xwos_tcb * t);

static __xwos_code
xwer_t xwos_scheduler_check_swcx(struct xwos_scheduler * xwsd,
                                 struct xwos_tcb * t,
                                 struct xwos_tcb ** pmtcb);

static __xwos_code
xwer_t xwos_scheduler_do_swcx(struct xwos_scheduler * xwsd);

static __xwos_code
void xwos_scheduler_reqfrz_intr_all_lic(struct xwos_scheduler * xwsd);

static __xwos_code
void xwos_scheduler_notify_allfrz_lc(void);

static __xwos_code
void xwos_scheduler_thaw_allfrz_lic(struct xwos_scheduler * xwsd);

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ********      function implementations       ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
__xwos_init_code
xwer_t xwos_scheduler_init_lc(struct xwos_pmdm * xwpmdm)
{
        xwid_t id;
        xwer_t rc;
        struct xwos_scheduler * xwsd;

        id = xwos_cpu_get_id();
        xwsd = &xwos_scheduler[id];

        xwsd->cstk = XWOS_SCHEDULER_IDLE_STK(xwsd);
        xwsd->pstk = NULL;
        xwsd->id = id;
        xwsd->state = false;
        xwsd->req_schedule_cnt = 0;
        xwsd->req_chkpmpt_cnt = 0;
        xwsd->dis_pmpt_cnt = 0;
#if defined(XWSMPCFG_SD_BH) && (1 == XWSMPCFG_SD_BH)
        xwsd->req_bh_cnt = 0;
        xwsd->dis_bh_cnt = 0;
        xwos_bh_cb_init(&xwsd->bhcb);
        xwos_scheduler_init_bhd(xwsd);
#endif /* XWSMPCFG_SD_BH */
        xwlk_splk_init(&xwsd->cxlock);
        xwlk_splk_init(&xwsd->tcblistlock);
        xwlib_bclst_init_head(&xwsd->tcblist);
        xwsd->thrd_num = 0;
        rc = xwos_tt_init(&xwsd->tt);
        if (__xwcc_unlikely(rc < 0)) {
                goto err_tt_init;
        }
        rc = xwos_rtrq_init(&xwsd->rq.rt);
        if (__xwcc_unlikely(rc < 0)) {
                goto err_rtrq_init;
        }
        xwos_scheduler_init_idled(xwsd);
        xwsd->pm.wklkcnt = XWOS_SCHEDULER_WKLKCNT_RUNNING;
        xwsd->pm.frz_thrd_cnt = 0;
        xwlk_splk_init(&xwsd->pm.lock);
        xwlib_bclst_init_head(&xwsd->pm.frzlist);
        xwsd->pm.xwpmdm = xwpmdm;
        rc = soc_scheduler_init(xwsd);
        if (__xwcc_unlikely(rc < 0)) {
                goto err_sd_init;
        }
        return XWOK;

err_sd_init:
        XWOS_BUG();
err_rtrq_init:
        XWOS_BUG();
err_tt_init:
        XWOS_BUG();
        return rc;
}

__xwos_init_code
xwer_t xwos_scheduler_start_lc(void)
{
        struct xwos_scheduler * xwsd;
        xwer_t rc;
        xwid_t id;

        id = xwos_cpu_get_id();
        xwsd = &xwos_scheduler[id];
        rc = xwos_syshwt_start(&xwsd->tt.hwt);
        if (__xwcc_unlikely(XWOK == rc)) {
                xwsd->state = true;
                soc_scheduler_start_lc(xwsd);
        }/* else {} */
        return rc;
}

/**
 * @brief 得到本地CPU调度器的指针
 * @return 本地CPU调度器的指针
 */
__xwos_code
struct xwos_scheduler * xwos_scheduler_get_lc(void)
{
        struct xwos_scheduler * xwsd;
        xwid_t cpuid;

        cpuid = xwos_cpu_get_id();
        xwmb_smp_ddb();
        do {
                xwsd = &xwos_scheduler[cpuid];
                cpuid = xwos_cpu_get_id();
                xwmb_smp_ddb();
        } while (xwsd->id != cpuid);
        return xwsd;
}

/**
 * @brief 得到指定ID的CPU调度器的指针
 * @param cpuid: (I) CPU ID
 * @param ptrbuf: (O) 指向缓冲区的指针，此缓冲区用于返回指定ID的CPU调度器的指针
 * @return 错误码
 */
__xwos_code
xwer_t xwos_scheduler_get_by_cpuid(xwid_t cpuid, struct xwos_scheduler ** ptrbuf)
{
        xwer_t rc;

        if (cpuid < XWOS_CPU_NUM) {
                *ptrbuf = &xwos_scheduler[cpuid];
                rc = XWOK;
        } else {
                rc = -ENODEV;
        }
        return rc;
}

/**
 * @brief 获得调度器中正在运行的线程的线程控制块指针
 * @param xwsd: (I) XWOS调度器的指针
 * @return 调度器中正在运行的线程的线程控制块指针
 */
__xwos_code
struct xwos_tcb * xwos_scheduler_get_ctcb(struct xwos_scheduler * xwsd)
{
        struct xwos_tcb * ctcb;
        ctcb = xwcc_baseof(xwsd->cstk, struct xwos_tcb, stack);
        return ctcb;
}

/**
 * @brief 获得本地CPU的调度器中正在运行的线程的线程控制块指针
 * @return 本地CPU的调度器中正在运行的线程的线程控制块指针
 */
__xwos_code
struct xwos_tcb * xwos_scheduler_get_ctcb_lc(void)
{
        struct xwos_scheduler * xwsd;
        struct xwos_tcb * ctcb;
        xwid_t cpuid;

        cpuid = xwos_cpu_get_id();
        xwmb_smp_ddb();
        do {
                xwsd = &xwos_scheduler[cpuid];
                ctcb = xwcc_baseof(xwsd->cstk, struct xwos_tcb, stack);
                cpuid = xwos_cpu_get_id();
                xwmb_smp_ddb();
        } while (xwsd->id != cpuid);
        return ctcb;
}

/**
 * @brief 从XWOS调度器中选择一个就绪的线程
 * @param xwsd: (I) XWOS调度器的指针
 * @return 线程控制块对象指针或NULL（代表空闲任务）
 * @note
 * - 此函数只能在锁xwsd->cxlock中调用。
 */
static __xwos_code
struct xwos_tcb * xwos_scheduler_rtrq_choose(struct xwos_scheduler * xwsd)
{
        struct xwos_rtrq * xwrtrq;
        struct xwos_tcb * t;
        xwpr_t prio;
        xwer_t rc;

        xwrtrq = &xwsd->rq.rt;
        xwlk_rawly_lock(&xwrtrq->lock);
        t = xwos_rtrq_choose_locked(xwrtrq);
        if (__xwcc_likely(NULL != t)) {
                rc = xwos_rtrq_remove_locked(xwrtrq, t);
                XWOS_BUG_ON(rc);
                xwlk_rawly_lock(&t->stlock);
                prio = t->dprio.rq;
                xwbop_c0m(xwsq_t, &t->state, XWSDOBJ_DST_READY);
                t->dprio.rq = XWOS_SD_PRIORITY_INVALID;
                xwbop_s1m(xwsq_t, &t->state, XWSDOBJ_DST_RUNNING);
                t->dprio.r = prio;
                xwlk_rawly_unlock(&t->stlock);
                xwlk_rawly_unlock(&xwrtrq->lock);
        } else {
                xwlk_rawly_unlock(&xwrtrq->lock);
        }
        return t;
}

/**
 * @brief 空闲任务的主函数
 * @param xwsd: (I) XWOS调度器的指针
 * @return 错误码
 */
static __xwos_code
xwer_t xwos_scheduler_idled(struct xwos_scheduler * xwsd)
{
        XWOS_UNUSED(xwsd);

        while (true) {
                xwos_scheduler_notify_allfrz_lc();
#if defined(BRDCFG_XWSD_IDLE_HOOK) && (1 == BRDCFG_XWSD_IDLE_HOOK)
                bdl_xwsd_idle_hook(xwsd);
#endif /* BRDCFG_XWSD_IDLE_HOOK */
        }
        return XWOK;
}

/**
 * @brief 初始化空闲线程栈
 * @param xwsd: (I) XWOS调度器的指针
 */
static __xwos_code
void xwos_scheduler_init_idled(struct xwos_scheduler * xwsd)
{
        xwsd->idle.name = NULL;
        xwsd->idle.main = (xwos_thrd_f)xwos_scheduler_idled;
        xwsd->idle.arg = xwsd;
        xwsd->idle.size = XWSMPCFG_SD_IDLE_STACK_SIZE;
        xwsd->idle.base = (xwstk_t *)xwos_scheduler_idled_stack[xwsd->id];
#if (defined(XWMMCFG_FD_STACK) && (1 == XWMMCFG_FD_STACK))
        xwsd->idle.sp = xwsd->idle.base + (xwsd->idle.size >> 2);
#elif (defined(XWMMCFG_ED_STACK) && (1 == XWMMCFG_ED_STACK))
        xwsd->idle.sp = xwsd->idle.base + (xwsd->idle.size >> 2) - 1;
#elif (defined(XWMMCFG_FA_STACK) && (1 == XWMMCFG_FA_STACK))
        xwsd->idle.sp = xwsd->idle.base - 1;
#elif (defined(XWMMCFG_EA_STACK) && (1 == XWMMCFG_EA_STACK))
        xwsd->idle.sp = xwsd->idle.base;
#else
  #error "Unknown stack type!"
#endif
        soc_scheduler_init_sdobj_stack(&xwsd->idle, XWSDOBJ_ATTR_PRIVILEGED);
}

#if defined(XWSMPCFG_SD_BH) && (1 == XWSMPCFG_SD_BH)
/**
 * @brief 中断底半部的主函数
 * @param xwsd: (I) XWOS调度器的指针
 * @return 错误码
 */
static __xwos_code
xwer_t xwos_scheduler_bhd(struct xwos_scheduler * xwsd)
{
        struct xwos_bh_node * bhn;
        xwsq_t nv;

        while (1) {
                xwlk_rawly_lock_cpuirq(&xwsd->bhcb.lock);
                if (!xwlib_bclst_tst_empty(&xwsd->bhcb.list)) {
                        bhn = xwlib_bclst_first_entry(&xwsd->bhcb.list,
                                                      struct xwos_bh_node,
                                                      node);
                        xwlib_bclst_del_init(&bhn->node);
                        xwlk_rawly_unlock_cpuirq(&xwsd->bhcb.lock);
                        bhn->func(bhn->arg);
                } else {
                        xwlk_rawly_unlock_cpuirq(&xwsd->bhcb.lock);
                }
                xwaop_sub(xwsq_t, &xwsd->req_bh_cnt, 1, &nv, NULL);
                if (0 == nv) {
                        xwos_scheduler_bh_yield(xwsd);
                }/* else {} */
        }
        return XWOK;
}

/**
 * @brief 初始化中断底半部栈
 * @param xwsd: (I) XWOS调度器的指针
 */
static __xwos_code
void xwos_scheduler_init_bhd(struct xwos_scheduler * xwsd)
{
        xwsd->bh.name = NULL;
        xwsd->bh.main = (xwos_thrd_f)xwos_scheduler_bhd;
        xwsd->bh.arg = xwsd;
        xwsd->bh.size = XWSMPCFG_SD_BH_STACK_SIZE;
        xwsd->bh.base = (xwstk_t *)xwos_scheduler_bhd_stack[xwsd->id];
#if defined(XWMMCFG_FD_STACK) && (1 == XWMMCFG_FD_STACK)
        xwsd->bh.sp = xwsd->bh.base + (xwsd->bh.size >> 2);
#elif defined(XWMMCFG_ED_STACK) && (1 == XWMMCFG_ED_STACK)
        xwsd->bh.sp = xwsd->bh.base + (xwsd->bh.size >> 2) - 1;
#elif defined(XWMMCFG_FA_STACK) && (1 == XWMMCFG_FA_STACK)
        xwsd->bh.sp = xwsd->bh.base - 1;
#elif defined(XWMMCFG_EA_STACK) && (1 == XWMMCFG_EA_STACK)
        xwsd->bh.sp = xwsd->bh.base;
#else
  #error "Unknown stack type!"
#endif
        soc_scheduler_init_sdobj_stack(&xwsd->bh, XWSDOBJ_ATTR_PRIVILEGED);
}

__xwos_api
struct xwos_scheduler * xwos_scheduler_dsbh_lc(void)
{
        struct xwos_scheduler * xwsd;
        xwid_t cpuid;
        bool retry;

        cpuid = xwos_cpu_get_id();
        xwmb_smp_ddb();
        do {
                xwsd = &xwos_scheduler[cpuid];
                xwaop_add(xwsq_t, &xwsd->dis_bh_cnt, 1, NULL, NULL);
                cpuid = xwos_cpu_get_id();
                xwmb_smp_ddb();
                if (__xwcc_unlikely(xwsd->id != cpuid)) {
                        xwaop_sub(xwsq_t, &xwsd->dis_bh_cnt, 1, NULL, NULL);
                        if ((!xwos_scheduler_tst_in_bh(xwsd)) &&
                            (0 != xwaop_load(xwsq_t, &xwsd->req_bh_cnt,
                                             xwmb_modr_acquire))) {
                                xwos_scheduler_sw_bh(xwsd);
                        }/* else {} */
                        retry = true;
                } else {
                        retry = false;
                }
        } while (retry);
        return xwsd;
}

__xwos_api
struct xwos_scheduler * xwos_scheduler_enbh_lc(void)
{
        struct xwos_scheduler * xwsd;
        xwsq_t nv;

        xwsd = xwos_scheduler_get_lc();
        xwaop_sub(xwsq_t, &xwsd->dis_bh_cnt, 1, &nv, NULL);
        if (0 == nv) {
                if (0 != xwaop_load(xwsq_t, &xwsd->req_bh_cnt, xwmb_modr_acquire)) {
                        xwos_scheduler_sw_bh(xwsd);
                }/* else {} */
        }
        return xwsd;
}

/**
 * @brief 禁止调度器进入中断底半部
 * @param xwsd: (I) XWOS调度器的指针
 * @return XWOS调度器的指针
 */
__xwos_code
struct xwos_scheduler * xwos_scheduler_dsbh(struct xwos_scheduler * xwsd)
{
        xwaop_add(xwsq_t, &xwsd->dis_bh_cnt, 1, NULL, NULL);
        return xwsd;
}

/**
 * @brief 允许调度器的中断底半部
 * @param xwsd: (I) XWOS调度器的指针
 * @return XWOS调度器的指针
 */
__xwos_code
struct xwos_scheduler * xwos_scheduler_enbh(struct xwos_scheduler * xwsd)
{
        xwsq_t nv;

        xwaop_sub(xwsq_t, &xwsd->dis_bh_cnt, 1, &nv, NULL);
        if (0 == nv) {
                if (0 != xwaop_load(xwsq_t, &xwsd->req_bh_cnt, xwmb_modr_acquire)) {
                        xwos_scheduler_sw_bh(xwsd);
                }/* else {} */
        }
        return xwsd;
}

/**
 * @brief 切换至中断底半部上下文
 * @param xwsd: (I) XWOS调度器的指针
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -EPERM: 中断底半部已被禁止
 * @retval -EINPROGRESS: 切换上下文的过程正在进行中
 * @retval -EALREADY: 当前上下文已经是中断底半部上下文
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
static __xwos_code
xwer_t xwos_scheduler_sw_bh(struct xwos_scheduler * xwsd)
{
        xwreg_t cpuirq;
        xwer_t rc;

        if (0 == xwaop_load(xwsq_t, &xwsd->dis_bh_cnt, xwmb_modr_relaxed)) {
                xwlk_rawly_lock_cpuirqsv(&xwsd->cxlock, &cpuirq);
                if (__xwcc_unlikely(NULL != xwsd->pstk)) {
                        xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                        rc = -EINPROGRESS;
                } else if (__xwcc_unlikely(XWOS_SCHEDULER_BH_STK(xwsd) == xwsd->cstk)) {
                        xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                        rc = -EALREADY;
                } else {
                        xwsd->pstk = xwsd->cstk;
                        xwsd->cstk = XWOS_SCHEDULER_BH_STK(xwsd);
                        xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                        soc_scheduler_req_swcx(xwsd);
                        rc = XWOK;
                }
        } else {
                rc = -EPERM;
        }
        return rc;
}

/**
 * @brief 请求切至中断底半部
 * @param xwsd: (I) XWOS调度器的指针
 * @return 错误码 @ref xwos_scheduler_sw_bh()
 */
__xwos_code
xwer_t xwos_scheduler_req_bh(struct xwos_scheduler * xwsd)
{
        xwaop_add(xwsq_t, &xwsd->req_bh_cnt, 1, NULL, NULL);
        return xwos_scheduler_sw_bh(xwsd);
}

/**
 * @brief 从中断底半部上下文中返回到线程上下文
 * @param xwsd: (I) XWOS调度器的指针
 */
static __xwos_code
void xwos_scheduler_bh_yield(struct xwos_scheduler * xwsd)
{
        xwreg_t cpuirq;

        xwlk_rawly_lock_cpuirqsv(&xwsd->cxlock, &cpuirq);
        xwsd->cstk = xwsd->pstk;
        xwsd->pstk = XWOS_SCHEDULER_BH_STK(xwsd);
        xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
        soc_scheduler_req_swcx(xwsd);
}
#endif /* XWSMPCFG_SD_BH */

__xwos_api
struct xwos_scheduler * xwos_scheduler_dspmpt_lc(void)
{
        struct xwos_scheduler * xwsd;
        xwid_t cpuid;
        bool retry;

        cpuid = xwos_cpu_get_id();
        xwmb_smp_ddb();
        do {
                xwsd = &xwos_scheduler[cpuid];
                xwaop_add(xwsq_t, &xwsd->dis_pmpt_cnt, 1, NULL, NULL);
                cpuid = xwos_cpu_get_id();
                xwmb_smp_ddb();
                if (__xwcc_unlikely(xwsd->id != cpuid)) {
                        xwaop_sub(xwsq_t, &xwsd->dis_pmpt_cnt, 1, NULL, NULL);
                        if (0 != xwaop_load(xwsq_t, &xwsd->req_chkpmpt_cnt,
                                            xwmb_modr_acquire)) {
                                xwos_scheduler_chkpmpt(xwsd);
                        }/* else {} */
                        retry = true;
                } else {
                        retry = false;
                }
        } while (retry);
        return xwsd;
}

__xwos_api
struct xwos_scheduler * xwos_scheduler_enpmpt_lc(void)
{
        struct xwos_scheduler * xwsd;
        xwsq_t nv;

        xwsd = xwos_scheduler_get_lc();
        xwaop_sub(xwsq_t, &xwsd->dis_pmpt_cnt, 1, &nv, NULL);
        if (0 == nv) {
                if (0 != xwaop_load(xwsq_t, &xwsd->req_chkpmpt_cnt,
                                    xwmb_modr_acquire)) {
                        xwos_scheduler_chkpmpt(xwsd);
                }/* else {} */
        }
        return xwsd;
}

/**
 * @brief 禁止调度器的抢占
 * @param xwsd: (I) XWOS调度器的指针
 * @return XWOS调度器的指针
 */
__xwos_code
struct xwos_scheduler * xwos_scheduler_dspmpt(struct xwos_scheduler * xwsd)
{
        xwaop_add(xwsq_t, &xwsd->dis_pmpt_cnt, 1, NULL, NULL);
        return xwsd;
}

/**
 * @brief 允许调度器的抢占
 * @param xwsd: (I) XWOS调度器的指针
 * @return XWOS调度器的指针
 */
__xwos_code
struct xwos_scheduler * xwos_scheduler_enpmpt(struct xwos_scheduler * xwsd)
{
        xwsq_t nv;

        xwaop_sub(xwsq_t, &xwsd->dis_pmpt_cnt, 1, &nv, NULL);
        if (0 == nv) {
                if (0 != xwaop_load(xwsq_t, &xwsd->req_chkpmpt_cnt,
                                    xwmb_modr_acquire)) {
                        xwos_scheduler_chkpmpt(xwsd);
                }/* else {} */
        }
        return xwsd;
}

/**
 * @brief 检查是否需要抢占
 * @param xwsd: (I) XWOS调度器的指针
 * @param t: (I) 线程控制块对象的指针
 * @return 布尔值
 * @retval true: 需要抢占
 * @retval false: 不需要抢占
 * @note
 * - 此函数被调用时需要获得锁xwsd->cxlock并且关闭本地CPU的中断。
 */
static __xwos_code
bool xwos_scheduler_do_chkpmpt(struct xwos_scheduler * xwsd, struct xwos_tcb * t)
{
        struct xwos_rtrq * xwrtrq;
        bool sched;

        xwrtrq = &xwsd->rq.rt;
        XWOS_BUG_ON(&t->stack == &xwsd->idle);
#if defined(XWSMPCFG_SD_BH) && (1 == XWSMPCFG_SD_BH)
        XWOS_BUG_ON(&t->stack == &xwsd->bh);
#endif /* XWSMPCFG_SD_BH */
        xwlk_rawly_lock(&xwrtrq->lock);
        xwlk_rawly_lock(&t->stlock);
        if (XWSDOBJ_DST_RUNNING & t->state) {
                if (t->dprio.r >= xwrtrq->top) {
                        sched = false;
                } else {
                        sched = true;
                }
        } else {
                /* The thread will give up the CPU in a jiffy.
                   We don't check the preemption */
                sched = false;
        }
        xwlk_rawly_unlock(&t->stlock);
        xwlk_rawly_unlock(&xwrtrq->lock);
        return sched;
}

/**
 * @brief 调度器检测抢占
 * @param xwsd: (I) XWOS调度器的指针
 */
__xwos_code
void xwos_scheduler_chkpmpt(struct xwos_scheduler * xwsd)
{
        struct xwos_tcb * t;
        struct xwos_sdobj_stack_info * cstk;
        xwreg_t cpuirq;
        bool sched;

        if (0 != xwaop_load(xwsq_t, &xwsd->dis_pmpt_cnt, xwmb_modr_acquire)) {
                xwaop_add(xwsq_t, &xwsd->req_chkpmpt_cnt, 1, NULL, NULL);
        } else {
                xwlk_rawly_lock_cpuirqsv(&xwsd->cxlock, &cpuirq);
#if defined(XWSMPCFG_SD_BH) && (1 == XWSMPCFG_SD_BH)
                if (XWOS_SCHEDULER_BH_STK(xwsd) == xwsd->cstk) {
                        cstk = xwsd->pstk;
                } else {
                        cstk = xwsd->cstk;
                }
#else /* XWSMPCFG_SD_BH */
                cstk = xwsd->cstk;
#endif /* !XWSMPCFG_SD_BH */
                if (XWOS_SCHEDULER_IDLE_STK(xwsd) != cstk) {
                        t = xwcc_baseof(cstk, struct xwos_tcb, stack);
                        sched = xwos_scheduler_do_chkpmpt(xwsd, t);
                } else {
                        sched = true;
                }
                xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                xwaop_write(xwsq_t, &xwsd->req_chkpmpt_cnt, 0, NULL);
                if (sched) {
                        xwos_scheduler_req_swcx(xwsd);
                }/* else {} */
        }
}

/**
 * @brief 所有CPU的调度器检测抢占
 */
__xwos_code
void xwos_scheduler_chkpmpt_all(void)
{
        struct xwos_scheduler * xwsd;
        xwid_t cpuid;

        for (cpuid = 0; cpuid < (xwid_t)CPUCFG_CPU_NUM; cpuid++) {
                xwsd = &xwos_scheduler[cpuid];
                xwos_scheduler_chkpmpt(xwsd);
        }
}

/**
 * @brief 检测调度器是否需要切换上下文
 * @param xwsd: (I) XWOS调度器的指针
 * @param t: (I) 被检测的线程控制块对象的指针
 * @param pmtcb: (O) 指向缓冲区的指针，此缓冲区用于返回抢占线程的控制块的指针
 * @return 错误码
 * @retval OK: 需要切换上下文
 * @retval <0: 不需要切换上下文
 * @note
 * - 此函数被调用时需要获得锁xwsd->cxlock并且关闭本地CPU的中断。
 */
static __xwos_code
xwer_t xwos_scheduler_check_swcx(struct xwos_scheduler * xwsd,
                                 struct xwos_tcb * t,
                                 struct xwos_tcb ** pmtcb)
{
        struct xwos_rtrq * xwrtrq;
        xwpr_t prio;
        xwer_t rc;

        xwrtrq = &xwsd->rq.rt;
        xwlk_rawly_lock(&xwrtrq->lock);
        xwlk_rawly_lock(&t->stlock);
        if (XWSDOBJ_DST_RUNNING & t->state) {
                if (t->dprio.r >= xwrtrq->top) {
                        xwlk_rawly_unlock(&t->stlock);
                        xwlk_rawly_unlock(&xwrtrq->lock);
                        rc = -EPERM;
                } else {
                        prio = t->dprio.r;
                        xwbop_c0m(xwsq_t, &t->state, XWSDOBJ_DST_RUNNING);
                        t->dprio.r = XWOS_SD_PRIORITY_INVALID;
                        xwlk_rawly_unlock(&t->stlock);
                        xwlk_rawly_unlock(&xwrtrq->lock);
                        rc = xwos_thrd_rq_add_head(t, prio);
                        XWOS_BUG_ON(rc < 0);
                        *pmtcb = xwos_scheduler_rtrq_choose(xwsd);
                        rc = XWOK;
                }
        } else {
                xwlk_rawly_unlock(&t->stlock);
                xwlk_rawly_unlock(&xwrtrq->lock);
                *pmtcb = xwos_scheduler_rtrq_choose(xwsd);
                rc = XWOK;
        }
        return rc;
}

/**
 * @brief 执行上下文的切换
 * @param xwsd: (I) XWOS调度器的指针
 * @retval rc: @ref xwos_scheduler_req_swcx()
 * @note
 * - 此函数被调用时需要获得锁xwsd->cxlock并且关闭本地CPU的中断。
 */
static __xwos_code
xwer_t xwos_scheduler_do_swcx(struct xwos_scheduler * xwsd)
{
        struct xwos_tcb * swtcb, * ctcb;
        xwer_t rc;

        if (XWOS_SCHEDULER_IDLE_STK(xwsd) == xwsd->cstk) {
                swtcb = xwos_scheduler_rtrq_choose(xwsd);
                if (NULL != swtcb) {
                        xwsd->pstk = xwsd->cstk;
                        xwsd->cstk = &swtcb->stack;
                        rc = XWOK;
                } else {
                        rc = -EAGAIN;
                }
        } else {
                ctcb = xwos_scheduler_get_ctcb(xwsd);
                rc = xwos_scheduler_check_swcx(xwsd, ctcb, &swtcb);
                if (XWOK == rc) {
                        if (ctcb == swtcb) {
                                /* 当前线程的状态可能在中断或其他CPU中被改变，
                                   并又被加入到就绪队列中，然后在此处又被重新选择。
                                   这种情况下，调度器只需要恢复此线程的
                                   @ref XWSDOBJ_DST_RUNNING状态，
                                   并不需要切换线程上下文。*/
                                rc = -EAGAIN;
                        } else {
                                if (NULL == swtcb) {
                                        xwsd->cstk = XWOS_SCHEDULER_IDLE_STK(xwsd);
                                } else {
                                        xwsd->cstk = &swtcb->stack;
                                }
                                xwsd->pstk = &ctcb->stack;
                                rc = XWOK;
                        }
                } else {
                        rc = -EINVAL;
                }
        }
        return rc;
}

#if defined(XWSMPCFG_SD_BH) && (1 == XWSMPCFG_SD_BH)
/**
 * @brief 请求切换上下文
 * @param xwsd: (I) XWOS调度器的指针
 * @return 错误码
 * @retval OK: 需要触发切换上下文的中断执行切换上下文的过程
 * @retval -EBUSY: 当前上下文为中断底半部上下文
 * @retval -EINVAL: 当前正在运行的线程状态错误
 * @retval -EAGAIN: 不需要切换上下文
 * @retval -EEINPROGRESS: 切换上下文的过程正在进行
 * @note
 * - 同步/异步：异步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwos_code
xwer_t xwos_scheduler_req_swcx(struct xwos_scheduler * xwsd)
{
        xwreg_t cpuirq;
        xwer_t rc;

        xwlk_rawly_lock_cpuirqsv(&xwsd->cxlock, &cpuirq);
        xwsd->req_schedule_cnt++;
        if (__xwcc_unlikely(NULL != xwsd->pstk)) {
                xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                rc = -EINPROGRESS;
        } else if (__xwcc_unlikely(XWOS_SCHEDULER_BH_STK(xwsd) == xwsd->cstk)) {
                xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                rc = -EBUSY;
        } else {
                xwsd->pstk = err_ptr(-EINVAL); /* Invalidate other caller. */
                rc = xwos_scheduler_do_swcx(xwsd);
                xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                if (XWOK == rc) {
                        soc_scheduler_req_swcx(xwsd);
                } else {
                        xwos_scheduler_finish_swcx(xwsd);
                }
        }
        return rc;
}

/**
 * @brief 结束上下文的切换
 * @param xwsd: (I) XWOS调度器的指针
 * @note
 * - 此函数需要在BSP中切换线程上下文的中断函数的最后一步调用。
 */
__xwos_code
void xwos_scheduler_finish_swcx(struct xwos_scheduler * xwsd)
{
        xwreg_t cpuirq;
        xwsq_t rbc;

        xwlk_rawly_lock_cpuirqsv(&xwsd->cxlock, &cpuirq);
        if (XWOS_SCHEDULER_BH_STK(xwsd) == xwsd->cstk) {
                /* Finish switching context from thread to BH */
                xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
        } else if (XWOS_SCHEDULER_BH_STK(xwsd) == xwsd->pstk) {
                /* Finish switching context from BH to thread */
                xwsd->pstk = NULL;
                rbc = xwaop_load(xwsq_t, &xwsd->req_bh_cnt, xwmb_modr_relaxed);
                if (rbc >= 1) {
                        xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                        xwos_scheduler_sw_bh(xwsd);
                } else if (xwsd->req_schedule_cnt > 0) {
                        xwsd->req_schedule_cnt = 0;
                        xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                        xwos_scheduler_req_swcx(xwsd);
                } else {
                        xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                }
        } else {
                /* Finish switching context from thread to thread */
                xwsd->pstk = NULL;
                xwsd->req_schedule_cnt--;
                rbc = xwaop_load(xwsq_t, &xwsd->req_bh_cnt, xwmb_modr_relaxed);
                if (rbc >= 1) {
                        xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                        xwos_scheduler_sw_bh(xwsd);
                } else if (xwsd->req_schedule_cnt > 0) {
                        xwsd->req_schedule_cnt = 0;
                        xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                        xwos_scheduler_req_swcx(xwsd);
                } else {
                        xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                }
        }
}
#else /* XWSMPCFG_SD_BH */
/**
 * @brief 请求切换上下文
 * @param xwsd: (I) XWOS调度器的指针
 * @return 错误码
 * @retval OK: 需要触发切换上下文的中断执行切换上下文的过程
 * @retval -EINVAL: 当前正在运行的线程状态错误
 * @retval -EAGAIN: 不需要切换上下文
 * @retval -EEINPROGRESS: 切换上下文的过程正在进行
 * @note
 * - 同步/异步：异步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwos_code
xwer_t xwos_scheduler_req_swcx(struct xwos_scheduler * xwsd)
{
        xwreg_t cpuirq;
        xwer_t rc;

        xwlk_rawly_lock_cpuirqsv(&xwsd->cxlock, &cpuirq);
        xwsd->req_schedule_cnt++;
        if (__xwcc_unlikely(NULL != xwsd->pstk)) {
                xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                rc = -EINPROGRESS;
        } else {
                xwsd->pstk = err_ptr(-EINVAL); /* Invalidate other caller. */
                rc = xwos_scheduler_do_swcx(xwsd);
                xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                if (XWOK == rc) {
                        soc_scheduler_req_swcx(xwsd);
                } else {
                        xwos_scheduler_finish_swcx(xwsd);
                }
        }
        return rc;
}

/**
 * @brief 结束上下文的切换
 * @param xwsd: (I) XWOS调度器的指针
 * @note
 * - 此函数需要在BSP中切换线程上下文的中断函数的最后一步调用。
 */
__xwos_code
void xwos_scheduler_finish_swcx(struct xwos_scheduler * xwsd)
{
        xwreg_t cpuirq;

        xwlk_rawly_lock_cpuirqsv(&xwsd->cxlock, &cpuirq);
        /* Finish switching context from thread to thread */
        xwsd->pstk = NULL;
        xwsd->req_schedule_cnt--;
        if (xwsd->req_schedule_cnt > 0) {
                xwsd->req_schedule_cnt = 0;
                xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
                xwos_scheduler_req_swcx(xwsd);
        } else {
                xwlk_rawly_unlock_cpuirqrs(&xwsd->cxlock, cpuirq);
        }
}
#endif /* !XWSMPCFG_SD_BH */

/**
 * @brief 将调度器的唤醒锁计数器加1
 * @param xwsd: (I) 调度器对象的指针
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval <0: 当前调度器正在进入低功耗模式
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwos_code
xwer_t xwos_scheduler_inc_wklkcnt(struct xwos_scheduler * xwsd)
{
        xwer_t rc;

        rc = xwaop_tge_then_add(xwsq_t, &xwsd->pm.wklkcnt,
                                XWOS_SCHEDULER_WKLKCNT_UNLOCKED, 1,
                                NULL, NULL);
        return rc;
}

/**
 * @brief 将调度器的唤醒锁计数器减1
 * @param xwsd: (I) 调度器对象的指针
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval <0: 当前调度器正在进入低功耗
 * @note
 * - 同步/异步：同步
 * - 上下文：中断、中断底半部、线程
 * - 重入性：可重入
 */
__xwos_code
xwer_t xwos_scheduler_dec_wklkcnt(struct xwos_scheduler * xwsd)
{
        xwer_t rc;
        xwsq_t nv;

        rc = xwaop_tge_then_sub(xwsq_t, &xwsd->pm.wklkcnt,
                                XWOS_SCHEDULER_WKLKCNT_UNLOCKED,
                                1,
                                &nv, NULL);
        if ((XWOK == rc) && (XWOS_SCHEDULER_WKLKCNT_FREEZING == nv)) {
                soc_scheduler_suspend(xwsd);
        }/* else {} */
        return rc;
}

/**
 * @brief 中断本地CPU中所有线程的阻塞或睡眠状态，并准备冻结
 * @param xwsd: (I) 调度器对象的指针
 */
static __xwos_code
void xwos_scheduler_reqfrz_intr_all_lic(struct xwos_scheduler * xwsd)
{
        struct xwos_tcb * c, * n;
        xwreg_t cpuirq;

        xwlk_splk_lock_cpuirqsv(&xwsd->tcblistlock, &cpuirq);
        xwlib_bclst_itr_next_entry_safe(c, n, &xwsd->tcblist,
                                        struct xwos_tcb, tcbnode) {
                xwos_thrd_grab(c);
                xwlk_splk_unlock_cpuirqrs(&xwsd->tcblistlock, cpuirq);
                xwos_thrd_reqfrz_lic(c);
                xwos_thrd_intr(c);
                xwos_thrd_put(c);
                xwlk_splk_lock_cpuirqsv(&xwsd->tcblistlock, &cpuirq);
        }
        xwlk_splk_unlock_cpuirqrs(&xwsd->tcblistlock, cpuirq);
}

/**
 * @brief 通知所有线程已经冻结（中断上下文中执行的部分）
 * @param xwsd: (I) 调度器对象的指针
 * @return 错误码
 */
__xwos_code
xwer_t xwos_scheduler_notify_allfrz_lic(struct xwos_scheduler * xwsd)
{
        xwer_t rc;
        xwsq_t nv;

        rc = xwaop_teq_then_sub(xwsq_t, &xwsd->pm.wklkcnt,
                                XWOS_SCHEDULER_WKLKCNT_FREEZING,
                                1,
                                &nv, NULL);
        if ((XWOK == rc) && (XWOS_SCHEDULER_WKLKCNT_ALLFRZ == nv)) {
                xwos_syshwt_stop(&xwsd->tt.hwt);
        } else {
                rc = -EINTR;
        }
        xwos_scheduler_req_swcx(xwsd);
        return rc;
}

/**
 * @brief 通知所有线程已经冻结（空闲任务中执行的部分）
 */
static __xwos_code
void xwos_scheduler_notify_allfrz_lc(void)
{
        struct xwos_scheduler * xwsd;
        xwsq_t nv;
        xwer_t rc;

        xwsd = xwos_scheduler_get_lc();
        rc = xwaop_teq_then_sub(xwsq_t, &xwsd->pm.wklkcnt,
                                XWOS_SCHEDULER_WKLKCNT_ALLFRZ,
                                1,
                                &nv, NULL);
        if ((XWOK == rc) && (XWOS_SCHEDULER_WKLKCNT_SUSPENDED == nv)) {
                if (xwsd->pm.xwpmdm) {
                        xwos_pmdm_report_xwsd_suspended(xwsd->pm.xwpmdm);
                }/* else {} */
        }
}

/**
 * @brief 解冻本地CPU中的所有线程
 * @param xwsd: (I) 本地CPU调度器对象的指针
 * @return 错误码
 */
static __xwos_code
void xwos_scheduler_thaw_allfrz_lic(struct xwos_scheduler * xwsd)
{
        struct xwos_tcb * c, * n;
        xwreg_t cpuirq;

        xwlk_splk_lock_cpuirqsv(&xwsd->pm.lock, &cpuirq);
        xwlk_splk_lock(&xwsd->tcblistlock);
        xwlib_bclst_itr_next_entry_safe(c, n, &xwsd->tcblist,
                                        struct xwos_tcb, tcbnode) {
                xwos_thrd_grab(c);
                xwlk_splk_unlock(&xwsd->tcblistlock);
                xwos_thrd_thaw_lic_pmlk(c);
                xwos_thrd_put(c);
                xwlk_splk_lock(&xwsd->tcblistlock);
        }
        xwlk_splk_unlock(&xwsd->tcblistlock);
        xwlk_splk_unlock_cpuirqrs(&xwsd->pm.lock, cpuirq);
        xwos_scheduler_req_swcx(xwsd);
        XWOS_BUG_ON(xwsd->pm.frz_thrd_cnt != 0);
}

/**
 * @brief 暂停本地CPU的调度器
 * @param xwsd: (I) 调度器对象的指针
 * @return 错误码
 * @note
 * - 此函数只能在CPU自身的调度器服务中断中执行，当电源管理的代码运行于
 *   其他CPU上时，可通过@ref xwos_scheduler_suspend(cpuid)向对应CPU申请
 *   调度器服务中断并执行此函数。
 */
__xwos_code
xwer_t xwos_scheduler_suspend_lic(struct xwos_scheduler * xwsd)
{
        xwreg_t cpuirq;

        xwos_scheduler_reqfrz_intr_all_lic(xwsd);
        xwlk_splk_lock_cpuirqsv(&xwsd->pm.lock, &cpuirq);
        xwlk_splk_lock(&xwsd->tcblistlock);
        if (xwsd->thrd_num == xwsd->pm.frz_thrd_cnt) {
                xwlk_splk_unlock(&xwsd->tcblistlock);
                xwlk_splk_unlock_cpuirqrs(&xwsd->pm.lock, cpuirq);
                xwos_scheduler_notify_allfrz_lic(xwsd);
        } else {
                xwlk_splk_unlock(&xwsd->tcblistlock);
                xwlk_splk_unlock_cpuirqrs(&xwsd->pm.lock, cpuirq);
        }
        return XWOK;
}

__xwos_api
xwer_t xwos_scheduler_suspend(xwid_t cpuid)
{
        struct xwos_scheduler * xwsd;
        xwer_t rc;

        rc = xwos_scheduler_get_by_cpuid(cpuid, &xwsd);
        if (XWOK == rc) {
                rc = xwos_scheduler_dec_wklkcnt(xwsd);
        }/* else {} */
        return rc;
}

/**
 * @brief 继续本地CPU调度器
 * @param xwsd: (I) 调度器对象的指针
 * @return 错误码
 * @note
 * - 此函数只能在CPU自身的调度器服务中断中执行，因此当电源管理的代码运行在
 *   其他CPU上时，可通过@ref xwos_scheduler_resume(cpuid)向对应CPU申请
 *   调度器服务中断执行此函数。
 */
__xwos_code
xwer_t xwos_scheduler_resume_lic(struct xwos_scheduler * xwsd)
{
        xwer_t rc;
        xwsq_t nv, ov;

        do {
                rc = xwaop_teq_then_add(xwsq_t, &xwsd->pm.wklkcnt,
                                        XWOS_SCHEDULER_WKLKCNT_SUSPENDED,
                                        1,
                                        &nv, &ov);
                if ((XWOK == rc) && (XWOS_SCHEDULER_WKLKCNT_ALLFRZ == nv)) {
                        xwos_pmdm_report_xwsd_resuming(xwsd->pm.xwpmdm);
                }

                rc = xwaop_teq_then_add(xwsq_t, &xwsd->pm.wklkcnt,
                                        XWOS_SCHEDULER_WKLKCNT_ALLFRZ,
                                        1,
                                        &nv, &ov);
                if ((XWOK == rc) && (XWOS_SCHEDULER_WKLKCNT_THAWING == nv)) {
                        xwos_syshwt_start(&xwsd->tt.hwt);
                }

                rc = xwaop_teq_then_add(xwsq_t, &xwsd->pm.wklkcnt,
                                        XWOS_SCHEDULER_WKLKCNT_THAWING,
                                        1,
                                        &nv, &ov);
                if ((XWOK == rc) && (XWOS_SCHEDULER_WKLKCNT_UNLOCKED == nv)) {
                        xwos_scheduler_thaw_allfrz_lic(xwsd);
                        rc = XWOK;
                        break;
                } else if (ov >= XWOS_SCHEDULER_WKLKCNT_UNLOCKED) {
                        rc = -EALREADY;
                        break;
                } else {
                }
        } while (true);
        return rc;
}

__xwos_api
xwer_t xwos_scheduler_resume(xwid_t cpuid)
{
        struct xwos_scheduler * xwsd;
        xwid_t localid;
        xwer_t rc;

        localid = xwos_cpu_get_id();
        if (localid == cpuid) {
                xwsd = xwos_scheduler_get_lc();
                if (XWOK == xwos_irq_get_id(NULL)) {
                        rc = xwos_scheduler_resume_lic(xwsd);
                } else {
                        rc = soc_scheduler_resume(xwsd);
                }
        } else {
                rc = xwos_scheduler_get_by_cpuid(cpuid, &xwsd);
                if (XWOK == rc) {
                        rc = soc_scheduler_resume(xwsd);
                }/* else {} */
        }
        return rc;
}

__xwos_api
xwsq_t xwos_scheduler_get_pm_state(struct xwos_scheduler * xwsd)
{
        xwsq_t wklkcnt;

        wklkcnt = xwaop_load(xwsq_t, &xwsd->pm.wklkcnt, xwmb_modr_acquire);
        return wklkcnt;
}

__xwos_api
void xwos_scheduler_get_context_lc(xwsq_t * ctxbuf, xwirq_t * irqnbuf)
{
        struct xwos_scheduler * xwsd;
        xwirq_t irqn;
        xwer_t rc;
        xwsq_t ctx;

        rc = xwos_irq_get_id(&irqn);
        if (XWOK == rc) {
                ctx = XWOS_SCHEDULER_CONTEXT_ISR;
                if (irqnbuf) {
                        *irqnbuf = irqn;
                }
        } else {
                xwsd = xwos_scheduler_get_lc();
                if (xwsd->state) {
                        if (-EINBH == rc) {
                                ctx = XWOS_SCHEDULER_CONTEXT_BH;
                        } else {
                                ctx = XWOS_SCHEDULER_CONTEXT_THRD;
                        }
                } else {
                        ctx = XWOS_SCHEDULER_CONTEXT_INIT_EXIT;
                }
        }
        if (ctxbuf) {
                *ctxbuf = ctx;
        }
}
