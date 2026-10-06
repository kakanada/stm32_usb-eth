/**
 ******************************************************************************
 * @file    usb_eth_sock.c
 * @brief   TCP-серверы и UDP-сокеты usb_eth поверх lwIP raw API: статические
 *          пулы, колбэки подключения/приёма/отправки/закрытия.
 * @author  Mechanic
 * @date    03.10.2026
 * @version 2.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include <string.h>

#include "usb_eth.h"
#include "usb_eth_internal.h"

#include "lwip/tcp.h"
#include "lwip/priv/tcp_priv.h"   /* tcp_active_pcbs, tcp_tw_pcbs - для DeInit */
#include "lwip/udp.h"
#include "lwip/pbuf.h"

/* ------------------------------------------------------------------------- */
/*  Внутренние константы                                                     */
/* ------------------------------------------------------------------------- */

/** TCP keepalive: первая проба через 5 с тишины, далее 3 пробы раз в 1 с -
 *  пропавший клиент обнаруживается примерно за 8 с. */
#define USB_ETH_KEEPALIVE_IDLE_MS   5000U
#define USB_ETH_KEEPALIVE_INTVL_MS  1000U
#define USB_ETH_KEEPALIVE_CNT       3U

/** Максимальная полезная нагрузка UDP без фрагментации (1500 - 20 - 8). */
#define USB_ETH_UDP_MAX_PAYLOAD     1472U

/* ------------------------------------------------------------------------- */
/*  Состояние модуля                                                         */
/* ------------------------------------------------------------------------- */

static USB_ETH_TcpServer_t s_servers[USB_ETH_MAX_TCP_SERVERS];
static USB_ETH_TcpConn_t s_conns[USB_ETH_MAX_TCP_CONNECTIONS];
static USB_ETH_UdpSocket_t s_udp[USB_ETH_MAX_UDP_SOCKETS];

/* pcb, сброшенный через tcp_abort() во время колбэка lwIP. Обнуляется на
 * входе каждого колбэка; если на выходе он равен pcb колбэка - колбэк
 * обязан вернуть ERR_ABRT (требование lwIP raw API). */
static struct tcp_pcb *s_aborted_pcb;

/* Буфер для склейки UDP-датаграммы, пришедшей цепочкой pbuf. */
static uint8_t s_udp_rx_buf[USB_ETH_UDP_MAX_PAYLOAD];

/* ------------------------------------------------------------------------- */
/*  TCP: внутренние функции                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief  Проверяет, что conn - живое соединение из пула.
 * @param  conn проверяемый указатель
 * @return true - можно работать с conn->pcb
 */
static bool usb_eth_conn_valid(const USB_ETH_TcpConn_t *conn)
{
    return (conn != NULL) && (conn->used != 0U) && (conn->pcb != NULL);
}

/**
 * @brief  Проверяет вызов функции API; при отказе пишет причину в лог.
 * @param  func      функция (USB_DEV_API_*)
 * @param  not_ready сеть не включена
 * @param  bad_param неверный параметр
 * @return true - вызов отклонён
 */
static bool usb_eth_api_bad(uint16_t func, bool not_ready, bool bad_param)
{
    int32_t reason;

    if (usb_eth_in_isr())
    {
        reason = USB_DEV_API_ERR_ISR;
    }
    else if (not_ready)
    {
        reason = USB_DEV_API_ERR_NOT_INIT;
    }
    else if (bad_param)
    {
        reason = USB_DEV_API_ERR_PARAM;
    }
    else
    {
        return false;
    }
    (void)func;
    (void)reason;
    USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_API_ERROR, func, reason);
    return true;
}

/**
 * @brief  Проверяет соединение, переданное в API; при отказе пишет в лог.
 * @param  func функция (USB_DEV_API_*)
 * @param  conn соединение
 * @return true - соединение NULL или уже закрыто
 */
static bool usb_eth_conn_bad(uint16_t func, const USB_ETH_TcpConn_t *conn)
{
    if (usb_eth_conn_valid(conn))
    {
        return false;
    }
    (void)func;
    USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_API_ERROR, func,
                    (conn == NULL) ? USB_DEV_API_ERR_PARAM : USB_DEV_API_ERR_CLOSED);
    return true;
}

