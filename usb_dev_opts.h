/**
 ******************************************************************************
 * @file    usb_dev_opts.h
 * @brief   Compile-time параметры библиотеки (USB, сеть, COM) и их значения по
 *          умолчанию. Общий для usb_dev, usb_eth, usb_com, lwipopts.h и
 *          tusb_config.h - единый источник истины.
 * @author  Mechanic
 * @date    03.10.2026
 * @version 2.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef USB_DEV_OPTS_H
#define USB_DEV_OPTS_H

/* ========================================================================= */
/*  Логирование через stm32_logger: 1 - включено, 0 - выключено.             */
/*  Единственный переключатель для всей библиотеки (USB, сеть, COM). При 0   */
/*  logger.h не подключается вовсе. При 1 нужны logger.h в include path,     */
/*  LOGGER_Init() до USB_ETH_Init()/USB_COM_Init() и в logger_codes.h        */
/*  проекта - LOGGER_ENABLE_USB_ETH (коды 0x42xx) и LOGGER_ENABLE_USB_DEV    */
/*  (коды 0x43xx). Можно поменять здесь или задать -DUSB_DEV_LOG_ENABLE=1.   */
/* ========================================================================= */
#ifndef USB_DEV_LOG_ENABLE
#define USB_DEV_LOG_ENABLE 0
#endif

#if defined(USB_ETH_LOG_ENABLE)
#error "usb_eth 2.0: USB_ETH_LOG_ENABLE переименован в USB_DEV_LOG_ENABLE"
#endif

/* Остальные define ниже переопределяются ТОЛЬКО глобальными символами
 * препроцессора проекта (-D...), а не #define перед #include: этот файл
 * включают также исходники lwIP и TinyUSB, и они обязаны видеть те же
 * значения, что и сама библиотека. Править их здесь не нужно. */

/* ------------------------------------------------------------------------- */
/*  Сеть (USB_ETH_xxx)                                                       */
/* ------------------------------------------------------------------------- */

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

/** Число буферов lwIP для входящих кадров (PBUF_POOL_SIZE), по ~1.5 КБ ОЗУ каждый. Любой
 *  кадр, даже 60-байтный, занимает целый буфер. Их держат: очередь приёма (до 4), сегменты
 *  TCP, пришедшие не по порядку, - на каждое соединение. Если в USB_ETH_GetStats() растёт
 *  rx_drop_no_pbuf - увеличить. */
#ifndef USB_ETH_RX_PBUF_POOL_SIZE
#define USB_ETH_RX_PBUF_POOL_SIZE 16
#endif

/* ------------------------------------------------------------------------- */
/*  USB (общее для сети и COM)                                               */
/* ------------------------------------------------------------------------- */

/* Защита от старых имён (до версии 2.0): молча проигнорированный define опаснее ошибки. */
#if defined(USB_ETH_RHPORT) || defined(USB_ETH_VBUS_SENSING) || defined(USB_ETH_USB_VID) || \
    defined(USB_ETH_USB_PID) || defined(USB_ETH_STR_MANUFACTURER) || defined(USB_ETH_STR_PRODUCT)
#error "usb_eth 2.0: USB_ETH_RHPORT/VBUS_SENSING/USB_VID/USB_PID/STR_* переименованы в USB_DEV_*"
#endif

/** Номер USB-порта TinyUSB: 0 - OTG_FS, 1 - OTG_HS (во встроенном FS PHY). На STM32H7
 *  с единственным USB (H72x/H73x/H7Ax/H7Bx) TinyUSB отображает OTG_HS на порт 0 -
 *  оставить 0, прерывание - OTG_HS_IRQHandler(). */
#ifndef USB_DEV_RHPORT
#define USB_DEV_RHPORT 0U
#endif

/** 1 - использовать VBUS sensing (пин VBUS разведён на плате), 0 - нет. */
#ifndef USB_DEV_VBUS_SENSING
#define USB_DEV_VBUS_SENSING 0U
#endif

/** USB VID. 0xCAFE - тестовый VID TinyUSB, для серийного изделия заменить. */
#ifndef USB_DEV_VID
#define USB_DEV_VID 0xCAFEU
#endif

/* PID - свой для каждого набора устройств: Windows запоминает описание устройства
 * по VID/PID, и при одном PID на разные наборы драйверы встали бы неверно. */

/** PID, когда включена только сеть (USB_ETH_Init). */
#ifndef USB_DEV_PID_ETH
#define USB_DEV_PID_ETH 0x4011U
#endif

/** PID, когда включён только COM (USB_COM_Init). */
#ifndef USB_DEV_PID_COM
#define USB_DEV_PID_COM 0x4012U
#endif

/** PID, когда включены сеть и COM одновременно. */
#ifndef USB_DEV_PID_ETH_COM
#define USB_DEV_PID_ETH_COM 0x4013U
#endif

