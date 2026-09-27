/**
 ******************************************************************************
 * @file    example_udp_discovery.c
 * @brief   Пример usb_eth: UDP-обнаружение платы в сети (порт 30303) - удобно
 *          в режиме CLIENT, когда IP выдан роутером и заранее неизвестен.
 * @author  Mechanic
 * @date    27.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

/* ПК шлёт широковещательную датаграмму "DISCOVER" на порт 30303, каждая
 * плата отвечает отправителю строкой "<имя> <IP>". */

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "usb_eth.h"

#define DISCOVERY_PORT 30303U

/**
 * @brief Приём запроса обнаружения - отвечаем отправителю.
 * @param sock        сокет
 * @param data        данные
 * @param len         длина
 * @param remote_ip   IP отправителя
 * @param remote_port порт отправителя
 */
static void Discovery_OnReceive(USB_ETH_UdpSocket_t *sock, const uint8_t *data, uint16_t len,
                                uint32_t remote_ip, uint16_t remote_port)
{
    static const char request[] = "DISCOVER";
    char reply[64];

    if ((len != (sizeof(request) - 1U)) || (memcmp(data, request, len) != 0))
    {
        return;
    }
    uint32_t ip = USB_ETH_GetIp();
    int n = snprintf(reply, sizeof(reply), "%s %u.%u.%u.%u", USB_ETH_HOSTNAME,
                     USB_ETH_IP4_OCTET(ip, 0), USB_ETH_IP4_OCTET(ip, 1),
                     USB_ETH_IP4_OCTET(ip, 2), USB_ETH_IP4_OCTET(ip, 3));
    if (n > 0)
    {
        (void)USB_ETH_UDP_SendTo(sock, remote_ip, remote_port, reply, (uint16_t)n);
    }
}

/**
 * @brief Запуск ответчика обнаружения (вызывать после USB_ETH_Init()).
 */
void Discovery_Start(void)
{
    if (USB_ETH_UDP_Bind(DISCOVERY_PORT, Discovery_OnReceive, NULL) == NULL)
    {
        Error_Handler();
    }
}