/**
 * @brief Отвязывает колбэки lwIP от pcb (после этого lwIP не вызовет нас).
 * @param pcb соединение lwIP
 */
static void usb_eth_tcp_detach(struct tcp_pcb *pcb)
{
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
}

/**
 * @brief Освобождает слот соединения и сообщает пользователю о закрытии.
 *        Должна вызываться, когда pcb уже отвязан/закрыт/уничтожен.
 * @param conn   соединение
 * @param reason причина закрытия
 */
static void usb_eth_conn_release(USB_ETH_TcpConn_t *conn, USB_ETH_CloseReason_t reason)
{
    USB_ETH_TcpServer_t *server = conn->server;

    conn->pcb = NULL;   /* с этого момента API на conn возвращает ошибку */
    USB_ETH_LOG(USB_ETH_LOG_CODE_TCP_CLOSED, server->port, reason);
    if (server->handlers.on_disconnect != NULL)
    {
        server->handlers.on_disconnect(conn, reason);
    }
    memset(conn, 0, sizeof(*conn));
}

/**
 * @brief Закрывает pcb: корректно (FIN), а при нехватке памяти - сбросом.
 * @param pcb соединение lwIP (колбэки уже отвязаны)
 */
static void usb_eth_tcp_shutdown(struct tcp_pcb *pcb)
{
    if (tcp_close(pcb) != ERR_OK)
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_TCP_CLOSE_RST, pcb->local_port, 0);
        s_aborted_pcb = pcb;
        tcp_abort(pcb);
    }
}

/**
 * @brief  Код возврата для колбэка lwIP после вызова пользовательского кода:
 *         ERR_ABRT, если пользователь уничтожил этот pcb через tcp_abort().
 * @param  pcb pcb, для которого вызван колбэк
 * @return ERR_ABRT или ERR_OK
 */
static err_t usb_eth_tcp_cb_result(const struct tcp_pcb *pcb)
{
    if (s_aborted_pcb == pcb)
    {
        s_aborted_pcb = NULL;
        return ERR_ABRT;
    }
    return ERR_OK;
}

/**
 * @brief  Колбэк lwIP: приняты данные или удалённая сторона закрыла поток.
 * @param  arg  соединение (USB_ETH_TcpConn_t*)
 * @param  pcb  pcb lwIP
 * @param  p    данные; NULL - удалённая сторона закрыла соединение
 * @param  err  код ошибки lwIP
 * @return ERR_OK / ERR_ABRT
 */
static err_t usb_eth_tcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    USB_ETH_TcpConn_t *conn = (USB_ETH_TcpConn_t *)arg;

    s_aborted_pcb = NULL;
    if ((conn == NULL) || (conn->pcb != pcb))
    {
        /* Соединение уже закрыто нами - данные просто выбрасываем */
        if (p != NULL)
        {
            tcp_recved(pcb, p->tot_len);
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if ((p == NULL) || (err != ERR_OK))
    {
        if (p != NULL)
        {
            pbuf_free(p);
        }
        usb_eth_tcp_detach(pcb);
        usb_eth_tcp_shutdown(pcb);
        usb_eth_conn_release(conn, USB_ETH_CLOSE_REMOTE);
        return usb_eth_tcp_cb_result(pcb);
    }

    /* Окно подтверждаем ДО передачи пользователю: если он закроет
     * соединение прямо в on_receive, lwIP не станет отвечать RST из-за
     * "непрочитанных" данных. */
    tcp_recved(pcb, p->tot_len);

    const USB_ETH_TcpHandlers_t *h = &conn->server->handlers;
    for (struct pbuf *q = p; q != NULL; q = q->next)
    {
        if ((h->on_receive == NULL) || (conn->pcb != pcb))
        {
            break;   /* обработчика нет или соединение закрыто внутри on_receive */
        }
        h->on_receive(conn, (const uint8_t *)q->payload, q->len);
    }
    pbuf_free(p);
    return usb_eth_tcp_cb_result(pcb);
}

/**
 * @brief  Колбэк lwIP: удалённая сторона подтвердила приём данных.
 * @param  arg соединение
 * @param  pcb pcb lwIP
 * @param  len подтверждено байт
 * @return ERR_OK / ERR_ABRT
 */
static err_t usb_eth_tcp_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    USB_ETH_TcpConn_t *conn = (USB_ETH_TcpConn_t *)arg;

    s_aborted_pcb = NULL;
    if ((conn != NULL) && (conn->pcb == pcb) && (conn->server->handlers.on_sent != NULL))
    {
        conn->server->handlers.on_sent(conn, len);
    }
    return usb_eth_tcp_cb_result(pcb);
}

