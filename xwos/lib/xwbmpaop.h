/**
 * @file
 * @brief XWOS通用库：位图原子操作
 * @author
 * + 隐星魂 (Roy.Sun) <https://xwos.tech>
 * @copyright
 * + (c) 2015 隐星魂 (Roy.Sun) <https://xwos.tech>
 * > This Source Code Form is subject to the terms of the Mozilla Public
 * > License, v. 2.0. If a copy of the MPL was not distributed with this
 * > file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef __xwos_lib_xwbmpaop_h__
#define __xwos_lib_xwbmpaop_h__

#include <xwos/standard.h>
#include <xwos/lib/xwbop.h>
#include <xwos/lib/xwaop.h>
#include <xwos/ospl/soc/xwbmpaop.h>

/**
 * @brief XWOS BOPLIB：声明位图
 * @param name: (I) 符号名
 * @param bits: (I) 位图中的位数
 */
#define xwbmpaop_declare(name, bits)    xwbmp_a name[BITS_TO_XWBMP_T(bits)]

/**
 * @brief XWOS AOPLIB：测试位图中的某位是否被置位
 * @param bmp: (I) 位图的起始地址指针
 * @param n: (I) 被测试的位的序号
 */
bool xwbmpaop_t1i(xwbmp_a * bmp, xwsq_t idx);

/**
 * @brief XWOS AOPLIB：将位图中某位置1
 * @param bmp: (I) 位图的起始地址指针
 * @param n: (I) 被置1的位的序号
 */
void xwbmpaop_s1i(xwbmp_a * bmp, xwsq_t idx);

/**
 * @brief XWOS AOPLIB：将位图中某位清0
 * @param bmp: (I) 位图的起始地址指针
 * @param n: (I) 被清0的位的序号
 */
void xwbmpaop_c0i(xwbmp_a * bmp, xwsq_t idx);

/**
 * @brief XWOS AOPLIB：将位图中某位翻转
 * @param bmp: (I) 位图的起始地址指针
 * @param n: (I) 被翻转的位的序号
 */
void xwbmpaop_x1i(xwbmp_a * bmp, xwsq_t idx);

/**
 * @brief XWOS AOPLIB：测试位图中某位是否为0。如果是，就将它置1。
 * @param bmp: (I) 位图的起始地址指针
 * @param i: (I) 被测试位的序号
 * @retval XWOK: 没有错误
 * @retval -EACCES: 测试失败
 */
xwer_t xwbmpaop_t0i_then_s1i(xwbmp_a * bmp, xwsq_t idx);

/**
 * @brief XWOS AOPLIB：测试位图中某位是否为1。如果是，就将它清0。
 * @param bmp: (I) 位图的起始地址指针
 * @param n: (I) 被测试位的序号
 * @retval XWOK: 没有错误
 * @retval -EACCES: 测试失败
 */
xwer_t xwbmpaop_t1i_then_c0i(xwbmp_a * bmp, xwsq_t idx);

/**
 * @brief XWOS AOPLIB：从最高位起找到位图中第一个为1的位并将它清0
 * @param bmp: (I) 位图的起始地址指针
 * @param num: (I) 位图中总的位数
 * @retval >=0: 位的序号
 * @retval -ENODATA: 没有任何一个位为1
 */
xwssq_t xwbmpaop_fls_then_c0i(xwbmp_a * bmp, xwsz_t num);

/**
 * @brief XWOS AOPLIB：从最高位起找到位图中第一个为0的位并将它置1
 * @param bmp: (I) 位图的起始地址指针
 * @param num: (I) 位图中总的位数
 * @retval >=0: 位的序号
 * @retval -ENODATA: 没有任何一个位为0
 */
xwssq_t xwbmpaop_flz_then_s1i(xwbmp_a * bmp, xwsz_t num);

/**
 * @brief XWOS AOPLIB：从最低位起找到位图中第一个为1的位并将它清0
 * @param bmp: (I) 位图的起始地址指针
 * @param num: (I) 位图中总的位数
 * @retval >=0: 位的序号
 * @retval -ENODATA: 没有任何一个位为1
 */
xwssq_t xwbmpaop_ffs_then_c0i(xwbmp_a * bmp, xwsz_t num);

/**
 * @brief XWOS AOPLIB：从最低位起找到位图中第一个为0的位并将它置1
 * @param bmp: (I) 位图的起始地址指针
 * @param num: (I) 位图中总的位数
 * @retval >=0: 位的序号
 * @retval -ENODATA: 没有任何一个位为0
 */
xwssq_t xwbmpaop_ffz_then_s1i(xwbmp_a * bmp, xwsz_t num);

/**
 * @brief XWOS AOPLIB：将位图所有位设为1
 * @param bmp: (I) 位图的起始地址指针
 * @param num: (I) 位图中总的位数
 */
static __xwlib_inline
void xwbmpaop_s1all(xwbmp_a * bmp, xwsq_t num)
{
        xwsz_t n = BITS_TO_XWBMP_T(num);
        xwsz_t i;

        for (i = 0; i < n; i++) {
                xwaop_s1m(xwbmp, &bmp[i], (xwbmp_t)(~0), NULL, NULL);
        }
}

/**
 * @brief XWOS AOPLIB：清除位图中所有位
 * @param bmp: (I) 位图的起始地址指针
 * @param num: (I) 位图中总的位数
 */
static __xwlib_inline
void xwbmpaop_c0all(xwbmp_a * bmp, xwsq_t num)
{
        xwsz_t n = BITS_TO_XWBMP_T(num);
        xwsz_t i;

        for (i = 0; i < n; i++) {
                xwaop_c0m(xwbmp, &bmp[i], (xwbmp_t)(~0), NULL, NULL);
        }
}

#endif /* xwos/lib/xwbmpaop.h */
