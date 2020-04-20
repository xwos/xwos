/**
  ******************************************************************************
  * File Name          : USART.c
  * Description        : This file provides code for the configuration
  *                      of the USART instances.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "usart.h"

/* USER CODE BEGIN 0 */
#include "tim.h"
#include "sys.h"

#include <xwos/standard.h>
#include <xwos/osal/lock/spinlock.h>
#include <xwos/osal/sync/condition.h>
#include <xwmd/ds/soc/dma.h>
#include <xwmd/ds/uart/dma.h>
#include <bm/stm32cube/xwds/stm32cube.h>

struct stm32cube_dmauartc_driver_data {
  struct {
    struct xwosal_cdt cdt;
    struct xwosal_splk splk;
    xwer_t rc;
  } tx;
};

#define MX_USART1_RSC_DMA_IDX_RX        0
#define MX_USART1_RSC_DMA_IDX_TX        1

static
xwer_t stm32cube_dmauartc1_drv_start(struct xwds_device * dev);

static
xwer_t stm32cube_dmauartc1_drv_stop(struct xwds_device * dev);

#if defined(XWMDCFG_ds_LPM) && (1 == XWMDCFG_ds_LPM)
static
xwer_t stm32cube_dmauartc1_drv_suspend(struct xwds_device * dev);

static
xwer_t stm32cube_dmauartc1_drv_resume(struct xwds_device * dev);
#endif /* XWMDCFG_ds_LPM */

static
xwer_t stm32cube_dmauartc1_drv_cfg(struct xwds_dmauartc * dmauartc,
                                   const struct xwds_uart_cfg * cfg);

static
void stm32cube_dmauartc1_tx_cb(struct xwds_soc * soc, xwid_t ch, xwu32_t rc,
                               xwds_dma_cbarg_t arg);

static
xwer_t stm32cube_dmauartc1_drv_tx(struct xwds_dmauartc * dmauartc,
                                  const xwu8_t * data, xwsz_t size,
                                  xwtm_t * xwtm);

static
void stm32cube_dmauartc1_rxdma_cb(struct xwds_soc * soc, xwid_t ch, xwu32_t rc,
                                  xwds_dma_cbarg_t arg);

const struct xwds_dmauartc_driver stm32cube_dmauartc1_drv = {
  .base = {
    .name = "stm32cube.dmauart.1",
    .probe = NULL,
    .remove = NULL,
    .start = stm32cube_dmauartc1_drv_start,
    .stop = stm32cube_dmauartc1_drv_stop,
#if defined(XWMDCFG_ds_LPM) && (1 == XWMDCFG_ds_LPM)
    .suspend = stm32cube_dmauartc1_drv_suspend,
    .resume =  stm32cube_dmauartc1_drv_resume,
#endif /* XWMDCFG_ds_LPM */
  },
  .cfg = stm32cube_dmauartc1_drv_cfg,
  .tx = stm32cube_dmauartc1_drv_tx,
};

struct xwds_resource_dma stm32cube_dmauartc1_rsc_dma[2] = {
  [MX_USART1_RSC_DMA_IDX_RX] = {
    .soc = &stm32cube_soc_cb,
    .ch = SODS_DMA_CH(10),
    .xwccfg = NULL,
    .description = "rsc.dma.rx.usart.1",
  },
  [MX_USART1_RSC_DMA_IDX_TX] = {
    .soc = &stm32cube_soc_cb,
    .ch = SODS_DMA_CH(15),
    .xwccfg = NULL,
    .description = "rsc.dma.tx.usart.1",
  },
};

struct stm32cube_dmauartc_driver_data stm32cube_dmauartc1_driver_data;

struct xwds_dmauartc stm32cube_dmauartc1_cb = {
  /* attributes */
  .dev = {
    .name = "stm32cube.usart.dmauart",
    .id = 1,
    .resources = NULL,
    .drv = xwds_static_cast(struct xwds_driver *, &stm32cube_dmauartc1_drv),
    .data = (void *)&stm32cube_dmauartc1_driver_data,
  },
  .cfg = NULL,
  .rxdmarsc = &stm32cube_dmauartc1_rsc_dma[MX_USART1_RSC_DMA_IDX_RX],
  .txdmarsc = &stm32cube_dmauartc1_rsc_dma[MX_USART1_RSC_DMA_IDX_TX],
};

/* USER CODE END 0 */

/* USART1 init function */

