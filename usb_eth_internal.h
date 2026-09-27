/**
 ******************************************************************************
 * @file    usb_eth_internal.h
 * @brief   Внутренние объявления usb_eth, общие для её .c-файлов. Не является
 *          частью публичного API.
 * @author  Mechanic
 * @date    27.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef USB_ETH_INTERNAL_H
#define USB_ETH_INTERNAL_H

#include "usb_eth.h"

#if USB_ETH_LOG_ENABLE
#include "logger.h"
/* Требует LOGGER_Init() до USB_ETH_Init() - см. README stm32_logger. */
#define USB_ETH_LOG(code, source_id, value) \
    LOGGER_Log((uint16_t)(code), (uint16_t)(source_id), (int32_t)(value))
#else
#define USB_ETH_LOG(code, source_id, value) ((void)0)
#endif

/**
 * @brief  Выполняется ли код сейчас в обработчике прерывания.
 * @return true - в прерывании (вызов API запрещён)
 */
static inline bool usb_eth_in_isr(void)
{
    return (__get_IPSR() != 0U);
}

/**
 * @brief  Инициализирована ли библиотека (USB_ETH_Init() прошёл успешно).
 * @return true - да
 */
bool usb_eth_is_ready(void);

/**
 * @brief Аварийно закрывает все TCP-соединения с причиной LINK_LOST
 *        (вызывается ядром при потере сети).
 */
void usb_eth_sock_link_lost(void);

/**
 * @brief  Генератор псевдослучайных чисел для lwIP (LWIP_RAND, см. cc.h).
 * @return 32-битное псевдослучайное число
 */
uint32_t usb_eth_rand(void);

#endif /* USB_ETH_INTERNAL_H */
