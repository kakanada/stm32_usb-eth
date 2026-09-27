/**
 ******************************************************************************
 * @file    example_echo.c
 * @brief   Пример usb_eth: TCP эхо-сервер на порту 7 и отслеживание сети.
 *          Фрагменты вставляются в соответствующие USER CODE секции CubeMX.
 * @author  Mechanic
 * @date    27.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include "main.h"
#include "usb_eth.h"

#define ECHO_PORT 7U

/**
 * @brief Сеть поднялась/упала: здесь удобно зажечь/погасить светодиод.
 * @param is_up true - есть IP
 * @param ip    собственный IP
 */
static void Echo_OnNet(bool is_up, uint32_t ip)
{
    (void)ip;
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, is_up ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief Клиент подключился - отправляем приветствие.
 * @param conn соединение
 */
static void Echo_OnConnect(USB_ETH_TcpConn_t *conn)
{
    (void)USB_ETH_TCP_SendString(conn, "echo ready\r\n");
}

/**
 * @brief Пришли данные - возвращаем их обратно.
 * @param conn соединение
 * @param data данные
 * @param len  длина
 */
static void Echo_OnReceive(USB_ETH_TcpConn_t *conn, const uint8_t *data, uint16_t len)
{
    if (USB_ETH_TCP_Send(conn, data, len) != HAL_OK)
    {
        /* Клиент не забирает данные - буфер отправки переполнен */
        (void)USB_ETH_TCP_Close(conn);
    }
}

/**
 * @brief Соединение закрыто (любой причиной).
 * @param conn   соединение
 * @param reason причина
 */
static void Echo_OnDisconnect(USB_ETH_TcpConn_t *conn, USB_ETH_CloseReason_t reason)
{
    (void)conn;
    (void)reason;
}

int main(void)
{
    /* ... HAL_Init(), SystemClock_Config(), MX_GPIO_Init() ... */
    MX_USB_OTG_FS_PCD_Init();

    /* USER CODE BEGIN 2 */
    static const USB_ETH_TcpHandlers_t echo_handlers =
    {
        .on_connect    = Echo_OnConnect,
        .on_receive    = Echo_OnReceive,
        .on_sent       = NULL,
        .on_disconnect = Echo_OnDisconnect
    };

    if (USB_ETH_Init() != HAL_OK)
    {
        Error_Handler();
    }
    USB_ETH_SetNetCallback(Echo_OnNet);
    if (USB_ETH_TCP_Listen(ECHO_PORT, &echo_handlers, NULL) == NULL)
    {
        Error_Handler();
    }
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN 3 */
        USB_ETH_Process();
        /* USER CODE END 3 */
    }
}

/* stm32h7xx_it.c:
 *
 * void OTG_FS_IRQHandler(void)
 * {
 *     USER CODE BEGIN OTG_FS_IRQn 0
 *     USB_ETH_IRQHandler();
 *     return;
 *     USER CODE END OTG_FS_IRQn 0
 *     HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);   <- больше не выполняется
 * }
 */
