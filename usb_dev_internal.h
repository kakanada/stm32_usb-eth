/**
 ******************************************************************************
 * @file    usb_dev_internal.h
 * @brief   Внутренние объявления общей части (usb_dev) для usb_eth, usb_com и
 *          дескрипторов. Не является частью публичного API.
 * @author  Mechanic
 * @date    03.10.2026
 * @version 2.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef USB_DEV_INTERNAL_H
#define USB_DEV_INTERNAL_H

#include "usb_dev.h"

#if USB_DEV_LOG_ENABLE
#include "logger.h"
/* Требует LOGGER_Init() до USB_ETH_Init()/USB_COM_Init() - см. README stm32_logger. */
#define USB_DEV_LOG(code, source_id, value) \
    LOGGER_Log((uint16_t)(code), (uint16_t)(source_id), (int32_t)(value))
#else
#define USB_DEV_LOG(code, source_id, value) ((void)0)
#endif

/** Устройства (функции) в составе USB-конфигурации - битовая маска. */
#define USB_DEV_FUNC_ETH  0x01U
#define USB_DEV_FUNC_COM  0x02U

/**
 * @brief  Выполняется ли код сейчас в обработчике прерывания.
 * @return true - в прерывании (вызов API запрещён)
 */
static inline bool usb_dev_in_isr(void)
{
    return (__get_IPSR() != 0U);
}

/**
 * @brief  Включает/выключает устройство в составе USB. Сам запускает TinyUSB
 *         при первом включении. Смена набора применяется в USB_Process(); если
 *         ПК уже видел плату, она переподключается.
 * @param  func USB_DEV_FUNC_ETH или USB_DEV_FUNC_COM
 * @param  on   true - включить, false - выключить
 * @return HAL_OK; HAL_ERROR - контроллер не тянет оба устройства сразу или
 *         не запустился TinyUSB
 */
HAL_StatusTypeDef usb_dev_set_func(uint8_t func, bool on);

/**
 * @brief  Подключено ли устройство: ПК сконфигурировал USB и func входит в
 *         текущую конфигурацию.
 * @param  func USB_DEV_FUNC_ETH или USB_DEV_FUNC_COM
 * @return true - можно обмениваться данными
 */
bool usb_dev_func_mounted(uint8_t func);

/**
 * @brief  Собирает дескрипторы под набор устройств (вызывается, пока плата
 *         отключена от ПК).
 * @param  funcs маска USB_DEV_FUNC_*
 */
void usb_dev_desc_build(uint8_t funcs);

/**
 * @brief Точки входа сети, вызываемые ядром. Ядро обращается к сети только
 *        через них, а регистрирует их USB_ETH_Init(): если приложение не
 *        вызывает USB_ETH_Init(), линковщик (--gc-sections) не включает в
 *        прошивку lwIP и его буферы (~45 КБ ОЗУ) - важно для COM без сети.
 */
typedef struct
{
    void (*process)(void);                                /**< обслуживание из USB_Process() */
    bool (*recv)(const uint8_t *frame, uint16_t size);    /**< кадр от ПК (tud_network_recv_cb) */
} usb_dev_eth_hooks_t;

/**
 * @brief Регистрирует точки входа сети (из USB_ETH_Init()).
 * @param hooks точки входа (статический объект)
 */
void usb_dev_set_eth_hooks(const usb_dev_eth_hooks_t *hooks);

/** @brief Обслуживание COM из USB_Process() (после tud_task()). */
void usb_com_process(void);

#endif /* USB_DEV_INTERNAL_H */
