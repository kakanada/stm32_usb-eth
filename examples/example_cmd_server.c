/**
 ******************************************************************************
 * @file    example_cmd_server.c
 * @brief   Пример usb_eth: построчный командный TCP-сервер (порт 7777) -
 *          сборка строк из потока, ответы, выдача большого ответа порциями.
 * @author  Mechanic
 * @date    03.10.2026
 * @version 2.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

/* Протокол: одна команда = одна строка, оканчивающаяся '\n'.
 *   LED <0..7> | LED STOP - управление индикацией
 *   DUMP                  - выдать 20000 байт тестовых данных (порциями)
 *   QUIT                  - закрыть соединение
 * Ответ - строка "OK ..." или "ERR ...". Проверка с ПК:
 *   ncat 192.168.7.1 7777   (или любой TCP-терминал) */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "main.h"
#include "usb_eth.h"

#define CMD_PORT      7777U
#define CMD_LINE_MAX  64U
#define CMD_DUMP_SIZE 20000U

/** Состояние одного клиента: своя строка и свой прогресс выдачи. */
typedef struct
{
    char line[CMD_LINE_MAX];
    uint16_t line_len;
    uint32_t dump_left;   /* сколько байт DUMP ещё отправить */
} CmdClient_t;

static CmdClient_t s_clients[USB_ETH_MAX_TCP_CONNECTIONS];

/**
 * @brief  Находит свободный контекст клиента.
 * @return контекст или NULL
 */
static CmdClient_t *Cmd_AllocClient(void)
{
    for (uint32_t i = 0U; i < USB_ETH_MAX_TCP_CONNECTIONS; i++)
    {
        if (s_clients[i].line_len == 0xFFFFU)
        {
            memset(&s_clients[i], 0, sizeof(s_clients[i]));
            return &s_clients[i];
        }
    }
    return NULL;
}

/**
 * @brief Досылает DUMP, сколько помещается в буфер отправки.
 * @param conn соединение
 */
static void Cmd_PumpDump(USB_ETH_TcpConn_t *conn)
{
    CmdClient_t *c = (CmdClient_t *)conn->user_ctx;
    static uint8_t chunk[512];

    while (c->dump_left > 0U)
    {
        uint16_t n = (c->dump_left < sizeof(chunk)) ? (uint16_t)c->dump_left : (uint16_t)sizeof(chunk);
        memset(chunk, 'A' + (int)(c->dump_left % 26U), n);
        if (USB_ETH_TCP_Send(conn, chunk, n) != HAL_OK)
        {
            return;   /* буфер полон - продолжим в on_sent */
        }
        c->dump_left -= n;
    }
}

/**
 * @brief Выполняет одну команду.
 * @param conn соединение
 * @param line строка команды (без "\r\n")
 */
static void Cmd_Execute(USB_ETH_TcpConn_t *conn, const char *line)
{
    char reply[48];

    if (strncmp(line, "LED ", 4) == 0)
    {
        const char *arg = line + 4;
        char *end = NULL;
        long v = strtol(arg, &end, 10);
        if (strcmp(arg, "STOP") == 0)
        {
            /* LOAD_PWM_Stop(...) */
            (void)snprintf(reply, sizeof(reply), "OK LED STOP\r\n");
        }
        else if ((end != arg) && (*end == '\0') && (v >= 0) && (v <= 7))
        {
            /* LOAD_PWM_StartCycle(..., v) */
            (void)snprintf(reply, sizeof(reply), "OK LED %ld\r\n", v);
        }
        else
        {
            (void)snprintf(reply, sizeof(reply), "ERR bad cycle (0..7 or STOP)\r\n");
        }
        (void)USB_ETH_TCP_SendString(conn, reply);
    }
    else if (strcmp(line, "DUMP") == 0)
    {
        ((CmdClient_t *)conn->user_ctx)->dump_left = CMD_DUMP_SIZE;
        Cmd_PumpDump(conn);
    }
    else if (strcmp(line, "QUIT") == 0)
    {
        (void)USB_ETH_TCP_SendString(conn, "OK BYE\r\n");
        (void)USB_ETH_TCP_Close(conn);   /* ответ будет дослан перед закрытием */
    }
    else
    {
        (void)USB_ETH_TCP_SendString(conn, "ERR unknown command\r\n");
    }
}

/**
 * @brief Подключение: выделяем клиенту контекст или отказываем.
 * @param conn соединение
 */
static void Cmd_OnConnect(USB_ETH_TcpConn_t *conn)
{
    CmdClient_t *c = Cmd_AllocClient();
    if (c == NULL)
    {
        (void)USB_ETH_TCP_Close(conn);
        return;
    }
    conn->user_ctx = c;
}

/**
 * @brief Приём: собираем строки из потока (команда может прийти по частям).
 * @param conn соединение
 * @param data данные
 * @param len  длина
 */
static void Cmd_OnReceive(USB_ETH_TcpConn_t *conn, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0U; (i < len) && USB_ETH_TCP_IsOpen(conn); i++)
    {
        CmdClient_t *c = (CmdClient_t *)conn->user_ctx;
        char ch = (char)data[i];

        if (ch == '\n')
        {
            if ((c->line_len > 0U) && (c->line[c->line_len - 1U] == '\r'))
            {
                c->line_len--;
            }
            c->line[c->line_len] = '\0';
            c->line_len = 0U;
            Cmd_Execute(conn, c->line);   /* может закрыть conn (QUIT) */
        }
        else if (c->line_len < (CMD_LINE_MAX - 1U))
        {
            c->line[c->line_len++] = ch;
        }
    }
}

/**
 * @brief Клиент подтвердил приём - досылаем DUMP.
 * @param conn      соединение
 * @param acked_len подтверждено байт
 */
static void Cmd_OnSent(USB_ETH_TcpConn_t *conn, uint16_t acked_len)
{
    (void)acked_len;
    Cmd_PumpDump(conn);
}

/**
 * @brief Закрытие: освобождаем контекст клиента.
 * @param conn   соединение
 * @param reason причина
 */
static void Cmd_OnDisconnect(USB_ETH_TcpConn_t *conn, USB_ETH_CloseReason_t reason)
{
    (void)reason;
    if (conn->user_ctx != NULL)
    {
        ((CmdClient_t *)conn->user_ctx)->line_len = 0xFFFFU;   /* слот свободен */
    }
}

/**
 * @brief Запуск командного сервера (вызывать после USB_ETH_Init()).
 */
void CmdServer_Start(void)
{
    static const USB_ETH_TcpHandlers_t handlers =
    {
        .on_connect    = Cmd_OnConnect,
        .on_receive    = Cmd_OnReceive,
        .on_sent       = Cmd_OnSent,
        .on_disconnect = Cmd_OnDisconnect
    };

    for (uint32_t i = 0U; i < USB_ETH_MAX_TCP_CONNECTIONS; i++)
    {
        s_clients[i].line_len = 0xFFFFU;
    }
    if (USB_ETH_TCP_Listen(CMD_PORT, &handlers, NULL) == NULL)
    {
        Error_Handler();
    }
}