void MX_USART1_UART_Init(void)
{
  LL_USART_InitTypeDef USART_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
  
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
  /**USART1 GPIO Configuration  
  PA9   ------> USART1_TX
  PA10   ------> USART1_RX 
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_9|LL_GPIO_PIN_10;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_7;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USART1 DMA Init */
  
  /* USART1_TX Init */
  LL_DMA_SetChannelSelection(DMA2, LL_DMA_STREAM_7, LL_DMA_CHANNEL_4);

  LL_DMA_SetDataTransferDirection(DMA2, LL_DMA_STREAM_7, LL_DMA_DIRECTION_MEMORY_TO_PERIPH);

  LL_DMA_SetStreamPriorityLevel(DMA2, LL_DMA_STREAM_7, LL_DMA_PRIORITY_HIGH);

  LL_DMA_SetMode(DMA2, LL_DMA_STREAM_7, LL_DMA_MODE_NORMAL);

  LL_DMA_SetPeriphIncMode(DMA2, LL_DMA_STREAM_7, LL_DMA_PERIPH_NOINCREMENT);

  LL_DMA_SetMemoryIncMode(DMA2, LL_DMA_STREAM_7, LL_DMA_MEMORY_INCREMENT);

  LL_DMA_SetPeriphSize(DMA2, LL_DMA_STREAM_7, LL_DMA_PDATAALIGN_BYTE);

  LL_DMA_SetMemorySize(DMA2, LL_DMA_STREAM_7, LL_DMA_MDATAALIGN_BYTE);

  LL_DMA_DisableFifoMode(DMA2, LL_DMA_STREAM_7);

  /* USART1_RX Init */
  LL_DMA_SetChannelSelection(DMA2, LL_DMA_STREAM_2, LL_DMA_CHANNEL_4);

  LL_DMA_SetDataTransferDirection(DMA2, LL_DMA_STREAM_2, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);

  LL_DMA_SetStreamPriorityLevel(DMA2, LL_DMA_STREAM_2, LL_DMA_PRIORITY_HIGH);

  LL_DMA_SetMode(DMA2, LL_DMA_STREAM_2, LL_DMA_MODE_CIRCULAR);

  LL_DMA_SetPeriphIncMode(DMA2, LL_DMA_STREAM_2, LL_DMA_PERIPH_NOINCREMENT);

  LL_DMA_SetMemoryIncMode(DMA2, LL_DMA_STREAM_2, LL_DMA_MEMORY_INCREMENT);

  LL_DMA_SetPeriphSize(DMA2, LL_DMA_STREAM_2, LL_DMA_PDATAALIGN_BYTE);

  LL_DMA_SetMemorySize(DMA2, LL_DMA_STREAM_2, LL_DMA_MDATAALIGN_BYTE);

  LL_DMA_DisableFifoMode(DMA2, LL_DMA_STREAM_2);

  /* USART1 interrupt Init */
  NVIC_SetPriority(USART1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),0, 0));
  NVIC_EnableIRQ(USART1_IRQn);

  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART1, &USART_InitStruct);
  LL_USART_ConfigAsyncMode(USART1);
  LL_USART_Enable(USART1);

}

/* USER CODE BEGIN 1 */
void MX_USART1_UART_Deinit(void)
{
  LL_USART_Disable(USART1);
  NVIC_DisableIRQ(USART1_IRQn);
  LL_APB2_GRP1_DisableClock(LL_APB2_GRP1_PERIPH_USART1);
}

static
void MX_USART1_UART_Setup_RXDMA(xwu8_t * mem, size_t size)
{
  LL_DMA_EnableIT_HT(DMA2, LL_DMA_STREAM_2);
  LL_DMA_EnableIT_TE(DMA2, LL_DMA_STREAM_2);
  LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_2);
  LL_DMA_DisableDoubleBufferMode(DMA2, LL_DMA_STREAM_2);
  LL_DMA_SetCurrentTargetMem(DMA2, LL_DMA_STREAM_2, LL_DMA_CURRENTTARGETMEM0);
  LL_DMA_ConfigAddresses(DMA2, LL_DMA_STREAM_2,
                         LL_USART_DMA_GetRegAddr(USART1),
                         (uint32_t)mem,
                         LL_DMA_GetDataTransferDirection(DMA2, LL_DMA_STREAM_2));
  LL_DMA_SetDataLength(DMA2, LL_DMA_STREAM_2, size);
  LL_USART_EnableDMAReq_RX(USART1);
}

static
void MX_USART1_UART_Setup_TXDMA(const xwu8_t * mem, size_t size)
{
  LL_DMA_EnableIT_TE(DMA2, LL_DMA_STREAM_7);
  LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_7);
  LL_DMA_DisableDoubleBufferMode(DMA2, LL_DMA_STREAM_7);
  LL_DMA_SetCurrentTargetMem(DMA2, LL_DMA_STREAM_7, LL_DMA_CURRENTTARGETMEM0);
  LL_DMA_ConfigAddresses(DMA2, LL_DMA_STREAM_7,
                         (uint32_t)mem,
                         LL_USART_DMA_GetRegAddr(USART1),
                         LL_DMA_GetDataTransferDirection(DMA2, LL_DMA_STREAM_7));
  LL_DMA_SetDataLength(DMA2, LL_DMA_STREAM_7, size);
  LL_USART_EnableDMAReq_TX(USART1);
}

