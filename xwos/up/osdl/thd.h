/**
 * @file
 * @brief XWOS UP内核操作系统接口描述层：调度
 * @author
 * + 隐星魂 (Roy Sun) <xwos@xwos.tech>
 * @copyright
 * + Copyright © 2015 xwos.tech, All Rights Reserved.
 * > This Source Code Form is subject to the terms of the Mozilla Public
 * > License, v. 2.0. If a copy of the MPL was not distributed with this
 * > file, You can obtain one at <http://mozilla.org/MPL/2.0/>.
 */

#ifndef __xwos_up_osdl_thd_h__
#define __xwos_up_osdl_thd_h__

#include <xwos/up/thd.h>

#if defined(XWOSCFG_SKD_THD_LOCAL_DATA_NUM)
#  define XWOSDL_THD_LOCAL_DATA_NUM             XWOSCFG_SKD_THD_LOCAL_DATA_NUM
#else
#  define XWOSDL_THD_LOCAL_DATA_NUM             (0U)
#endif

#define xwosdl_thd_attr xwup_thd_attr
#define xwosdl_thd xwup_thd
typedef xwup_thd_f xwosdl_thd_f;

typedef struct {
        struct xwosdl_thd * thd;
        xwsq_t tik;
} xwosdl_thd_d;

#define XWOSDL_THD_NILD ((xwosdl_thd_d){NULL, 0,})

static __xwcc_inline
void xwosdl_thd_attr_init(struct xwosdl_thd_attr * attr)
{
        xwup_thd_attr_init(attr);
}

xwer_t xwosdl_thd_init(struct xwosdl_thd * thd, xwosdl_thd_d * thdd,
                       const struct xwosdl_thd_attr * inattr,
                       xwosdl_thd_f thdfunc, void * arg);

static __xwcc_inline
xwer_t xwosdl_thd_grab(struct xwosdl_thd * thd)
{
        XWOS_VALIDATE((thd), "nullptr", -EFAULT);

        return xwup_thd_grab(thd);
}

static __xwcc_inline
xwer_t xwosdl_thd_put(struct xwosdl_thd * thd)
{
        XWOS_VALIDATE((thd), "nullptr", -EFAULT);

        return xwup_thd_put(thd);
}

xwer_t xwosdl_thd_create(xwosdl_thd_d * thdd,
                         const struct xwosdl_thd_attr * attr,
                         xwosdl_thd_f thdfunc, void * arg);

static __xwcc_inline
xwer_t xwosdl_thd_acquire(struct xwosdl_thd * thd, xwsq_t tik)
{
        return xwup_thd_acquire(thd, tik);
}

static __xwcc_inline
xwer_t xwosdl_thd_release(struct xwosdl_thd * thd, xwsq_t tik)
{
        return xwup_thd_release(thd, tik);
}

xwer_t xwosdl_thd_get_attr(struct xwosdl_thd * thd, xwsq_t tik,
                           struct xwosdl_thd_attr * attr);

xwer_t xwosdl_thd_intr(struct xwosdl_thd * thd, xwsq_t tik);

xwer_t xwosdl_thd_quit(struct xwosdl_thd * thd, xwsq_t tik);

xwer_t xwosdl_thd_join(struct xwosdl_thd * thd, xwsq_t tik, xwer_t * trc);

xwer_t xwosdl_thd_stop(struct xwosdl_thd * thd, xwsq_t tik, xwer_t * rc);

xwer_t xwosdl_thd_detach(struct xwosdl_thd * thd, xwsq_t tik);

static __xwcc_inline
xwer_t xwosdl_thd_migrate(struct xwosdl_thd * thd, xwsq_t tik, xwid_t dstcpu)
{
        XWOS_UNUSED(thd);
        XWOS_UNUSED(tik);
        XWOS_UNUSED(dstcpu);
        return -ENOSYS;
}

xwosdl_thd_d xwosdl_cthd_self(void);

static __xwcc_inline
void xwosdl_cthd_yield(void)
{
        return xwup_cthd_yield();
}

static __xwcc_inline
void xwosdl_cthd_exit(xwer_t rc)
{
        xwup_cthd_exit(rc);
}

static __xwcc_inline
bool xwosdl_cthd_shld_frz(void)
{
        return xwup_cthd_shld_frz();
}

static __xwcc_inline
bool xwosdl_cthd_shld_stop(void)
{
        return xwup_cthd_shld_stop();
}

static __xwcc_inline
bool xwosdl_cthd_frz_shld_stop(bool * frozen)
{
        return xwup_cthd_frz_shld_stop(frozen);
}

static __xwcc_inline
xwer_t xwosdl_cthd_sleep_to(xwtm_t to)
{
        return xwup_cthd_sleep_to(to);
}

static __xwcc_inline
xwer_t xwosdl_cthd_sleep_from(xwtm_t * from, xwtm_t dur)
{
        XWOS_VALIDATE((from), "nullptr", -EFAULT);

        return xwup_cthd_sleep_from(from, dur);
}

static __xwcc_inline
xwer_t xwosdl_cthd_freeze(void)
{
        return xwup_cthd_freeze();
}

#if defined(XWOSCFG_SKD_THD_LOCAL_DATA_NUM) && (XWOSCFG_SKD_THD_LOCAL_DATA_NUM > 0U)
xwer_t xwosdl_thd_set_data(struct xwosdl_thd * thd, xwsq_t tik,
                           xwsq_t pos, void * data);

xwer_t xwosdl_thd_get_data(struct xwosdl_thd * thd, xwsq_t tik,
                           xwsq_t pos, void ** databuf);

static __xwos_inline_api
xwer_t xwosdl_cthd_set_data(xwsq_t pos, void * data)
{
        return xwup_cthd_set_data(pos, data);
}

static __xwos_inline_api
xwer_t xwosdl_cthd_get_data(xwsq_t pos, void ** databuf)
{
        return xwup_cthd_get_data(pos, databuf);

}
#endif

#endif /* xwos/up/osdl/thd.h */
