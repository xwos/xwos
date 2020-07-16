/**
 * @file
 * @brief xwmd设备栈：I2C外设
 * @author
 * + 隐星魂 (Roy.Sun) <https://xwos.tech>
 * @copyright
 * + (c) 2015 隐星魂 (Roy.Sun) <https://xwos.tech>
 * > This Source Code Form is subject to the terms of the Mozilla Public
 * > License, v. 2.0. If a copy of the MPL was not distributed with this
 * > file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef __xwmd_ds_i2c_perpheral_h__
#define __xwmd_ds_i2c_perpheral_h__

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********      include      ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
#include <xwmd/ds/standard.h>
#include <xwos/lib/xwbop.h>
#include <xwmd/ds/device.h>
#include <xwmd/ds/i2c/common.h>
#include <xwmd/ds/i2c/master.h>

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********       macros      ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********       types       ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
struct xwds_i2cp;

/**
 * @brief BSP中需要提供的I2C外设驱动函数表
 */
struct xwds_i2cp_driver {
        struct xwds_driver base; /**< C语言面向对象：继承struct xwds_driver */
        xwer_t (* ioctl)(struct xwds_i2cp *, xwsq_t, va_list); /**< 输入、输出与控制 */
};

/**
 * @brief I2C外设
 */
struct xwds_i2cp {
        struct xwds_device dev; /**< 继承struct xwds_device */

        /* attributes */
        struct xwds_i2cm * bus; /**< I2C总线 */
        xwu16_t addr; /**< 外设地址 */
};

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ********         function prototypes         ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
/******** ******** ******** constructor & destructor ******** ******** ********/
__xwds_api
void xwds_i2cp_construct(struct xwds_i2cp * i2cp);

__xwds_api
void xwds_i2cp_destruct(struct xwds_i2cp * i2cp);

/******** ******** ******** APIs ******** ******** ********/
/**
 * @brief XWDS API：I2C外设输入、输出、控制
 * @param i2cp: (I) I2C外设对象指针
 * @param cmd: (I) 命令
 * @param ...: (I) 参数表
 * @return 错误码
 * @retval XWOK: 没有错误
 * @retval -ENOSYS: 无效CMD
 * @retval -EFAULT: 无效指针
 * @note
 * - 同步/异步：依赖于CMD的实现
 * - 上下文：依赖于CMD的实现
 * - 重入性：依赖于CMD的实现
 */
__xwds_api
xwer_t xwds_i2cp_ioctl(struct xwds_i2cp * i2cp, xwsq_t cmd, ...);

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ********   inline function implementations   ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
/**
 * @brief 增加对象的引用计数
 * @param i2cp: (I) I2C外设对象指针
 * @return 错误码
 * @retval @ref xwds_device_grab()
 */
static __xwds_inline
xwer_t xwds_i2cp_grab(struct xwds_i2cp * i2cp)
{
        return xwds_device_grab(&i2cp->dev);
}

/**
 * @brief 减少对象的引用计数
 * @param i2cp: (I) I2C外设对象指针
 * @return 错误码
 * @retval @ref xwds_device_put()
 */
static __xwds_inline
xwer_t xwds_i2cp_put(struct xwds_i2cp * i2cp)
{
        return xwds_device_put(&i2cp->dev);
}

/**
 * @brief 增加设备运行状态计数器
 * @param i2cp: (I) I2C外设对象指针
 * @return 错误码
 * @retval @ref xwds_device_request()
 */
static __xwds_inline
xwer_t xwds_i2cp_request(struct xwds_i2cp * i2cp)
{
        return xwds_device_request(&i2cp->dev);
}

/**
 * @brief 减少设备运行状态计数器
 * @param i2cp: (I) I2C外设对象指针
 * @return 错误码
 * @retval @ref xwds_device_release()
 */
static __xwds_inline
xwer_t xwds_i2cp_release(struct xwds_i2cp * i2cp)
{
        return xwds_device_release(&i2cp->dev);
}

#endif /* xwmd/ds/i2c/perpheral.h */
