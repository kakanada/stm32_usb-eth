/**
 ******************************************************************************
 * @file    usb_eth_opts.h
 * @brief   Compile-time параметры usb_eth и их значения по умолчанию. Общий
 *          для usb_eth, lwipopts.h и tusb_config.h - единый источник истины.
 * @author  Mechanic
 * @date    27.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef USB_ETH_OPTS_H
#define USB_ETH_OPTS_H

/* Все define ниже переопределяются ТОЛЬКО глобальными символами
 * препроцессора проекта (-D...), а не #define перед #include: этот файл
 * включают также исходники lwIP и TinyUSB, и они обязаны видеть те же
 * значения, что и сама библиотека. Править этот файл не нужно. */

/* ------------------------------------------------------------------------- */
/*  Режим работы сети                                                        */
/* ------------------------------------------------------------------------- */

/** STM32 - DHCP-сервер со статическим IP: раздаёт адрес подключённому ПК. */
#define USB_ETH_MODE_HOST   0U
/** STM32 - DHCP-клиент: получает IP от роутера/ПК, как обычное сетевое устройство. */
#define USB_ETH_MODE_CLIENT 1U

#ifndef USB_ETH_MODE
#define USB_ETH_MODE USB_ETH_MODE_HOST
#endif

#if (USB_ETH_MODE != USB_ETH_MODE_HOST) && (USB_ETH_MODE != USB_ETH_MODE_CLIENT)
#error "USB_ETH_MODE: допустимо только USB_ETH_MODE_HOST или USB_ETH_MODE_CLIENT"
#endif

/** Сборка IPv4-адреса в uint32_t (порядок байт хоста): USB_ETH_IP4(192, 168, 7, 1). */
#define USB_ETH_IP4(a, b, c, d) \
    ((((uint32_t)(a) & 0xFFU) << 24) | (((uint32_t)(b) & 0xFFU) << 16) | \
     (((uint32_t)(c) & 0xFFU) << 8)  | ((uint32_t)(d) & 0xFFU))

/** Режим HOST: собственный IP устройства (маска всегда /24, ПК получит .2/.3). */
#ifndef USB_ETH_HOST_IP
#define USB_ETH_HOST_IP USB_ETH_IP4(192, 168, 7, 1)
#endif

/** Режим CLIENT: имя устройства, передаваемое DHCP-серверу (видно в списке роутера). */
#ifndef USB_ETH_HOSTNAME
#define USB_ETH_HOSTNAME "stm32-usb-eth"
#endif

/* ------------------------------------------------------------------------- */
/*  Размеры статических пулов (без malloc)                                   */
/* ------------------------------------------------------------------------- */

/** Максимум одновременно слушаемых TCP-портов. */
#ifndef USB_ETH_MAX_TCP_SERVERS
#define USB_ETH_MAX_TCP_SERVERS 4U
#endif

/** Максимум одновременных TCP-соединений (суммарно на все серверы). */
#ifndef USB_ETH_MAX_TCP_CONNECTIONS
#define USB_ETH_MAX_TCP_CONNECTIONS 4U
#endif

/** Максимум одновременно открытых UDP-портов. */
#ifndef USB_ETH_MAX_UDP_SOCKETS
#define USB_ETH_MAX_UDP_SOCKETS 4U
#endif

/* ------------------------------------------------------------------------- */
/*  USB                                                                      */
/* ------------------------------------------------------------------------- */

/** Номер USB-порта TinyUSB: 0 - OTG_FS, 1 - OTG_HS (во встроенном FS PHY). */
#ifndef USB_ETH_RHPORT
#define USB_ETH_RHPORT 0U
#endif

/** 1 - использовать VBUS sensing (пин VBUS разведён на плате), 0 - нет. */
#ifndef USB_ETH_VBUS_SENSING
#define USB_ETH_VBUS_SENSING 0U
#endif

/** USB VID/PID. 0xCAFE - тестовый VID TinyUSB, для серийного изделия заменить. */
#ifndef USB_ETH_USB_VID
#define USB_ETH_USB_VID 0xCAFEU
#endif

#ifndef USB_ETH_USB_PID
#define USB_ETH_USB_PID 0x4011U
#endif

#ifndef USB_ETH_STR_MANUFACTURER
#define USB_ETH_STR_MANUFACTURER "Mechanic"
#endif

#ifndef USB_ETH_STR_PRODUCT
#define USB_ETH_STR_PRODUCT "STM32 USB Ethernet"
#endif

