/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sdio.h
  * @brief   This file contains all the function prototypes for
  *          the sdio.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * Copyright (c) 2022 xwos.tech.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SDIO_H__
#define __SDIO_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern SD_HandleTypeDef hsd;

/* USER CODE BEGIN Private defines */
#define MX_SD_SECTOR_SIZE 512U

/* USER CODE END Private defines */

void MX_SDIO_SD_Init(void);

/* USER CODE BEGIN Prototypes */
void MX_SDIO_SD_Construct(void);
void MX_SDIO_SD_Destruct(void);
void MX_SDIO_SD_DeInit(void);
void MX_SDIO_SD_ReInit(uint32_t clkdiv);
xwer_t MX_SDIO_SD_TrimClk(xwsq_t cnt);
xwer_t MX_SDIO_SD_GetState(void);
xwer_t MX_SDIO_SD_Read(uint8_t * data, uint32_t blkaddr,
                       uint32_t nrblk);
xwer_t MX_SDIO_SD_Write(uint8_t * data, uint32_t blkaddr,
                        uint32_t nrblk);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __SDIO_H__ */

