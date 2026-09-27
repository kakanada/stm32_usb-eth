/**
 ******************************************************************************
 * @file    lwipopts.h
 * @brief   Настройки lwIP для usb_eth (NO_SYS, bare-metal). Выводятся из
 *          usb_eth_opts.h - править этот файл не нужно.
 * @author  Mechanic
 * @date    27.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#include "usb_eth_opts.h"

/* --- Система: без ОС, весь стек обслуживается из USB_ETH_Process() --- */
#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0   /* lwIP не вызывается из прерываний */
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0
#define LWIP_RAW                        0
#define LWIP_NOASSERT                       /* ассерты не останавливают прошивку */

/* --- Память: куча lwIP для исходящих TCP-данных (TCP_WRITE_FLAG_COPY) --- */
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (16 * 1024)
#define PBUF_POOL_SIZE                  8   /* входящие кадры */

/* --- Протоколы --- */
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define LWIP_ICMP                       1
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define ETH_PAD_SIZE                    0
#define ETHARP_SUPPORT_STATIC_ENTRIES   0

#if (USB_ETH_MODE == USB_ETH_MODE_CLIENT)
#define LWIP_DHCP                       1
#define LWIP_DHCP_DOES_ACD_CHECK        0   /* адрес проверяет сам DHCP-сервер */
#define LWIP_NETIF_HOSTNAME             1
#else
#define LWIP_DHCP                       0
/* DHCP-сервер: принимать запросы на порт 67, пока у ПК ещё нет адреса */
#define LWIP_IP_ACCEPT_UDP_PORT(p)      ((p) == PP_NTOHS(67))
#endif

/* --- TCP --- */
#define TCP_MSS                         (1500 - 40)
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * TCP_SND_BUF) / TCP_MSS)
#define LWIP_TCP_KEEPALIVE              1

/* --- Пулы: из размеров пулов usb_eth (+ служебные pcb) --- */
#define MEMP_NUM_TCP_PCB                (USB_ETH_MAX_TCP_CONNECTIONS + 2)
#define MEMP_NUM_TCP_PCB_LISTEN         USB_ETH_MAX_TCP_SERVERS
#define MEMP_NUM_UDP_PCB                (USB_ETH_MAX_UDP_SOCKETS + 1)
#define MEMP_NUM_TCP_SEG                (TCP_SND_QUEUELEN * 2)

/* --- Интерфейс --- */
#define LWIP_SINGLE_NETIF               1
#define LWIP_NETIF_LINK_CALLBACK        0
#define LWIP_NETIF_STATUS_CALLBACK      0

/* --- Отладка выключена --- */
#define LWIP_STATS                      0
#define LWIP_DEBUG                      0

#endif /* LWIPOPTS_H */