/**
 * @brief Колбэк lwIP: соединение уничтожено стеком (RST, таймаут). pcb к
 *        этому моменту уже освобождён lwIP - трогать его нельзя.
 * @param arg соединение
 * @param err причина (ERR_RST, ERR_ABRT, ...)
 */
static void usb_eth_tcp_err_cb(void *arg, err_t err)
{
    USB_ETH_TcpConn_t *conn = (USB_ETH_TcpConn_t *)arg;
    USB_ETH_CloseReason_t reason;

    if ((conn == NULL) || (conn->used == 0U) || (conn->pcb == NULL))
    {
        return;
    }
    switch (err)
    {
        case ERR_RST:
            reason = USB_ETH_CLOSE_RESET;
            break;
        case ERR_ABRT:   /* свои abort'ы мы делаем с отвязанными колбэками */
            reason = USB_ETH_CLOSE_TIMEOUT;
            break;
        case ERR_CLSD:
            reason = USB_ETH_CLOSE_REMOTE;
            break;
        default:
            reason = USB_ETH_CLOSE_ERROR;
            break;
    }
    USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_TCP_ERROR, conn->server->port, err);
    usb_eth_conn_release(conn, reason);
}

/**
 * @brief  Колбэк lwIP: новое входящее соединение.
 * @param  arg    сервер (USB_ETH_TcpServer_t*)
 * @param  newpcb pcb нового соединения
 * @param  err    код ошибки lwIP
 * @return ERR_OK - принято; ERR_ABRT - отклонено (pcb уничтожен)
 */
static err_t usb_eth_tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    USB_ETH_TcpServer_t *server = (USB_ETH_TcpServer_t *)arg;
    USB_ETH_TcpConn_t *conn = NULL;

    s_aborted_pcb = NULL;
    if ((err != ERR_OK) || (newpcb == NULL) || (server == NULL))
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_TCP_ACCEPT_ERR, (server != NULL) ? server->port : 0U,
                        (err != ERR_OK) ? err : ERR_VAL);
        return ERR_VAL;
    }
    for (uint32_t i = 0U; i < USB_ETH_MAX_TCP_CONNECTIONS; i++)
    {
        if (s_conns[i].used == 0U)
        {
            conn = &s_conns[i];
            break;
        }
    }
    if (conn == NULL)
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_TCP_POOL_FULL, server->port, 0);
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    memset(conn, 0, sizeof(*conn));
    conn->used = 1U;
    conn->pcb = newpcb;
    conn->server = server;
    conn->user_ctx = server->user_ctx;
    conn->remote_ip = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&newpcb->remote_ip)));
    conn->remote_port = newpcb->remote_port;

    tcp_arg(newpcb, conn);
    tcp_recv(newpcb, usb_eth_tcp_recv_cb);
    tcp_sent(newpcb, usb_eth_tcp_sent_cb);
    tcp_err(newpcb, usb_eth_tcp_err_cb);
    tcp_nagle_disable(newpcb);   /* короткие ответы уходят сразу */

    ip_set_option(newpcb, SOF_KEEPALIVE);
    newpcb->keep_idle = USB_ETH_KEEPALIVE_IDLE_MS;
    newpcb->keep_intvl = USB_ETH_KEEPALIVE_INTVL_MS;
    newpcb->keep_cnt = USB_ETH_KEEPALIVE_CNT;

    USB_ETH_LOG(USB_ETH_LOG_CODE_TCP_ACCEPT, server->port, conn->remote_ip);
    if (server->handlers.on_connect != NULL)
    {
        server->handlers.on_connect(conn);
    }
    return usb_eth_tcp_cb_result(newpcb);
}