/** xwds driver code **/
static
xwer_t stm32cube_dmauartc1_drv_start(struct xwds_device * dev)
{
  struct xwds_dmauartc * dmauartc;
  const struct xwds_resource_dma * rxdmarsc;
  const struct xwds_resource_dma * txdmarsc;
  struct stm32cube_dmauartc_driver_data * drvdata;
  xwer_t rc;

  dmauartc = xwds_static_cast(struct xwds_dmauartc *, dev);
  drvdata = dmauartc->dev.data;
  rxdmarsc = dmauartc->rxdmarsc;
  txdmarsc = dmauartc->txdmarsc;

  xwosal_splk_init(&drvdata->tx.splk);
  xwosal_cdt_init(&drvdata->tx.cdt);
  drvdata->tx.rc = -ECANCELED;

  rc = xwds_dma_req(rxdmarsc->soc, rxdmarsc->ch);
  if (rc < 0) {
    goto err_rxdma_req;
  }
  rc = xwds_dma_req(txdmarsc->soc, txdmarsc->ch);
  if (rc < 0) {
    goto err_txdma_req;
  }

  MX_USART1_UART_Init();
  MX_USART1_UART_Setup_RXDMA(dmauartc->rxq.mem, sizeof(dmauartc->rxq.mem));
  rc = xwds_dma_cfg(rxdmarsc->soc, rxdmarsc->ch, NULL,
                    stm32cube_dmauartc1_rxdma_cb, dmauartc);
  if (rc < 0) {
    goto err_dma_cfg;
  }
  rc = xwds_dma_enable(rxdmarsc->soc, rxdmarsc->ch);
  if (rc < 0) {
    goto err_dma_enable;
  }

  MX_TIM9_Start();
  return OK;

err_dma_enable:
err_dma_cfg:
  xwds_dma_rls(txdmarsc->soc, txdmarsc->ch);
err_txdma_req:
  xwds_dma_rls(rxdmarsc->soc, rxdmarsc->ch);
err_rxdma_req:
  xwosal_cdt_destroy(&drvdata->tx.cdt);
  return rc;
}

static
xwer_t stm32cube_dmauartc1_drv_stop(struct xwds_device * dev)
{
  struct xwds_dmauartc * dmauartc;
  const struct xwds_resource_dma * rxdmarsc;
  const struct xwds_resource_dma * txdmarsc;
  struct stm32cube_dmauartc_driver_data * drvdata;

  dmauartc = xwds_static_cast(struct xwds_dmauartc *, dev);
  drvdata = dmauartc->dev.data;
  rxdmarsc = dmauartc->rxdmarsc;
  txdmarsc = dmauartc->txdmarsc;

  MX_TIM9_Stop();

  xwds_dma_disable(rxdmarsc->soc, rxdmarsc->ch);
  xwds_dma_disable(txdmarsc->soc, txdmarsc->ch);

  MX_USART1_UART_Deinit();

  xwds_dma_rls(rxdmarsc->soc, rxdmarsc->ch);
  xwds_dma_rls(txdmarsc->soc, txdmarsc->ch);

  xwosal_cdt_destroy(&drvdata->tx.cdt);
  return OK;
}

#if defined(XWMDCFG_ds_LPM) && (1 == XWMDCFG_ds_LPM)
static
xwer_t stm32cube_dmauartc1_drv_resume(struct xwds_device * dev)
{
  return stm32cube_dmauartc1_drv_start(dev);
}

static
xwer_t stm32cube_dmauartc1_drv_suspend(struct xwds_device * dev)
{
  return stm32cube_dmauartc1_drv_suspend(dev);
}
#endif /* XWMDCFG_ds_LPM */

static
xwer_t stm32cube_dmauartc1_drv_cfg(struct xwds_dmauartc * dmauartc,
                                   const struct xwds_uart_cfg * cfg)
{
  UNUSED(dmauartc);
  UNUSED(cfg);
  return -ENOSYS;
}

