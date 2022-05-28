/**
 * @file
 * @brief 玄武OS UP内核操作系统接口描述层：信号选择器
 * @author
 * + 隐星魂 (Roy Sun) <xwos@xwos.tech>
 * @copyright
 * + Copyright © 2015 xwos.tech, All Rights Reserved.
 * > This Source Code Form is subject to the terms of the Mozilla Public
 * > License, v. 2.0. If a copy of the MPL was not distributed with this
 * > file, You can obtain one at <http://mozilla.org/MPL/2.0/>.
 */

#include <xwos/standard.h>
#include <xwos/up/osdl/sync/sel.h>

__xwup_code
xwer_t xwosdl_sel_create(xwosdl_sel_d * seld, xwsz_t num)
{
        xwer_t rc;
        struct xwup_evt * sel;

        XWOS_VALIDATE((seld), "nullptr", -EFAULT);

        rc = xwup_evt_create(&sel, XWUP_EVT_TYPE_SEL, num);
        if (XWOK == rc) {
                seld->sel = sel;
                seld->tik = 1;
        } else {
                *seld = XWOSDL_SEL_NILD;
        }
        return rc;
}

__xwup_code
xwer_t xwosdl_sel_acquire(struct xwosdl_sel * sel, xwsq_t tik)
{
        xwer_t rc;

        if ((NULL == sel) || (0 == tik)) {
                rc = -ENILOBJD;
        } else {
                rc = XWOK;
        }
        return rc;
}

__xwup_code
xwer_t xwosdl_sel_release(struct xwosdl_sel * sel, xwsq_t tik)
{
        xwer_t rc;

        if ((NULL == sel) || (0 == tik)) {
                rc = -ENILOBJD;
        } else {
                rc = XWOK;
        }
        return rc;
}
