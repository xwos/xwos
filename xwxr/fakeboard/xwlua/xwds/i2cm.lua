--[[--------
XWLUA模块：I2C主机模式控制器

@classmod xwds.i2cm
]]


--[[--------
注册I2C主机模式控制器对象



在C代码中为XWLUA注册I2C主机模式控制器对象。

@within CAPI
@function xwds.i2cm.xwlua_i2cm_register

@param L **(lua_State *)** (**in**) 虚拟机指针（向哪个虚拟机注册）

@param name **(const char *)** (**in**) 在Lua语言中的符号名

@param i2cm **(struct xwds_i2cm *)** (**in**) I2C主机模式控制器对象

@usage
void xwlua_i2cm_register(lua_State * L, const char * name, struct xwds_i2cm * i2cm);

@usage
xwlua_i2cm_register(L, "i2cm2", &stm32cube_usart1_cb);
]]


--[[--------
删除I2C主机模式控制器对象



在C代码中删除I2C主机模式控制器对象。


@within CAPI
@function xwds.i2cm.xwlua_i2cm_unregister

@param L **(lua_State *)** (**in**) 虚拟机指针（从哪个虚拟机删除）

@param name **(const char *)** (**in**) 在Lua语言中的符号名

@usage
void xwlua_i2cm_unregister(lua_State * L, const char * name);
]]


--[[--------
元方法：__tostring



将I2C主机模式控制器对象格式化成字符串，可用于终端打印调试。

@within MetaMethods
@function xwds.i2cm:metatable.__tostring

@tparam userdata i2cm (**in**) C代码中注册的I2C主机模式控制器控制器
]]


--[[--------
接收数据



@tparam userdata i2cm (**in**) C代码中注册的I2C主机模式控制器控制器

@tparam number addr (**in**) I2C外设的地址

@tparam boolean start (**in**) 是否产生 **Start** 条件

@tparam boolean stop (**in**) 是否产生 **Stop** 条件

@tparam boolean rx (**in**) 方向是否为 **读**

@tparam number size (**in**) 本次传输的数据大小

@tparam string txd (**in**) 待发送的数据

@tparam number time (**optional**) (**in**) 期望的阻塞等待时间

@treturn[1] number `rx` 为 `false` 时的错误码<br>
　● **0** 没有错误<br>
　● **-EINTR** 等待被中断<br>
　● **-ENOTTHDCTX** 不在线程上下文中<br>
　● **-ETIMEDOUT** 超时，仅当存在可选参数 **time** 时才会出现此错误值

@treturn[2] number `rx` 为 `true` 时的错误码<br>
　● **0** 没有错误<br>
　● **-EINTR** 等待被中断<br>
　● **-ENOTTHDCTX** 不在线程上下文中<br>
　● **-ETIMEDOUT** 超时，仅当存在可选参数 **time** 时才会出现此错误值

@treturn[2] string `rx` 为 `true` 时的接收数据

@usage
-- 向EEPROM的0地址写入4字节数据，打包{地址0, 88, 87, 79, 83}
txd = string.pack("BBBBB", 0, 88, 87, 79, 83)
-- 写入地址和数据，最多等待2s
rc = i2c2m:xfer(0xA0, true, true, false, #txd, txd, xwtm.ft(xwtm.s(2)))

@usage
-- 从EEPROM的0地址读出4字节数据，打包{地址0}
txd = string.pack("B", 0)
-- 写入地址，产生Start，不产生Stop
rc = i2c2m:xfer(0xA0, true, false, false, #txd, txd)
-- 读出数据，重新产生Start，产生Stop
rc, rxd = i2c2m:xfer(0xA0, true, true, true, 4)
]]
function xfer(i2cm, addr, start, stop, rx, size, txd, time)
end