#ifndef USB_DEV_STR_MANUFACTURER
#define USB_DEV_STR_MANUFACTURER "Mechanic"
#endif

#ifndef USB_DEV_STR_PRODUCT
#define USB_DEV_STR_PRODUCT "STM32 USB Device"
#endif

/** Сколько плата остаётся отключённой от ПК при смене набора устройств (Init/DeInit
 *  второго устройства, когда первое уже работает), мс. */
#ifndef USB_DEV_REENUM_MS
#define USB_DEV_REENUM_MS 300U
#endif

/* ------------------------------------------------------------------------- */
/*  Виртуальный COM-порт (CDC-ACM)                                           */
/* ------------------------------------------------------------------------- */

/** Буфер приёма COM, байт. Пока он полон, ПК ждёт - данные не теряются. */
#ifndef USB_COM_RX_BUF_SIZE
#define USB_COM_RX_BUF_SIZE 512
#endif

/** Буфер передачи COM, байт: USB_COM_Transmit() принимает данные, пока в нём есть место. */
#ifndef USB_COM_TX_BUF_SIZE
#define USB_COM_TX_BUF_SIZE 1024
#endif

/* USB_ETH_MAC_ADDR - по умолчанию НЕ определён: MAC вычисляется из
 * уникального ID чипа (разный у каждой платы). Для фиксированного MAC:
 * -DUSB_ETH_MAC_ADDR="{0x02,0x11,0x22,0x33,0x44,0x56}" (первый байт 0x02 -
 * локально администрируемый адрес, младший бит последнего байта - 0). */

/* ------------------------------------------------------------------------- */
/*  Коды событий для stm32_logger (при USB_DEV_LOG_ENABLE = 1)               */
/* ------------------------------------------------------------------------- */

/* Общая часть и COM: адресное пространство 0x43 (LOGGER_ENABLE_USB_DEV,
 * LOG_CODE_USB_DEV_*). Значения совпадают с таблицей логгера. */
#ifndef USB_DEV_LOG_CODE_USB_START_FAIL
#define USB_DEV_LOG_CODE_USB_START_FAIL   0x4300U /**< HIGH: TinyUSB не запустился */
#endif
#ifndef USB_DEV_LOG_CODE_INIT_REFUSED
#define USB_DEV_LOG_CODE_INIT_REFUSED     0x4301U /**< HIGH: Init отклонён - нет точек на сеть+COM, src = устройство (1 сеть, 2 COM) */
#endif
#ifndef USB_DEV_LOG_CODE_USB_CONNECTED
#define USB_DEV_LOG_CODE_USB_CONNECTED    0x4302U /**< LOW: подключено к ПК, value = набор (бит0 сеть, бит1 COM) */
#endif
#ifndef USB_DEV_LOG_CODE_USB_DISCONNECTED
#define USB_DEV_LOG_CODE_USB_DISCONNECTED 0x4303U /**< MEDIUM: отключено от ПК (кабель/сон ПК), value = набор */
#endif
#ifndef USB_DEV_LOG_CODE_REENUM
#define USB_DEV_LOG_CODE_REENUM           0x4304U /**< LOW: переподключение к ПК, value = новый набор */
#endif
#ifndef USB_DEV_LOG_CODE_COM_INIT
#define USB_DEV_LOG_CODE_COM_INIT         0x4305U /**< LOW: COM-порт включён */
#endif
#ifndef USB_DEV_LOG_CODE_COM_DEINIT
#define USB_DEV_LOG_CODE_COM_DEINIT       0x4306U /**< LOW: COM-порт выключен */
#endif
#ifndef USB_DEV_LOG_CODE_COM_OPEN
#define USB_DEV_LOG_CODE_COM_OPEN         0x4307U /**< LOW: порт открыт на ПК (DTR), value = скорость */
#endif
#ifndef USB_DEV_LOG_CODE_COM_CLOSE
#define USB_DEV_LOG_CODE_COM_CLOSE        0x4308U /**< LOW: порт закрыт на ПК */
#endif
#ifndef USB_DEV_LOG_CODE_COM_TX_BUSY
#define USB_DEV_LOG_CODE_COM_TX_BUSY      0x4309U /**< MEDIUM: передача отклонена, value = длина (раз на серию) */
#endif
#ifndef USB_DEV_LOG_CODE_ETH_DEINIT
#define USB_DEV_LOG_CODE_ETH_DEINIT       0x430AU /**< LOW: сеть выключена (USB_ETH_DeInit) */
#endif
#ifndef USB_DEV_LOG_CODE_API_ERROR
#define USB_DEV_LOG_CODE_API_ERROR        0x430BU /**< MEDIUM: неверный вызов USB_/USB_COM_, src = функция, value = причина */
#endif
#ifndef USB_DEV_LOG_CODE_COM_TX_NOT_READY
#define USB_DEV_LOG_CODE_COM_TX_NOT_READY 0x430CU /**< MEDIUM: Transmit, а COM не подключён к ПК, value = длина */
#endif
#ifndef USB_DEV_LOG_CODE_COM_TX_DISCARD
#define USB_DEV_LOG_CODE_COM_TX_DISCARD   0x430DU /**< MEDIUM: порт открыт/закрыт - неотправленные данные стёрты, value = байт */
#endif
#ifndef USB_DEV_LOG_CODE_USB_CONNECT_FAIL
#define USB_DEV_LOG_CODE_USB_CONNECT_FAIL 0x430EU /**< HIGH: TinyUSB не подключил/отключил плату, value = 1 подкл., 0 откл. */
#endif
#ifndef USB_DEV_LOG_CODE_TUSB_ASSERT
#define USB_DEV_LOG_CODE_TUSB_ASSERT      0x430FU /**< HIGH: внутренняя ошибка TinyUSB (TU_ASSERT), value = адрес кода */
#endif
#ifndef USB_DEV_LOG_CODE_LOG_SUPPRESSED
#define USB_DEV_LOG_CODE_LOG_SUPPRESSED   0x4310U /**< MEDIUM: записи подавлены (частота/повторный вход), src = код, value = сколько */
#endif

