/**
 ******************************************************************************
 * @file    usb_eth.h
 * @brief   STM32 как сетевое устройство по USB (CDC-NCM + lwIP): публичный API
 *          - запуск стека, события сети, TCP-серверы и UDP-сокеты с колбэками.
 * @author  Mechanic
 * @date    27.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef USB_ETH_H
#define USB_ETH_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "usb_eth_opts.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*  Типы                                                                     */
/* ========================================================================= */

/** Причина закрытия TCP-соединения (аргумент on_disconnect). */
typedef enum
{
    USB_ETH_CLOSE_LOCAL = 0,   /**< закрыто нашей стороной: USB_ETH_TCP_Close() */
    USB_ETH_CLOSE_REMOTE,      /**< удалённая сторона корректно закрыла соединение */
    USB_ETH_CLOSE_RESET,       /**< удалённая сторона сбросила соединение (RST) */
    USB_ETH_CLOSE_TIMEOUT,     /**< удалённая сторона пропала (keepalive/повторы исчерпаны) */
    USB_ETH_CLOSE_LINK_LOST,   /**< пропала сеть: USB отключён или потерян IP */
    USB_ETH_CLOSE_ERROR        /**< прочая ошибка стека */
} USB_ETH_CloseReason_t;

typedef struct USB_ETH_TcpConn_s   USB_ETH_TcpConn_t;
typedef struct USB_ETH_TcpServer_s USB_ETH_TcpServer_t;
typedef struct USB_ETH_UdpSocket_s USB_ETH_UdpSocket_t;

/**
 * @brief Обработчики событий TCP-сервера. Любой указатель может быть NULL.
 *        Все вызываются только из USB_ETH_Process(), не из прерывания.
 */
typedef struct
{
    /** Клиент подключился. conn действителен до on_disconnect включительно. */
    void (*on_connect)(USB_ETH_TcpConn_t *conn);
    /** Пришли данные. TCP - поток: одно сообщение клиента может прийти
     *  несколькими вызовами, несколько сообщений - одним. data действителен
     *  только внутри вызова. */
    void (*on_receive)(USB_ETH_TcpConn_t *conn, const uint8_t *data, uint16_t len);
    /** Клиент подтвердил приём acked_len байт - в буфере отправки освободилось
     *  место (удобно для отправки больших объёмов порциями). */
    void (*on_sent)(USB_ETH_TcpConn_t *conn, uint16_t acked_len);
    /** Соединение закрыто (вызывается ровно один раз на соединение, в т.ч.
     *  изнутри USB_ETH_TCP_Close()). После возврата conn использовать нельзя. */
    void (*on_disconnect)(USB_ETH_TcpConn_t *conn, USB_ETH_CloseReason_t reason);
} USB_ETH_TcpHandlers_t;

/** TCP-соединение (слот статического пула). */
struct USB_ETH_TcpConn_s
{
    /* --- Публичные поля (только чтение, кроме user_ctx) --- */
    USB_ETH_TcpServer_t *server;   /**< сервер, принявший соединение */
    uint32_t remote_ip;            /**< IP клиента (порядок байт хоста, см. USB_ETH_IP4) */
    uint16_t remote_port;          /**< порт клиента */
    void *user_ctx;                /**< свободное поле пользователя; при подключении = server->user_ctx */

    /* --- Внутренние поля (не трогать, только через API) --- */
    void *pcb;                     /**< struct tcp_pcb* lwIP; NULL - соединение закрыто */
    uint8_t used;                  /**< слот пула занят */
};

/** TCP-сервер (слушающий порт, слот статического пула). */
struct USB_ETH_TcpServer_s
{
    /* --- Публичные поля (только чтение) --- */
    uint16_t port;                    /**< слушаемый порт */
    USB_ETH_TcpHandlers_t handlers;   /**< копия обработчиков */
    void *user_ctx;                   /**< контекст, переданный в USB_ETH_TCP_Listen() */

