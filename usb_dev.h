/**
 ******************************************************************************
 * @file    usb_dev.h
 * @brief   Общая часть библиотеки: обслуживание USB, прерывание, проверка
 *          возможности работать сетью и COM-портом одновременно. Сеть -
 *          usb_eth.h (USB_ETH_xxx), виртуальный COM-порт - usb_com.h (USB_COM_xxx).
 * @author  Mechanic
 * @date    03.10.2026
 * @version 2.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef USB_DEV_H
#define USB_DEV_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "usb_dev_opts.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Обслуживание USB, сети и COM-порта: вызывать в while(1) как можно
 *        чаще, без задержек. Все колбэки библиотеки (сети и COM) вызываются
 *        только отсюда. До USB_ETH_Init()/USB_COM_Init() и из прерывания
 *        ничего не делает.
 */
void USB_Process(void);

/**
 * @brief Обработчик прерывания USB. Вызывать из OTG_FS_IRQHandler() (или
 *        OTG_HS_IRQHandler(), см. USB_DEV_RHPORT) в секции USER CODE BEGIN 0 с
 *        последующим return - HAL_PCD_IRQHandler вызываться не должен.
 */
void USB_IRQHandler(void);

/**
 * @brief  Может ли USB-контроллер этого микроконтроллера работать сетью и
 *         COM-портом одновременно. Если нет - работает любое одно из них, а
 *         Init второго возвращает HAL_ERROR.
 * @return true - сеть и COM одновременно (STM32H7, F446, F412, F413, F7...);
 *         false - только одно (STM32F401, F405/F407 на OTG_FS, F411...)
 */
bool USB_IsCompositeSupported(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_DEV_H */
