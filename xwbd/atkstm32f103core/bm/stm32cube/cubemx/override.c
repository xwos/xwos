/**
 * @file
 * @brief STM32CUBE：重写某些override属性的函数
 * @author
 * + 隐星魂 (Roy.Sun) <www.starsoul.tech>
 * @copyright
 * + (c) 2015 隐星魂 (Roy.Sun) <www.starsoul.tech>
 * > Licensed under the Apache License, Version 2.0 (the "License");
 * > you may not use this file except in compliance with the License.
 * > You may obtain a copy of the License at
 * >
 * >         http://www.apache.org/licenses/LICENSE-2.0
 * >
 * > Unless required by applicable law or agreed to in writing, software
 * > distributed under the License is distributed on an "AS IS" BASIS,
 * > WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * > See the License for the specific language governing permissions and
 * > limitations under the License.
 */

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********      include      ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
#include <bm/stm32cube/standard.h>
#include <arch_systick.h>
#include <bm/stm32cube/cubemx/Core/Inc/main.h>

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********      macros       ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ******** ********       .data       ******** ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
extern uint32_t uwTickPrio;

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ********         function prototypes         ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/

/******** ******** ******** ******** ******** ******** ******** ********
 ******** ********      function implementations       ******** ********
 ******** ******** ******** ******** ******** ******** ******** ********/
void stm32cube_override_placeholder(void)
{
        /* ld连接静态库(.a)时，--gc-sections是以.c文件为单位，若文件中
           全是override的函数，会被ld认为.c文件中所有符号都没有被使用而被删掉，
           因此写一个占位函数，并在init中调用。 */
}

void HAL_MspInit(void)
{
        /* bm/stm32cube/cubemx/Core/Src/stm32f0xx_hal_msp.c中只有一个override函数，
           静态连接时整个文件会被GC掉，因此需要重新在此文件中定义一次。*/
        __HAL_RCC_SYSCFG_CLK_ENABLE();
        __HAL_RCC_PWR_CLK_ENABLE();

        LL_PWR_EnableBkUpAccess();
        LL_DBGMCU_EnableDBGStopMode();
        LL_DBGMCU_EnableDBGStandbyMode();
}

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
        HAL_StatusTypeDef ret;

        /* Configure the SysTick IRQ priority */
        if (TickPriority < (1UL << __NVIC_PRIO_BITS)) {
                uwTickPrio = ARCH_IRQ_TICK_PRIO;
                ret = HAL_OK;
        } else {
                ret = HAL_ERROR;
        }
        return ret;
}