    /* --- Внутренние поля --- */
    void *pcb;                        /**< struct tcp_pcb* lwIP (слушающий) */
    uint8_t used;                     /**< слот пула занят */
};

/**
 * @brief Обработчик входящей UDP-датаграммы (из USB_ETH_Process()).
 * @param sock        сокет, получивший датаграмму
 * @param data        данные (действительны только внутри вызова)
 * @param len         длина данных
 * @param remote_ip   IP отправителя (порядок байт хоста)
 * @param remote_port порт отправителя
 */
typedef void (*USB_ETH_UdpReceive_t)(USB_ETH_UdpSocket_t *sock, const uint8_t *data,
                                     uint16_t len, uint32_t remote_ip, uint16_t remote_port);

/** UDP-сокет (слот статического пула). */
struct USB_ETH_UdpSocket_s
{
    /* --- Публичные поля (только чтение) --- */
    uint16_t port;                   /**< локальный порт */
    USB_ETH_UdpReceive_t on_receive; /**< обработчик приёма */
    void *user_ctx;                  /**< контекст, переданный в USB_ETH_UDP_Bind() */

    /* --- Внутренние поля --- */
    void *pcb;                       /**< struct udp_pcb* lwIP */
    uint8_t used;                    /**< слот пула занят */
};

/**
 * @brief Обработчик изменения состояния сети (из USB_ETH_Process()).
 * @param is_up true - сеть готова (есть IP), false - сеть потеряна
 * @param ip    собственный IP устройства (порядок байт хоста), 0 при is_up == false
 */
typedef void (*USB_ETH_NetCallback_t)(bool is_up, uint32_t ip);

/** Байт n (0 - старший) IPv4-адреса: для печати "%u.%u.%u.%u". */
#define USB_ETH_IP4_OCTET(ip, n) ((uint8_t)((uint32_t)(ip) >> (24U - (8U * (uint32_t)(n)))))

/** Широковещательный адрес для USB_ETH_UDP_SendTo(). */
#define USB_ETH_IP4_BROADCAST 0xFFFFFFFFU

/* ========================================================================= */
/*  Инициализация и обслуживание                                             */
/* ========================================================================= */

/**
 * @brief  Запускает USB-стек и lwIP. Вызывать один раз после
 *         MX_USB_OTG_FS_PCD_Init() (нужны его тактирование и GPIO USB).
 *         Повторный вызов безопасен и возвращает HAL_OK.
 * @return HAL_OK - успех; HAL_ERROR - ошибка запуска или вызов из прерывания
 */
HAL_StatusTypeDef USB_ETH_Init(void);

/**
 * @brief Обслуживание стека: вызывать в while(1) как можно чаще, без
 *        задержек. Все колбэки библиотеки вызываются только отсюда.
 */
void USB_ETH_Process(void);

/**
 * @brief Обработчик прерывания USB. Вызывать из OTG_FS_IRQHandler() (секция
 *        USER CODE BEGIN OTG_FS_IRQn 0) с последующим return - HAL_PCD_IRQHandler
 *        вызываться не должен.
 */
void USB_ETH_IRQHandler(void);

/**
 * @brief Регистрирует обработчик изменения состояния сети (NULL - отключить).
 *        Если сеть уже поднята, обработчик будет вызван с is_up == true при
 *        ближайшем USB_ETH_Process().
 * @param cb обработчик
 */
void USB_ETH_SetNetCallback(USB_ETH_NetCallback_t cb);

/**
 * @brief  Готова ли сеть (USB подключён и IP назначен).
 * @return true - готова
 */
bool USB_ETH_IsNetUp(void);

/**
 * @brief  Собственный IP устройства.
 * @return IP (порядок байт хоста) или 0, если сеть не готова
 */
uint32_t USB_ETH_GetIp(void);

/* ========================================================================= */
/*  TCP                                                                      */
/* ========================================================================= */