/* ------------------------------------------------------------------------- */
/*  TCP: публичный API                                                       */
/* ------------------------------------------------------------------------- */

USB_ETH_TcpServer_t *USB_ETH_TCP_Listen(uint16_t port, const USB_ETH_TcpHandlers_t *handlers,
                                        void *user_ctx)
{
    USB_ETH_TcpServer_t *server = NULL;

    if (usb_eth_api_bad(USB_DEV_API_TCP_LISTEN, !usb_eth_is_ready(),
                        (port == 0U) || (handlers == NULL)))
    {
        return NULL;
    }
    for (uint32_t i = 0U; i < USB_ETH_MAX_TCP_SERVERS; i++)
    {
        if ((s_servers[i].used != 0U) && (s_servers[i].port == port))
        {
            return &s_servers[i];   /* идемпотентность: порт уже слушается */
        }
        if ((server == NULL) && (s_servers[i].used == 0U))
        {
            server = &s_servers[i];
        }
    }
    if (server == NULL)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_LISTEN_FAIL, port, ERR_MEM);
        return NULL;
    }

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb == NULL)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_LISTEN_FAIL, port, ERR_MEM);
        return NULL;
    }
    err_t err = tcp_bind(pcb, IP4_ADDR_ANY, port);
    if (err != ERR_OK)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_LISTEN_FAIL, port, err);
        (void)tcp_close(pcb);
        return NULL;
    }
    struct tcp_pcb *lpcb = tcp_listen(pcb);   /* при успехе pcb освобождён */
    if (lpcb == NULL)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_LISTEN_FAIL, port, ERR_MEM);
        (void)tcp_close(pcb);
        return NULL;
    }

    memset(server, 0, sizeof(*server));
    server->used = 1U;
    server->port = port;
    server->handlers = *handlers;
    server->user_ctx = user_ctx;
    server->pcb = lpcb;
    tcp_arg(lpcb, server);
    tcp_accept(lpcb, usb_eth_tcp_accept_cb);
    return server;
}

HAL_StatusTypeDef USB_ETH_TCP_Send(USB_ETH_TcpConn_t *conn, const void *data, uint16_t len)
{
    if (usb_eth_api_bad(USB_DEV_API_TCP_SEND, false, (data == NULL) && (len > 0U)) ||
        usb_eth_conn_bad(USB_DEV_API_TCP_SEND, conn))
    {
        return HAL_ERROR;
    }
    if (len == 0U)
    {
        return HAL_OK;
    }
    if (len > USB_ETH_TCP_GetFreeSpace(conn))
    {
        return HAL_BUSY;
    }

    struct tcp_pcb *pcb = (struct tcp_pcb *)conn->pcb;
    /* tcp_write атомарен: либо ставит в очередь всё, либо ничего */
    err_t err = tcp_write(pcb, data, len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_MEM)
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_SEND_NO_MEM, conn->server->port, len);
        return HAL_BUSY;
    }
    if (err != ERR_OK)
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_TCP_SEND_FAIL, conn->server->port, err);
        return HAL_ERROR;
    }
    err = tcp_output(pcb);
    if (err != ERR_OK)
    {
        /* Данные уже в очереди lwIP - уйдут при следующей попытке; только лог */
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_TCP_SEND_FAIL, conn->server->port, err);
    }
    return HAL_OK;
}

HAL_StatusTypeDef USB_ETH_TCP_SendString(USB_ETH_TcpConn_t *conn, const char *str)
{
    if (usb_eth_api_bad(USB_DEV_API_TCP_SEND, false, (str == NULL) || (strlen(str) > 0xFFFFU)))
    {
        return HAL_ERROR;
    }
    size_t len = strlen(str);
    return USB_ETH_TCP_Send(conn, str, (uint16_t)len);
}

