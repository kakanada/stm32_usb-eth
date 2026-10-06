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
/**
 * @brief Запись в stm32_logger (требует LOGGER_Init() до USB_ETH_Init()/
 *        USB_COM_Init()). Повторный вход запрещён: запись, сделанная изнутри
 *        другой записи (вывод лога идёт в этот же COM/сеть) или из прерывания
 *        во время записи, не выполняется, а считается подавленной.
 * @param code      код
 * @param source_id источник
 * @param value     значение
 * @param limited   true - не чаще USB_DEV_LOG_MIN_INTERVAL_MS для этого кода,
 *                  пропущенные записи считаются и выводятся кодом
 *                  USB_DEV_LOG_CODE_LOG_SUPPRESSED
 */
void usb_dev_log(uint16_t code, uint16_t source_id, int32_t value, bool limited);

/** Событие: записывается всегда (редкие события - подключение, Init...). */
#define USB_DEV_LOG(code, source_id, value) \
    usb_dev_log((uint16_t)(code), (uint16_t)(source_id), (int32_t)(value), false)
/** Ошибка: с ограничением частоты (может повторяться лавиной). */
#define USB_DEV_LOG_ERR(code, source_id, value) \
    usb_dev_log((uint16_t)(code), (uint16_t)(source_id), (int32_t)(value), true)
#else
#define USB_DEV_LOG(code, source_id, value)     ((void)0)
#define USB_DEV_LOG_ERR(code, source_id, value) ((void)0)
#endif

/* Источник (src) для кодов API_ERROR: какая функция отказала. */
#define USB_DEV_API_PROCESS          1U   /**< USB_Process */
#define USB_DEV_API_COM_INIT         2U   /**< USB_COM_Init */
#define USB_DEV_API_COM_DEINIT       3U   /**< USB_COM_DeInit */
#define USB_DEV_API_COM_TRANSMIT     4U   /**< USB_COM_Transmit / TransmitString */
#define USB_DEV_API_COM_READ         5U   /**< USB_COM_Read / Available / GetFreeSpace */
#define USB_DEV_API_ETH_INIT         16U  /**< USB_ETH_Init */
#define USB_DEV_API_ETH_DEINIT       17U  /**< USB_ETH_DeInit */
#define USB_DEV_API_ETH_GET_STATS    18U  /**< USB_ETH_GetStats */
#define USB_DEV_API_TCP_LISTEN       19U  /**< USB_ETH_TCP_Listen */
#define USB_DEV_API_TCP_SEND         20U  /**< USB_ETH_TCP_Send / SendString */
#define USB_DEV_API_TCP_CLOSE        21U  /**< USB_ETH_TCP_Close */
#define USB_DEV_API_UDP_BIND         22U  /**< USB_ETH_UDP_Bind */
#define USB_DEV_API_UDP_SEND         23U  /**< USB_ETH_UDP_SendTo */

/* Значение (value) для кодов API_ERROR: причина отказа. */
#define USB_DEV_API_ERR_ISR          1    /**< вызов из прерывания */
#define USB_DEV_API_ERR_NOT_INIT     2    /**< модуль не включён / сеть не поднята */
#define USB_DEV_API_ERR_PARAM        3    /**< неверный параметр (NULL, 0, слишком длинно) */
#define USB_DEV_API_ERR_CLOSED       4    /**< соединение/сокет уже закрыт */
#define USB_DEV_API_ERR_BUSY_DEINIT  5    /**< Init во время выполнения DeInit */

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
 * @brief  Адрес IN-точки данных COM в текущих дескрипторах.
 * @return 0x82 (только COM) или 0x84 (COM после сети)
 */
uint8_t usb_dev_desc_com_ep_in(void);

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
