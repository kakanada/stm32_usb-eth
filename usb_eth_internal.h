/**
 ******************************************************************************
 * @file    usb_eth_internal.h
 * @brief   Внутренние объявления usb_eth, общие для её .c-файлов. Не является
 *          частью публичного API.
 * @author  Mechanic
 * @date    03.10.2026
 * @version 2.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef USB_ETH_INTERNAL_H
#define USB_ETH_INTERNAL_H

#include "usb_eth.h"
#include "usb_dev_internal.h"

/* События сети - через общий переключатель USB_DEV_LOG_ENABLE (usb_dev_opts.h). */
#define USB_ETH_LOG(code, source_id, value) USB_DEV_LOG((code), (source_id), (value))

/**
 * @brief  Выполняется ли код сейчас в обработчике прерывания.
 * @return true - в прерывании (вызов API запрещён)
 */
static inline bool usb_eth_in_isr(void)
{
    return usb_dev_in_isr();
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
 * @brief Закрывает все соединения (LINK_LOST), удаляет TCP-серверы и UDP-сокеты
 *        и очищает пулы (вызывается из USB_ETH_DeInit()).
 */
void usb_eth_sock_deinit(void);

/**
 * @brief  Генератор псевдослучайных чисел для lwIP (LWIP_RAND, см. cc.h).
 * @return 32-битное псевдослучайное число
 */
uint32_t usb_eth_rand(void);

#endif /* USB_ETH_INTERNAL_H */