static
void stm32cube_dmauartc1_tx_cb(struct xwds_soc * soc,
                               xwid_t ch, xwu32_t rc,
                               xwds_dma_cbarg_t arg)
{
  struct xwds_dmauartc * dmauartc;
  struct stm32cube_dmauartc_driver_data * drvdata;
  xwreg_t flag;

  UNUSED(soc);
  UNUSED(ch);

  dmauartc = xwds_static_cast(struct xwds_dmauartc *, arg);
  drvdata = dmauartc->dev.data;
  xwosal_splk_lock_cpuirqsv(&drvdata->tx.splk, &flag);
  if (-ECANCELED != drvdata->tx.rc) {
    if (STM32CUBE_SOC_DMA_RC_TC & rc) {
      drvdata->tx.rc = OK;
    } else {
      drvdata->tx.rc = -EIO;
    }
  } else {
  }
  xwosal_splk_unlock_cpuirqrs(&drvdata->tx.splk, flag);
  xwosal_cdt_unicast(xwosal_cdt_get_id(&drvdata->tx.cdt));
}

static
xwer_t stm32cube_dmauartc1_drv_tx(struct xwds_dmauartc * dmauartc,
                                  const xwu8_t * data, xwsz_t size,
                                  xwtm_t * xwtm)
{
  const struct xwds_resource_dma * txdmarsc;
  struct stm32cube_dmauartc_driver_data * drvdata;
  xwreg_t flag;
  union xwlk_ulock ulk;
  xwsq_t lkst;
  xwer_t rc;

  txdmarsc = dmauartc->txdmarsc;
  drvdata = dmauartc->dev.data;

  MX_USART1_UART_Setup_TXDMA(data, size);
  rc = xwds_dma_cfg(txdmarsc->soc, txdmarsc->ch, NULL,
                    stm32cube_dmauartc1_tx_cb, dmauartc);
  if (rc < 0) {
    goto err_dma_cfg;
  }

  ulk.osal.splk = &drvdata->tx.splk;
  xwosal_splk_lock_cpuirqsv(&drvdata->tx.splk, &flag);
  drvdata->tx.rc = -EINPROGRESS;
  rc = xwds_dma_enable(txdmarsc->soc, txdmarsc->ch);
  if (OK == rc) {
    rc = xwosal_cdt_timedwait(xwosal_cdt_get_id(&drvdata->tx.cdt),
                              ulk,
                              XWLK_TYPE_SPLK_CPUIRQ,
                              NULL, XWOS_UNUSED_ARGUMENT,
                              xwtm, &lkst);
    if (OK == rc) {
      if (XWLK_STATE_UNLOCKED == lkst) {
        xwosal_splk_lock_cpuirq(&drvdata->tx.splk);
      }
      rc = drvdata->tx.rc;
    } else {
      if (XWLK_STATE_UNLOCKED == lkst) {
        xwosal_splk_lock_cpuirq(&drvdata->tx.splk);
      }
      drvdata->tx.rc = -ECANCELED;
    }
  } else {
    drvdata->tx.rc = -ECANCELED;
  }
  xwosal_splk_unlock_cpuirqrs(&drvdata->tx.splk, flag);

err_dma_cfg:
  return rc;
}

static
void stm32cube_dmauartc1_rxdma_cb(struct xwds_soc * soc, xwid_t ch, xwu32_t rc,
                                  xwds_dma_cbarg_t arg)
{
  struct xwds_dmauartc * dmauartc;

  XWOS_UNUSED(soc);
  XWOS_UNUSED(ch);

  dmauartc = arg;
  MX_TIM9_Stop();
  if (STM32CUBE_SOC_DMA_RC_TC & rc) {
    xwds_dmauartc_lib_rxq_pub(dmauartc, 0);
  } else if (STM32CUBE_SOC_DMA_RC_HT & rc) {
    xwds_dmauartc_lib_rxq_pub(dmauartc, SODS_DMAUART_RXQ_SIZE);
  } else {
  }
  MX_TIM9_Start();
}

void stm32cube_dmauartc1_timer_isr(void)
{
  xwirq_t irqn;
  struct soc_irq_data irqdata;
  struct xwds_dmauartc * dmauartc;
  xwsq_t tail;

  xwos_irq_get_id(&irqn);
  xwos_irq_get_data(irqn, &irqdata);
  dmauartc = irqdata.data;

  LL_TIM_ClearFlag_UPDATE(TIM9);
  tail = sizeof(dmauartc->rxq.mem) -
         (xwsq_t)LL_DMA_GetDataLength(DMA2, LL_DMA_STREAM_2);
  if (sizeof(dmauartc->rxq.mem) == tail) {
    xwds_dmauartc_lib_rxq_pub(dmauartc, 0);
  } else {
    xwds_dmauartc_lib_rxq_pub(dmauartc, tail);
  }
}

void stm32cube_dmauartc1_isr(void)
{
  /*
  xwirq_t irqn;
  struct soc_irq_data irqdata;
  struct xwds_dmauartc * dmauartc;

  irqn = xwos_irq_get_id();
  xwos_irq_get_data(irqn, &irqdata);
  dmauartc = irqdata.data;
  */
}

/* USER CODE END 1 */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