uint16_t USB_ETH_TCP_GetFreeSpace(const USB_ETH_TcpConn_t *conn)
{
    if (!usb_eth_conn_valid(conn))
    {
        return 0U;
    }
    const struct tcp_pcb *pcb = (const struct tcp_pcb *)conn->pcb;
    /* Кроме байт буфера ограничено и число сегментов в очереди */
    if (tcp_sndqueuelen(pcb) >= (TCP_SND_QUEUELEN - 1U))
    {
        return 0U;
    }
    return (uint16_t)tcp_sndbuf(pcb);
}

bool USB_ETH_TCP_IsOpen(const USB_ETH_TcpConn_t *conn)
{
    return usb_eth_conn_valid(conn);
}

HAL_StatusTypeDef USB_ETH_TCP_Close(USB_ETH_TcpConn_t *conn)
{
    if (usb_eth_api_bad(USB_DEV_API_TCP_CLOSE, false, false) ||
        usb_eth_conn_bad(USB_DEV_API_TCP_CLOSE, conn))
    {
        return HAL_ERROR;
    }
    struct tcp_pcb *pcb = (struct tcp_pcb *)conn->pcb;
    usb_eth_tcp_detach(pcb);
    usb_eth_tcp_shutdown(pcb);
    usb_eth_conn_release(conn, USB_ETH_CLOSE_LOCAL);
    return HAL_OK;
}

void usb_eth_sock_link_lost(void)
{
    for (uint32_t i = 0U; i < USB_ETH_MAX_TCP_CONNECTIONS; i++)
    {
        USB_ETH_TcpConn_t *conn = &s_conns[i];
        if (usb_eth_conn_valid(conn))
        {
            struct tcp_pcb *pcb = (struct tcp_pcb *)conn->pcb;
            usb_eth_tcp_detach(pcb);
            tcp_abort(pcb);   /* вызывается не из колбэка lwIP - ERR_ABRT не нужен */
            usb_eth_conn_release(conn, USB_ETH_CLOSE_LINK_LOST);
        }
    }
}

void usb_eth_sock_deinit(void)
{
    usb_eth_sock_link_lost();

    for (uint32_t i = 0U; i < USB_ETH_MAX_TCP_SERVERS; i++)
    {
        if ((s_servers[i].used != 0U) && (s_servers[i].pcb != NULL))
        {
            struct tcp_pcb *lpcb = (struct tcp_pcb *)s_servers[i].pcb;
            tcp_arg(lpcb, NULL);
            tcp_accept(lpcb, NULL);
            (void)tcp_close(lpcb);   /* слушающий pcb закрывается всегда успешно */
        }
        memset(&s_servers[i], 0, sizeof(s_servers[i]));
    }
    for (uint32_t i = 0U; i < USB_ETH_MAX_UDP_SOCKETS; i++)
    {
        if ((s_udp[i].used != 0U) && (s_udp[i].pcb != NULL))
        {
            udp_remove((struct udp_pcb *)s_udp[i].pcb);
        }
        memset(&s_udp[i], 0, sizeof(s_udp[i]));
    }

    /* Соединения, уже закрытые пользователем, ещё живут в lwIP (дозакрытие,
     * TIME_WAIT до 2 мин) и держат свой порт: Listen на тот же порт после
     * повторного Init получил бы ERR_USE. Они отвязаны от библиотеки
     * (колбэков нет) - уничтожаются все. */
    while (tcp_active_pcbs != NULL)
    {
        tcp_abort(tcp_active_pcbs);
    }
    while (tcp_tw_pcbs != NULL)
    {
        tcp_abort(tcp_tw_pcbs);
    }
}

/* ------------------------------------------------------------------------- */
/*  UDP                                                                      */
/* ------------------------------------------------------------------------- */

/**
 * @brief Колбэк lwIP: принята UDP-датаграмма.
 * @param arg  сокет (USB_ETH_UdpSocket_t*)
 * @param pcb  pcb lwIP
 * @param p    данные
 * @param addr IP отправителя
 * @param port порт отправителя
 */