/**
 * @brief  Открывает TCP-порт на приём подключений. Можно вызывать сразу после
 *         USB_ETH_Init(), не дожидаясь сети. Повторный вызов с тем же портом
 *         возвращает уже существующий сервер (обработчики не меняются).
 * @param  port     TCP-порт (1..65535)
 * @param  handlers обработчики событий (копируются, не NULL)
 * @param  user_ctx контекст пользователя (копируется в conn->user_ctx)
 * @return указатель на сервер или NULL (неверные параметры, пул занят,
 *         ошибка lwIP, библиотека не инициализирована, вызов из прерывания)
 */
USB_ETH_TcpServer_t *USB_ETH_TCP_Listen(uint16_t port, const USB_ETH_TcpHandlers_t *handlers,
                                        void *user_ctx);

/**
 * @brief  Ставит данные в очередь отправки: либо целиком, либо ничего.
 *         Данные копируются - буфер можно переиспользовать сразу после вызова.
 * @param  conn соединение
 * @param  data данные
 * @param  len  длина (0 - ничего не делает, HAL_OK)
 * @return HAL_OK - данные в очереди; HAL_BUSY - нет места (повторить позже,
 *         например в on_sent); HAL_ERROR - соединение закрыто/неверные параметры
 */
HAL_StatusTypeDef USB_ETH_TCP_Send(USB_ETH_TcpConn_t *conn, const void *data, uint16_t len);

/**
 * @brief  Отправка строки с завершающим нулём (без самого нуля).
 * @param  conn соединение
 * @param  str  строка (не длиннее 65535 символов)
 * @return как у USB_ETH_TCP_Send()
 */
HAL_StatusTypeDef USB_ETH_TCP_SendString(USB_ETH_TcpConn_t *conn, const char *str);

/**
 * @brief  Сколько байт сейчас гарантированно примет USB_ETH_TCP_Send().
 * @param  conn соединение
 * @return свободное место в байтах, 0 если соединение закрыто
 */
uint16_t USB_ETH_TCP_GetFreeSpace(const USB_ETH_TcpConn_t *conn);

/**
 * @brief  Открыто ли соединение (можно ли вызывать Send/Close).
 * @param  conn соединение
 * @return true - открыто
 */
bool USB_ETH_TCP_IsOpen(const USB_ETH_TcpConn_t *conn);

/**
 * @brief  Закрывает соединение: уже поставленные в очередь данные будут
 *         досланы. on_disconnect(USB_ETH_CLOSE_LOCAL) вызывается прямо внутри.
 * @param  conn соединение
 * @return HAL_OK - закрыто; HAL_ERROR - уже закрыто/неверный параметр
 */
HAL_StatusTypeDef USB_ETH_TCP_Close(USB_ETH_TcpConn_t *conn);

/* ========================================================================= */
/*  UDP                                                                      */
/* ========================================================================= */

/**
 * @brief  Открывает UDP-порт. Повторный вызов с тем же портом возвращает уже
 *         существующий сокет (обработчик не меняется).
 * @param  port       UDP-порт (1..65535)
 * @param  on_receive обработчик входящих датаграмм (NULL - только отправка)
 * @param  user_ctx   контекст пользователя
 * @return указатель на сокет или NULL при ошибке
 */
USB_ETH_UdpSocket_t *USB_ETH_UDP_Bind(uint16_t port, USB_ETH_UdpReceive_t on_receive,
                                      void *user_ctx);

/**
 * @brief  Отправляет одну датаграмму (данные копируются).
 * @param  sock сокет (порт отправителя)
 * @param  ip   IP получателя (порядок байт хоста), USB_ETH_IP4_BROADCAST - всем
 * @param  port порт получателя
 * @param  data данные
 * @param  len  длина (не больше 1472 байт - одна датаграмма без фрагментации)
 * @return HAL_OK; HAL_BUSY - нет памяти; HAL_ERROR - сеть не готова/неверные параметры
 */
HAL_StatusTypeDef USB_ETH_UDP_SendTo(USB_ETH_UdpSocket_t *sock, uint32_t ip, uint16_t port,
                                     const void *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* USB_ETH_H */
