/**
 * @file
 * @brief 玄武OS UP内核操作系统接口描述层：互斥锁
 * @author
 * + 隐星魂 (Roy Sun) <xwos@xwos.tech>
 * @copyright
 * + Copyright © 2015 xwos.tech, All Rights Reserved.
 * > This Source Code Form is subject to the terms of the Mozilla Public
 * > License, v. 2.0. If a copy of the MPL was not distributed with this
 * > file, You can obtain one at <http://mozilla.org/MPL/2.0/>.
 */

#include <xwos/standard.h>
#include <xwos/up/osdl/lock/mtx.h>

__xwup_code
xwer_t xwosdl_mtx_grab(struct xwosdl_mtx * mtx)
{
        XWOS_VALIDATE((mtx), "nullptr", -EFAULT);
        return xwup_mtx_grab(mtx);
}

__xwup_code
xwer_t xwosdl_mtx_put(struct xwosdl_mtx * mtx)
{
        XWOS_VALIDATE((mtx), "nullptr", -EFAULT);
        return xwup_mtx_put(mtx);
}

__xwup_code
xwer_t xwosdl_mtx_create(xwosdl_mtx_d * mtxd, xwpr_t sprio)
{
        xwer_t rc;
        struct xwup_mtx * mtx;

        XWOS_VALIDATE((mtxd), "nullptr", -EFAULT);

        rc = xwup_mtx_create(&mtx, sprio);
        if (XWOK == rc) {
                mtxd->mtx = mtx;
                mtxd->tik = mtx->xwobj.tik;
        } else {
                *mtxd = XWOSDL_MTX_NILD;
        }
        return rc;
}