static void usb_eth_udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                                const ip_addr_t *addr, u16_t port)
{
    USB_ETH_UdpSocket_t *sock = (USB_ETH_UdpSocket_t *)arg;
    (void)pcb;

    if ((sock != NULL) && (sock->used != 0U) && (sock->on_receive != NULL))
    {
        const uint8_t *data = (const uint8_t *)p->payload;
        uint16_t len = p->len;
        if (p->next != NULL)
        {
            /* Датаграмма в нескольких pbuf - склеиваем в один буфер */
            if (p->tot_len > sizeof(s_udp_rx_buf))
            {
                USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_UDP_RX_TRUNC, sock->port, p->tot_len);
            }
            len = pbuf_copy_partial(p, s_udp_rx_buf, sizeof(s_udp_rx_buf), 0U);
            data = s_udp_rx_buf;
        }
        sock->on_receive(sock, data, len, lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(addr))), port);
    }
    pbuf_free(p);
}

USB_ETH_UdpSocket_t *USB_ETH_UDP_Bind(uint16_t port, USB_ETH_UdpReceive_t on_receive,
                                      void *user_ctx)
{
    USB_ETH_UdpSocket_t *sock = NULL;

    if (usb_eth_api_bad(USB_DEV_API_UDP_BIND, !usb_eth_is_ready(), port == 0U))
    {
        return NULL;
    }
    for (uint32_t i = 0U; i < USB_ETH_MAX_UDP_SOCKETS; i++)
    {
        if ((s_udp[i].used != 0U) && (s_udp[i].port == port))
        {
            return &s_udp[i];
        }
        if ((sock == NULL) && (s_udp[i].used == 0U))
        {
            sock = &s_udp[i];
        }
    }
    if (sock == NULL)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_UDP_BIND_FAIL, port, ERR_MEM);
        return NULL;
    }

    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb == NULL)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_UDP_BIND_FAIL, port, ERR_MEM);
        return NULL;
    }
    err_t err = udp_bind(pcb, IP4_ADDR_ANY, port);
    if (err != ERR_OK)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_UDP_BIND_FAIL, port, err);
        udp_remove(pcb);
        return NULL;
    }
    ip_set_option(pcb, SOF_BROADCAST);

    memset(sock, 0, sizeof(*sock));
    sock->used = 1U;
    sock->port = port;
    sock->on_receive = on_receive;
    sock->user_ctx = user_ctx;
    sock->pcb = pcb;
    udp_recv(pcb, usb_eth_udp_recv_cb, sock);
    return sock;
}

HAL_StatusTypeDef USB_ETH_UDP_SendTo(USB_ETH_UdpSocket_t *sock, uint32_t ip, uint16_t port,
                                     const void *data, uint16_t len)
{
    if (usb_eth_api_bad(USB_DEV_API_UDP_SEND, !USB_ETH_IsNetUp(),
                        (sock == NULL) || (data == NULL) || (len == 0U) ||
                        (len > USB_ETH_UDP_MAX_PAYLOAD) || (port == 0U)))
    {
        return HAL_ERROR;
    }
    if ((sock->used == 0U) || (sock->pcb == NULL))
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_API_ERROR, USB_DEV_API_UDP_SEND, USB_DEV_API_ERR_CLOSED);
        return HAL_ERROR;
    }

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p == NULL)
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_SEND_NO_MEM, sock->port, len);
        return HAL_BUSY;
    }
    (void)pbuf_take(p, data, len);

    ip_addr_t dst;
    ip_addr_set_ip4_u32(&dst, lwip_htonl(ip));
    err_t err = udp_sendto((struct udp_pcb *)sock->pcb, p, &dst, port);
    pbuf_free(p);

    if (err == ERR_MEM)
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_SEND_NO_MEM, sock->port, len);
        return HAL_BUSY;
    }
    if (err != ERR_OK)
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_UDP_SEND_FAIL, sock->port, err);
        return HAL_ERROR;
    }
    return HAL_OK;
}