/** Ошибки одного кода пишутся в лог не чаще этого интервала (мс); пропущенные
 *  считаются и выводятся одной записью USB_DEV_LOG_CODE_LOG_SUPPRESSED. */
#ifndef USB_DEV_LOG_MIN_INTERVAL_MS
#define USB_DEV_LOG_MIN_INTERVAL_MS 1000U
#endif

/* Сеть: адресное пространство 0x42 закреплено за usb_eth в
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
#ifndef USB_ETH_LOG_CODE_API_ERROR
#define USB_ETH_LOG_CODE_API_ERROR     0x4210U /**< MEDIUM: неверный вызов USB_ETH_, src = функция, value = причина */
#endif
#ifndef USB_ETH_LOG_CODE_TX_NO_USB
#define USB_ETH_LOG_CODE_TX_NO_USB     0x4211U /**< MEDIUM: кадр не отправлен - USB не подключён, value = длина */
#endif
#ifndef USB_ETH_LOG_CODE_TCP_SEND_FAIL
#define USB_ETH_LOG_CODE_TCP_SEND_FAIL 0x4212U /**< MEDIUM: ошибка tcp_write/tcp_output, src = порт, value = код lwIP */
#endif
#ifndef USB_ETH_LOG_CODE_UDP_SEND_FAIL
#define USB_ETH_LOG_CODE_UDP_SEND_FAIL 0x4213U /**< MEDIUM: ошибка udp_sendto, src = порт, value = код lwIP */
#endif
#ifndef USB_ETH_LOG_CODE_RX_INPUT_FAIL
#define USB_ETH_LOG_CODE_RX_INPUT_FAIL 0x4214U /**< MEDIUM: lwIP отверг входящий кадр, value = код lwIP */
#endif
#ifndef USB_ETH_LOG_CODE_LWIP_MEM_ERR
#define USB_ETH_LOG_CODE_LWIP_MEM_ERR  0x4215U /**< HIGH: нехватка памяти в lwIP, src = пул (0xFFFF - куча), value = всего отказов */
#endif
#ifndef USB_ETH_LOG_CODE_LWIP_ASSERT
#define USB_ETH_LOG_CODE_LWIP_ASSERT   0x4216U /**< HIGH: внутренняя проверка lwIP (LWIP_ASSERT), value = адрес кода */
#endif
#ifndef USB_ETH_LOG_CODE_LWIP_ARG_ERR
#define USB_ETH_LOG_CODE_LWIP_ARG_ERR  0x4217U /**< HIGH: неверные аргументы функции lwIP (LWIP_ERROR), value = адрес кода */
#endif
#ifndef USB_ETH_LOG_CODE_TCP_CLOSE_RST
#define USB_ETH_LOG_CODE_TCP_CLOSE_RST 0x4218U /**< MEDIUM: нет памяти на закрытие (FIN) - сброшено RST, src = порт */
#endif
#ifndef USB_ETH_LOG_CODE_TCP_ACCEPT_ERR
#define USB_ETH_LOG_CODE_TCP_ACCEPT_ERR 0x4219U /**< MEDIUM: lwIP сообщил ошибку при входящем подключении, src = порт, value = код lwIP */
#endif
#ifndef USB_ETH_LOG_CODE_UDP_RX_TRUNC
#define USB_ETH_LOG_CODE_UDP_RX_TRUNC  0x421AU /**< MEDIUM: входящая датаграмма обрезана до USB_ETH_UDP_MAX_PAYLOAD, src = порт, value = длина */
#endif

#endif /* USB_DEV_OPTS_H */