/* USB_ETH_MAC_ADDR - по умолчанию НЕ определён: MAC вычисляется из
 * уникального ID чипа (разный у каждой платы). Для фиксированного MAC:
 * -DUSB_ETH_MAC_ADDR="{0x02,0x11,0x22,0x33,0x44,0x56}" (первый байт 0x02 -
 * локально администрируемый адрес, младший бит последнего байта - 0). */

/* ------------------------------------------------------------------------- */
/*  Логирование через stm32_logger (опционально)                             */
/* ------------------------------------------------------------------------- */

/** 1 - писать события в stm32_logger (нужен logger.h в include path), 0 - нет. */
#ifndef USB_ETH_LOG_ENABLE
#define USB_ETH_LOG_ENABLE 0
#endif

/* Коды событий: адресное пространство 0x42 закреплено за usb_eth в
 * stm32_logger (LOGGER_ENABLE_USB_ETH, LOG_CODE_USB_ETH_*). Значения
 * совпадают с таблицей логгера; переопределять не требуется. */
#ifndef USB_ETH_LOG_CODE_INIT_OK
#define USB_ETH_LOG_CODE_INIT_OK       0x4200U /**< LOW: init выполнен, value = режим */
#endif
#ifndef USB_ETH_LOG_CODE_INIT_FAIL
#define USB_ETH_LOG_CODE_INIT_FAIL     0x4201U /**< HIGH: ошибка init, value = этап (1 tusb, 2 netif, 3 dhcp) */
#endif
#ifndef USB_ETH_LOG_CODE_USB_MOUNTED
#define USB_ETH_LOG_CODE_USB_MOUNTED   0x4202U /**< LOW: USB подключён к хосту */
#endif
#ifndef USB_ETH_LOG_CODE_USB_UNMOUNTED
#define USB_ETH_LOG_CODE_USB_UNMOUNTED 0x4203U /**< MEDIUM: USB отключён от хоста */
#endif
#ifndef USB_ETH_LOG_CODE_NET_UP
#define USB_ETH_LOG_CODE_NET_UP        0x4204U /**< LOW: сеть готова, value = IP */
#endif
#ifndef USB_ETH_LOG_CODE_NET_DOWN
#define USB_ETH_LOG_CODE_NET_DOWN      0x4205U /**< MEDIUM: сеть потеряна */
#endif
#ifndef USB_ETH_LOG_CODE_DHCP_TIMEOUT
#define USB_ETH_LOG_CODE_DHCP_TIMEOUT  0x4206U /**< MEDIUM: DHCP-клиент без адреса, value = мс */
#endif
#ifndef USB_ETH_LOG_CODE_TCP_ACCEPT
#define USB_ETH_LOG_CODE_TCP_ACCEPT    0x4207U /**< LOW: src = порт, value = IP клиента */
#endif
#ifndef USB_ETH_LOG_CODE_TCP_CLOSED
#define USB_ETH_LOG_CODE_TCP_CLOSED    0x4208U /**< LOW: src = порт, value = причина */
#endif
#ifndef USB_ETH_LOG_CODE_TCP_POOL_FULL
#define USB_ETH_LOG_CODE_TCP_POOL_FULL 0x4209U /**< HIGH: src = порт, соединение отклонено */
#endif
#ifndef USB_ETH_LOG_CODE_TCP_ERROR
#define USB_ETH_LOG_CODE_TCP_ERROR     0x420AU /**< MEDIUM: src = порт, value = код lwIP */
#endif
#ifndef USB_ETH_LOG_CODE_RX_DROP
#define USB_ETH_LOG_CODE_RX_DROP       0x420BU /**< MEDIUM: входящий кадр отброшен, value = длина */
#endif
#ifndef USB_ETH_LOG_CODE_TX_TIMEOUT
#define USB_ETH_LOG_CODE_TX_TIMEOUT    0x420CU /**< MEDIUM: таймаут USB, кадр отброшен, value = длина */
#endif
#ifndef USB_ETH_LOG_CODE_SEND_NO_MEM
#define USB_ETH_LOG_CODE_SEND_NO_MEM   0x420DU /**< MEDIUM: src = порт, value = запрошено байт */
#endif
#ifndef USB_ETH_LOG_CODE_LISTEN_FAIL
#define USB_ETH_LOG_CODE_LISTEN_FAIL   0x420EU /**< HIGH: src = порт, value = код lwIP */
#endif
#ifndef USB_ETH_LOG_CODE_UDP_BIND_FAIL
#define USB_ETH_LOG_CODE_UDP_BIND_FAIL 0x420FU /**< HIGH: src = порт, value = код lwIP */
#endif

#endif /* USB_ETH_OPTS_H */
