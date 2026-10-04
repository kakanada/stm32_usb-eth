/**
 ******************************************************************************
 * @file    tusb_config.h
 * @brief   Настройки TinyUSB: устройство CDC-NCM (сеть) + CDC-ACM (COM), без ОС.
 *          Серия STM32 определяется автоматически - править не нужно.
 * @author  Mechanic
 * @date    03.10.2026
 * @version 2.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#include "main.h"   /* объявляет серию STM32 (STM32H7, STM32F4, ...) */
#include "usb_dev_opts.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Серия микроконтроллера --- */
#ifndef CFG_TUSB_MCU
#if defined(STM32H7)
#define CFG_TUSB_MCU OPT_MCU_STM32H7
#elif defined(STM32F7)
#define CFG_TUSB_MCU OPT_MCU_STM32F7
#elif defined(STM32F4)
#define CFG_TUSB_MCU OPT_MCU_STM32F4
#elif defined(STM32F2)
#define CFG_TUSB_MCU OPT_MCU_STM32F2
#elif defined(STM32F1)
#define CFG_TUSB_MCU OPT_MCU_STM32F1
#elif defined(STM32F0)
#define CFG_TUSB_MCU OPT_MCU_STM32F0
#elif defined(STM32G4)
#define CFG_TUSB_MCU OPT_MCU_STM32G4
#elif defined(STM32G0)
#define CFG_TUSB_MCU OPT_MCU_STM32G0
#elif defined(STM32L4)
#define CFG_TUSB_MCU OPT_MCU_STM32L4
#elif defined(STM32U5)
#define CFG_TUSB_MCU OPT_MCU_STM32U5
#elif defined(STM32H5)
#define CFG_TUSB_MCU OPT_MCU_STM32H5
#else
#error "usb_eth: серия STM32 не распознана - задайте CFG_TUSB_MCU глобальным define"
#endif
#endif

#define CFG_TUSB_OS          OPT_OS_NONE
#define CFG_TUSB_DEBUG       0
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN   __attribute__((aligned(4)))

/* --- Только устройство, Full Speed --- */
#define CFG_TUD_ENABLED        1
#define CFG_TUD_MAX_SPEED      OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64

/* --- Сетевой класс CDC-NCM: драйвер встроен в Windows 10/11, Linux, macOS --- */
#define CFG_TUD_ECM_RNDIS            0
#define CFG_TUD_NCM                  1
#define CFG_TUD_NET_MTU              1514   /* размер кадра Ethernet (14 + 1500) */
#define CFG_TUD_NCM_IN_NTB_MAX_SIZE  2048   /* минимум по CDC-NCM 1.0, табл. 6-4 */
#define CFG_TUD_NCM_OUT_NTB_MAX_SIZE 2048
#define CFG_TUD_NCM_OUT_NTB_N        1
#define CFG_TUD_NCM_IN_NTB_N         1

/* --- Виртуальный COM-порт CDC-ACM: драйвер встроен в Windows 10/11, Linux, macOS --- */
#define CFG_TUD_CDC                  1
#define CFG_TUD_CDC_RX_BUFSIZE       USB_COM_RX_BUF_SIZE
#define CFG_TUD_CDC_TX_BUFSIZE       USB_COM_TX_BUF_SIZE
#define CFG_TUD_CDC_EP_BUFSIZE       64
/* Порт на ПК не открыт - в буфере остаются последние данные, а не первые */
#define CFG_TUD_CDC_TX_OVERWRITABLE_IF_NOT_CONNECTED 1

/* --- Остальные классы не используются --- */
#define CFG_TUD_MSC          0
#define CFG_TUD_HID          0
#define CFG_TUD_MIDI         0
#define CFG_TUD_VENDOR       0
#define CFG_TUD_VIDEO        0
#define CFG_TUD_AUDIO        0
#define CFG_TUD_DFU          0
#define CFG_TUD_DFU_RUNTIME  0
#define CFG_TUD_USBTMC       0
#define CFG_TUD_PRINTER      0
#define CFG_TUD_MTP          0

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
