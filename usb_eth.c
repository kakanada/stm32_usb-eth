/**
 ******************************************************************************
 * @file    usb_eth.c
 * @brief   Ядро usb_eth: связка TinyUSB (CDC-NCM) <-> lwIP, режимы HOST
 *          (DHCP-сервер) / CLIENT (DHCP-клиент), отслеживание состояния сети.
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
#include "usb_dev_internal.h"

#include "tusb.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"

#if (USB_ETH_MODE == USB_ETH_MODE_HOST)
#include "dhserver.h"
#else
#include "lwip/dhcp.h"
#endif

/* ------------------------------------------------------------------------- */
/*  Внутренние константы                                                     */
/* ------------------------------------------------------------------------- */

/** Сколько ждать освобождения USB-передатчика, прежде чем отбросить кадр. */
#define USB_ETH_TX_TIMEOUT_MS     20U

/** Сколько входящих кадров передаётся в lwIP за один USB_Process() (дальше - в следующий). */
#define USB_ETH_RX_BURST          16U

/** Через сколько после подключения USB сообщать, что DHCP не выдал адрес. */
#define USB_ETH_DHCP_WARN_MS      15000U

/** IP MTU интерфейса (кадр Ethernet 1514 = 14 заголовок + 1500). */
#define USB_ETH_IP_MTU            1500U

/** Срок аренды адреса, выдаваемого ПК в режиме HOST, с. */
#define USB_ETH_DHCP_LEASE_S      (24U * 60U * 60U)

/* ------------------------------------------------------------------------- */
/*  Состояние модуля                                                         */
/* ------------------------------------------------------------------------- */

/* MAC сетевого адаптера на стороне ПК. Имя и тип заданы TinyUSB
 * (extern в class/net/net_device.h), MAC самого STM32 отличается от него
 * младшим битом последнего байта. */
uint8_t tud_network_mac_address[6];

static struct netif s_netif;
static bool s_initialized;
static bool s_lwip_started;       /* lwip_init() допустим только один раз */
static bool s_in_process;         /* идёт обслуживание сети - внутри колбэков lwIP */
static bool s_deinit_pending;     /* DeInit вызван из колбэка - выполнить по выходе */
static bool s_deiniting;          /* идёт DeInit (вызываются обработчики пользователя) */
static bool s_link_up;            /* состояние, уже применённое к netif */
static bool s_net_up;             /* состояние, уже сообщённое пользователю */
static bool s_net_cb_pending;     /* сообщить текущее состояние новому колбэку */
static USB_ETH_NetCallback_t s_net_cb;
static uint32_t s_rand_state;

static struct pbuf *s_rx_queue[USB_ETH_RX_QUEUE_LEN];
static uint8_t s_rx_head;
static uint8_t s_rx_count;

/* Исходящие кадры, ждущие свободного буфера USB (ссылки pbuf, без копий). */
static struct pbuf *s_tx_queue[USB_ETH_TX_QUEUE_LEN];
static uint8_t s_tx_head;
static uint8_t s_tx_count;

static USB_ETH_Stats_t s_stats;

#if (USB_ETH_MODE == USB_ETH_MODE_CLIENT)
static uint32_t s_link_up_tick;
static bool s_dhcp_warned;
#else
/* Адреса, которые DHCP-сервер раздаёт подключённому ПК: .2 и .3 той же /24. */
static dhcp_entry_t s_dhcp_entries[] =
{
    { {0}, {0}, USB_ETH_DHCP_LEASE_S },
    { {0}, {0}, USB_ETH_DHCP_LEASE_S },
};

static dhcp_config_t s_dhcp_config =
{
    .router    = { 0 },   /* без шлюза: ПК не пытается пустить интернет через USB */
    .port      = 67,
    .dns       = { 0 },   /* без DNS: ПК не отправляет DNS-запросы на плату */
    .domain    = "usb",
    .num_entry = (int)(sizeof(s_dhcp_entries) / sizeof(s_dhcp_entries[0])),
    .entries   = s_dhcp_entries
};
#endif

/* ------------------------------------------------------------------------- */
/*  Вспомогательные функции                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief  Хэш FNV-1a от уникального ID чипа с "солью".
 * @param  salt начальное значение для получения разных хэшей от одного UID
 * @return 32-битный хэш
 */
static uint32_t usb_eth_uid_hash(uint32_t salt)
{
    uint32_t words[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
    const uint8_t *bytes = (const uint8_t *)words;
    uint32_t hash = 2166136261UL ^ salt;

    for (uint32_t i = 0U; i < sizeof(words); i++)
    {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

/**
 * @brief Заполняет tud_network_mac_address: из USB_ETH_MAC_ADDR либо из UID.
 */
static void usb_eth_make_mac(void)
{
#ifdef USB_ETH_MAC_ADDR
    static const uint8_t fixed_mac[6] = USB_ETH_MAC_ADDR;
    memcpy(tud_network_mac_address, fixed_mac, sizeof(fixed_mac));
#else
    uint32_t h = usb_eth_uid_hash(0U);

    tud_network_mac_address[0] = 0x02U;   /* локально администрируемый, unicast */
    tud_network_mac_address[1] = (uint8_t)(h >> 24);
    tud_network_mac_address[2] = (uint8_t)(h >> 16);
    tud_network_mac_address[3] = (uint8_t)(h >> 8);
    tud_network_mac_address[4] = (uint8_t)h;
    tud_network_mac_address[5] = (uint8_t)(usb_eth_uid_hash(0x5A5AU) & 0xFEU);
#endif
}

/**
 * @brief  Перевод IP из формата lwIP в uint32_t с порядком байт хоста.
 * @param  addr адрес lwIP
 * @return адрес (порядок байт хоста)
 */
static uint32_t usb_eth_ip_from_lwip(const ip4_addr_t *addr)
{
    return lwip_ntohl(ip4_addr_get_u32(addr));
}

/* ------------------------------------------------------------------------- */
/*  Быстрые копирование и контрольная сумма для lwIP (MEMCPY, LWIP_CHKSUM_COPY) */
/* ------------------------------------------------------------------------- */

/** 32-битное слово по выровненному / произвольному адресу (без нарушения aliasing). */
typedef uint32_t __attribute__((may_alias)) usb_eth_w_t;
typedef uint32_t __attribute__((may_alias, aligned(1))) usb_eth_wu_t;

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *usb_eth_memcpy(void *dst, const void *src, size_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if ((((uintptr_t)d | (uintptr_t)s) & 3U) == 0U)
    {
        usb_eth_w_t *dw = (usb_eth_w_t *)d;
        const usb_eth_w_t *sw = (const usb_eth_w_t *)s;

        for (; len >= 16U; len -= 16U)
        {
            uint32_t a = sw[0], b = sw[1], c = sw[2], e = sw[3];
            dw[0] = a;
            dw[1] = b;
            dw[2] = c;
            dw[3] = e;
            dw += 4;
            sw += 4;
        }
        for (; len >= 4U; len -= 4U)
        {
            *dw++ = *sw++;
        }
        d = (uint8_t *)dw;
        s = (const uint8_t *)sw;
    }
    else
    {
        /* Разное выравнивание: слова по произвольному адресу (на ядрах без
         * невыровненного доступа компилятор сам разложит их на байты) */
        for (; len >= 4U; len -= 4U)
        {
            *(usb_eth_wu_t *)d = *(const usb_eth_wu_t *)s;
            d += 4;
            s += 4;
        }
    }
    while (len-- > 0U)
    {
        *d++ = *s++;
    }
    return dst;
}

__attribute__((optimize("no-tree-loop-distribute-patterns")))
uint16_t usb_eth_chksum_copy(void *dst, const void *src, uint16_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint64_t acc = 0U;   /* сумма 32-битных слов: ADDS + ADC на слово, без проверок переноса */
    uint32_t n = len;

    /* Сумма слов по 4 байта, свёрнутая в 16 бит, равна сумме слов по 2 байта (2^16 = 1 по
     * модулю 65535): результат тот же, что у lwip_standard_chksum (порядок памяти) */
    for (; n >= 16U; n -= 16U)
    {
        uint32_t w0 = ((const usb_eth_wu_t *)s)[0];
        uint32_t w1 = ((const usb_eth_wu_t *)s)[1];
        uint32_t w2 = ((const usb_eth_wu_t *)s)[2];
        uint32_t w3 = ((const usb_eth_wu_t *)s)[3];
        ((usb_eth_wu_t *)d)[0] = w0;
        ((usb_eth_wu_t *)d)[1] = w1;
        ((usb_eth_wu_t *)d)[2] = w2;
        ((usb_eth_wu_t *)d)[3] = w3;
        acc += w0;
        acc += w1;
        acc += w2;
        acc += w3;
        s += 16;
        d += 16;
    }
    for (; n >= 4U; n -= 4U)
    {
        uint32_t w = *(const usb_eth_wu_t *)s;
        *(usb_eth_wu_t *)d = w;
        acc += w;
        s += 4;
        d += 4;
    }
    if (n >= 2U)
    {
        d[0] = s[0];
        d[1] = s[1];
        acc += (uint32_t)s[0] | ((uint32_t)s[1] << 8);
        s += 2;
        d += 2;
        n -= 2U;
    }
    if (n != 0U)
    {
        d[0] = s[0];
        acc += s[0];
    }
    acc = (acc & 0xFFFFFFFFU) + (acc >> 32);
    acc = (acc & 0xFFFFU) + (acc >> 16);
    acc = (acc & 0xFFFFU) + (acc >> 16);
    acc = (acc & 0xFFFFU) + (acc >> 16);
    return (uint16_t)acc;
}

/* ------------------------------------------------------------------------- */
/*  lwIP <-> TinyUSB                                                         */
/* ------------------------------------------------------------------------- */

/**
 * @brief  Есть ли связь с ПК для отправки кадров.
 * @return true - сеть в составе USB и ПК её сконфигурировал
 */
static bool usb_eth_tx_link_ok(void)
{
    /* Не только tud_ready(): при программном переподключении TinyUSB ещё
     * считает себя подключённым, а кадр ушёл бы в никуда */
    return usb_dev_func_mounted(USB_DEV_FUNC_ETH) && tud_ready();
}

/**
 * @brief  Снимает первый кадр с очереди отправки.
 * @return кадр (ссылка очереди переходит вызывающему)
 */
static struct pbuf *usb_eth_tx_pop(void)
{
    struct pbuf *p = s_tx_queue[s_tx_head];
    s_tx_queue[s_tx_head] = NULL;
    s_tx_head = (uint8_t)((s_tx_head + 1U) % USB_ETH_TX_QUEUE_LEN);
    s_tx_count--;
    return p;
}

/**
 * @brief Сбрасывает очередь отправки (USB отключён или сеть выключается).
 * @param count_drop true - считать сброшенные кадры потерянными
 */
static void usb_eth_tx_clear(bool count_drop)
{
    while (s_tx_count > 0U)
    {
        pbuf_free(usb_eth_tx_pop());
        if (count_drop)
        {
            s_stats.tx_drop_no_usb++;
        }
    }
}

/**
 * @brief Передаёт кадры из очереди отправки в свободные буферы USB (NTB),
 *        сколько поместится. Не ждёт: остаток уйдёт при следующем вызове.
 */
static void usb_eth_tx_drain(void)
{
    if (s_tx_count == 0U)
    {
        return;
    }
    if (!usb_eth_tx_link_ok())
    {
        usb_eth_tx_clear(true);
        return;
    }
    while ((s_tx_count > 0U) && tud_network_can_xmit(s_tx_queue[s_tx_head]->tot_len))
    {
        struct pbuf *p = usb_eth_tx_pop();
        /* tud_network_xmit копирует кадр сразу (tud_network_xmit_cb) */
        tud_network_xmit(p, 0U);
        s_stats.tx_frames++;
        pbuf_free(p);   /* ссылка очереди; TCP хранит сегмент до подтверждения сам */
    }
}

/**
 * @brief  Ставит кадр в очередь отправки без копирования (pbuf_ref). Пока
 *         кадр в очереди, lwIP его не меняет (tcp_output_segment_busy).
 * @param  p кадр
 * @return true - поставлен; false - очередь полна
 */
static bool usb_eth_tx_enqueue(struct pbuf *p)
{
    if (s_tx_count >= USB_ETH_TX_QUEUE_LEN)
    {
        return false;
    }
    pbuf_ref(p);
    s_tx_queue[(uint8_t)((s_tx_head + s_tx_count) % USB_ETH_TX_QUEUE_LEN)] = p;
    s_tx_count++;
    if (s_tx_count > s_stats.tx_queue_max_used)
    {
        s_stats.tx_queue_max_used = s_tx_count;
    }
    return true;
}

/**
 * @brief  Кадр не отправлен: USB не подключён.
 * @param  p кадр
 * @return ERR_IF
 */
static err_t usb_eth_tx_no_usb(const struct pbuf *p)
{
    (void)p;
    usb_eth_tx_clear(true);
    s_stats.tx_drop_no_usb++;
    USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_TX_NO_USB, 0U, p->tot_len);
    return ERR_IF;
}

/**
 * @brief  Отправка кадра lwIP в USB (netif->linkoutput). Процессор не ждёт
 *         USB: кадр сразу копируется в свободный буфер USB (NTB) либо
 *         ставится в очередь и уходит из USB_Process(). Ждёт (не дольше
 *         USB_ETH_TX_TIMEOUT_MS) только при полной очереди, по таймауту кадр
 *         теряется (TCP перешлёт его сам).
 * @param  netif интерфейс
 * @param  p     кадр
 * @return ERR_OK - отправлен или в очереди; ERR_IF - USB не готов;
 *         ERR_TIMEOUT - таймаут
 */
static err_t usb_eth_linkoutput(struct netif *netif, struct pbuf *p)
{
    (void)netif;

    if (!usb_eth_tx_link_ok())
    {
        return usb_eth_tx_no_usb(p);
    }
    /* Сначала очередь - кадры не обгоняют друг друга */
    usb_eth_tx_drain();
    if ((s_tx_count == 0U) && tud_network_can_xmit(p->tot_len))
    {
        tud_network_xmit(p, 0U);
        s_stats.tx_frames++;
        return ERR_OK;
    }
    if (usb_eth_tx_enqueue(p))
    {
        return ERR_OK;
    }

    /* Очередь полна: ждём, пока USB заберёт кадры */
    uint32_t start = HAL_GetTick();
    for (;;)
    {
        /* Продвигаем USB, чтобы завершилась предыдущая передача. Входящие
         * кадры при этом только ставятся в очередь (tud_network_recv_cb) и
         * не попадают в lwIP рекурсивно. */
        tud_task();
        if (!usb_eth_tx_link_ok())
        {
            return usb_eth_tx_no_usb(p);
        }
        usb_eth_tx_drain();
        if (usb_eth_tx_enqueue(p))
        {
            return ERR_OK;
        }
        if ((HAL_GetTick() - start) >= USB_ETH_TX_TIMEOUT_MS)
        {
            s_stats.tx_drop_timeout++;
            USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_TX_TIMEOUT, 0U, p->tot_len);
            return ERR_TIMEOUT;
        }
    }
}

/**
 * @brief  Инициализация netif (колбэк netif_add).
 * @param  netif интерфейс
 * @return ERR_OK
 */
static err_t usb_eth_netif_init(struct netif *netif)
{
    netif->mtu = USB_ETH_IP_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    netif->name[0] = 'u';
    netif->name[1] = 'e';
    netif->linkoutput = usb_eth_linkoutput;
    netif->output = etharp_output;
    netif->hwaddr_len = 6U;
    memcpy(netif->hwaddr, tud_network_mac_address, 6U);
    netif->hwaddr[5] ^= 0x01U;   /* MAC STM32 отличается от MAC адаптера ПК */
#if LWIP_NETIF_HOSTNAME
    netif->hostname = USB_ETH_HOSTNAME;
#endif
    return ERR_OK;
}

/**
 * @brief  Приём кадра от ПК (tud_network_recv_cb через usb_dev). Кадр только
 *         копируется в очередь, в lwIP он уйдёт из USB_Process().
 * @param  src  данные кадра
 * @param  size длина
 * @return true - кадр принят/отброшен; false - очередь полна, TinyUSB
 *         повторит после tud_network_recv_renew()
 */
static bool usb_eth_recv(const uint8_t *src, uint16_t size)
{
    if ((size == 0U) || !s_initialized)
    {
        return true;   /* сеть выключена, а ПК ещё не узнал об этом - кадр не нужен */
    }
    if (s_rx_count >= USB_ETH_RX_QUEUE_LEN)
    {
        /* Кадр остаётся в TinyUSB, новый приём не запускается - ПК ждёт (NAK) */
        s_stats.rx_backpressure++;
        return false;
    }

    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
    if (p == NULL)
    {
        /* Нет памяти - кадр теряется (не задерживаем приём навсегда) */
        s_stats.rx_drop_no_pbuf++;
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_RX_DROP, 0U, size);
    }
    else
    {
        (void)pbuf_take(p, src, size);
        s_rx_queue[(uint8_t)((s_rx_head + s_rx_count) % USB_ETH_RX_QUEUE_LEN)] = p;
        s_rx_count++;
    }

    /* Запросить следующий кадр (TinyUSB защищён от рекурсии сам) */
    tud_network_recv_renew();
    return true;
}

/**
 * @brief  Копирование исходящего кадра в буфер TinyUSB.
 * @param  dst буфер TinyUSB
 * @param  ref pbuf, переданный в tud_network_xmit()
 * @param  arg не используется
 * @return число скопированных байт
 */
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
    struct pbuf *p = (struct pbuf *)ref;
    (void)arg;
    return pbuf_copy_partial(p, dst, p->tot_len, 0U);
}

/**
 * @brief Инициализация сетевого класса TinyUSB (обязательный колбэк, не нужен).
 */
void tud_network_init_cb(void)
{
}

/**
 * @brief  Время в мс для таймеров lwIP.
 * @return HAL_GetTick()
 */
uint32_t sys_now(void)
{
    return HAL_GetTick();
}

/* ------------------------------------------------------------------------- */
/*  Обслуживание                                                             */
/* ------------------------------------------------------------------------- */

/**
 * @brief Передаёт накопленные входящие кадры в lwIP.
 */
static void usb_eth_rx_drain(void)
{
    uint32_t done = 0U;

    /* Очередь короткая (экономия ОЗУ), поэтому после опустошения она сразу пополняется из
     * принятого USB-буфера - за один проход обрабатывается до USB_ETH_RX_BURST кадров */
    do
    {
        while (s_rx_count > 0U)
        {
            struct pbuf *p = s_rx_queue[s_rx_head];
            s_rx_queue[s_rx_head] = NULL;
            s_rx_head = (uint8_t)((s_rx_head + 1U) % USB_ETH_RX_QUEUE_LEN);
            s_rx_count--;
            done++;

            s_stats.rx_frames++;
            err_t err = s_netif.input(p, &s_netif);
            if (err != ERR_OK)
            {
                USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_RX_INPUT_FAIL, 0U, err);
                pbuf_free(p);
            }
        }
        tud_network_recv_renew();
    } while ((s_rx_count > 0U) && (done < USB_ETH_RX_BURST));
}

/**
 * @brief  Готов ли IP-уровень (в режиме CLIENT - получен ли адрес по DHCP).
 * @return true - есть рабочий IP
 */
static bool usb_eth_ip_ready(void)
{
#if (USB_ETH_MODE == USB_ETH_MODE_CLIENT)
    return (dhcp_supplied_address(&s_netif) != 0U);
#else
    return true;
#endif
}

/**
 * @brief Отслеживает подключение USB и готовность IP, вызывает колбэк сети.
 */
static void usb_eth_update_state(void)
{
    bool mounted = usb_dev_func_mounted(USB_DEV_FUNC_ETH);

    if (mounted != s_link_up)
    {
        s_link_up = mounted;
        if (s_link_up)
        {
            USB_ETH_LOG(USB_ETH_LOG_CODE_USB_MOUNTED, 0U, 0);
            netif_set_link_up(&s_netif);   /* в режиме CLIENT запускает DHCP */
#if (USB_ETH_MODE == USB_ETH_MODE_CLIENT)
            s_link_up_tick = HAL_GetTick();
            s_dhcp_warned = false;
#endif
        }
        else
        {
            USB_ETH_LOG(USB_ETH_LOG_CODE_USB_UNMOUNTED, 0U, 0);
            netif_set_link_down(&s_netif);
        }
    }

#if (USB_ETH_MODE == USB_ETH_MODE_CLIENT)
    if (s_link_up && !s_dhcp_warned && !usb_eth_ip_ready() &&
        ((HAL_GetTick() - s_link_up_tick) >= USB_ETH_DHCP_WARN_MS))
    {
        s_dhcp_warned = true;
        USB_ETH_LOG(USB_ETH_LOG_CODE_DHCP_TIMEOUT, 0U, USB_ETH_DHCP_WARN_MS);
    }
#endif

    bool net_up = s_link_up && usb_eth_ip_ready();
    if ((net_up == s_net_up) && !s_net_cb_pending)
    {
        return;
    }
    bool changed = (net_up != s_net_up);
    s_net_up = net_up;
    s_net_cb_pending = false;

    if (changed)
    {
        if (net_up)
        {
            USB_ETH_LOG(USB_ETH_LOG_CODE_NET_UP, 0U, USB_ETH_GetIp());
        }
        else
        {
            USB_ETH_LOG(USB_ETH_LOG_CODE_NET_DOWN, 0U, 0);
            usb_eth_sock_link_lost();
        }
    }
    if ((s_net_cb != NULL) && (changed || net_up))
    {
        s_net_cb(net_up, USB_ETH_GetIp());
    }
}

/**
 * @brief Выключает сеть немедленно (вне колбэков lwIP): закрывает сокеты,
 *        останавливает DHCP, удаляет netif, убирает сеть из состава USB.
 */
static void usb_eth_do_deinit(void)
{
    s_deinit_pending = false;
    s_deiniting = true;   /* Init из обработчиков ниже вернёт HAL_ERROR */

    usb_eth_sock_deinit();
    if (s_net_up)
    {
        s_net_up = false;
        USB_ETH_LOG(USB_ETH_LOG_CODE_NET_DOWN, 0U, 0);
        if (s_net_cb != NULL)
        {
            s_net_cb(false, 0U);
        }
    }

#if (USB_ETH_MODE == USB_ETH_MODE_HOST)
    dhserv_free();
#else
    dhcp_release_and_stop(&s_netif);
    dhcp_cleanup(&s_netif);
#endif
    netif_set_down(&s_netif);
    netif_remove(&s_netif);

    while (s_rx_count > 0U)
    {
        pbuf_free(s_rx_queue[s_rx_head]);
        s_rx_queue[s_rx_head] = NULL;
        s_rx_head = (uint8_t)((s_rx_head + 1U) % USB_ETH_RX_QUEUE_LEN);
        s_rx_count--;
    }
    usb_eth_tx_clear(false);
    s_link_up = false;
    s_net_cb_pending = false;
    s_initialized = false;
    s_deiniting = false;
    (void)usb_dev_set_func(USB_DEV_FUNC_ETH, false);
    USB_ETH_LOG(USB_DEV_LOG_CODE_ETH_DEINIT, 0U, 0);
}

#if USB_DEV_LOG_ENABLE
/** Отказы памяти lwIP, уже записанные в лог: по пулам и куча. */
static uint16_t s_memp_err_seen[MEMP_MAX];
static uint16_t s_mem_err_seen;

/**
 * @brief Пишет в лог новые отказы выделения памяти внутри lwIP (пулы и куча):
 *        так видны и потери, которые lwIP обрабатывает молча (ARP, ICMP,
 *        входящие сегменты...).
 */
static void usb_eth_mem_check(void)
{
    for (uint32_t i = 0U; i < (uint32_t)MEMP_MAX; i++)
    {
        const struct stats_mem *st = lwip_stats.memp[i];
        if ((st != NULL) && (st->err != s_memp_err_seen[i]))
        {
            s_memp_err_seen[i] = st->err;
            USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_LWIP_MEM_ERR, i, st->err);
        }
    }
    if (lwip_stats.mem.err != s_mem_err_seen)
    {
        s_mem_err_seen = lwip_stats.mem.err;
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_LWIP_MEM_ERR, 0xFFFFU, lwip_stats.mem.err);
    }
}

void usb_eth_lwip_assert(void)
{
    USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_LWIP_ASSERT, 0U, (uintptr_t)__builtin_return_address(0));
}

void usb_eth_lwip_arg_error(void)
{
    USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_LWIP_ARG_ERR, 0U, (uintptr_t)__builtin_return_address(0));
}
#else
#define usb_eth_mem_check() ((void)0)
#endif /* USB_DEV_LOG_ENABLE */

/**
 * @brief Обслуживание сети из USB_Process(): кадры в lwIP, таймеры, состояние
 *        сети, отложенный DeInit.
 */
static void usb_eth_process(void)
{
    if (!s_initialized)
    {
        return;
    }
    s_in_process = true;
    usb_eth_tx_drain();     /* буферы USB, освободившиеся в tud_task() */
    usb_eth_rx_drain();
    sys_check_timeouts();
    usb_eth_update_state();
    usb_eth_sock_output();  /* отложенная отправка TCP - полными сегментами */
    usb_eth_tx_drain();
    s_in_process = false;
    usb_eth_mem_check();

    if (s_deinit_pending)
    {
        usb_eth_do_deinit();
    }
}

/** Точки входа сети для ядра usb_dev (регистрируются в USB_ETH_Init()). */
static const usb_dev_eth_hooks_t s_eth_hooks =
{
    .process = usb_eth_process,
    .recv    = usb_eth_recv
};

/* ------------------------------------------------------------------------- */
/*  Публичный API                                                            */
/* ------------------------------------------------------------------------- */

HAL_StatusTypeDef USB_ETH_Init(void)
{
    if (usb_eth_in_isr())
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_API_ERROR, USB_DEV_API_ETH_INIT, USB_DEV_API_ERR_ISR);
        return HAL_ERROR;
    }
    if (s_deiniting)
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_API_ERROR, USB_DEV_API_ETH_INIT, USB_DEV_API_ERR_BUSY_DEINIT);
        return HAL_ERROR;
    }
    if (s_initialized)
    {
        s_deinit_pending = false;   /* DeInit+Init внутри одного колбэка - сеть остаётся */
        return HAL_OK;
    }

    usb_eth_make_mac();   /* до подключения USB: MAC отдаётся ПК в дескрипторах */
    usb_dev_set_eth_hooks(&s_eth_hooks);
    if (usb_dev_set_func(USB_DEV_FUNC_ETH, true) != HAL_OK)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_INIT_FAIL, 0U, 1);
        return HAL_ERROR;
    }

    /* --- lwIP: инициализация стека - один раз, netif - при каждом Init --- */
    if (!s_lwip_started)
    {
        s_rand_state = usb_eth_uid_hash(HAL_GetTick()) | 1U;
        lwip_init();
        s_lwip_started = true;
    }

    ip4_addr_t ip;
    ip4_addr_t mask;
    ip4_addr_t gw;
#if (USB_ETH_MODE == USB_ETH_MODE_HOST)
    ip4_addr_set_u32(&ip, lwip_htonl(USB_ETH_HOST_IP));
    ip4_addr_set_u32(&mask, lwip_htonl(USB_ETH_IP4(255, 255, 255, 0)));
#else
    ip4_addr_set_zero(&ip);
    ip4_addr_set_zero(&mask);
#endif
    ip4_addr_set_zero(&gw);

    if (netif_add(&s_netif, &ip, &mask, &gw, NULL, usb_eth_netif_init, ethernet_input) == NULL)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_INIT_FAIL, 0U, 2);
        (void)usb_dev_set_func(USB_DEV_FUNC_ETH, false);
        return HAL_ERROR;
    }
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);   /* link поднимется при подключении USB */

#if (USB_ETH_MODE == USB_ETH_MODE_HOST)
    uint32_t net = USB_ETH_HOST_IP & 0xFFFFFF00UL;
    for (uint32_t i = 0U; i < (uint32_t)s_dhcp_config.num_entry; i++)
    {
        memset(s_dhcp_entries[i].mac, 0, sizeof(s_dhcp_entries[i].mac));
        ip4_addr_set_u32(&s_dhcp_entries[i].addr, lwip_htonl(net | (2U + i)));
    }
    err_t dhcp_err = dhserv_init(&s_dhcp_config);
#else
    /* Пока link down, DHCP ждёт и стартует сам при netif_set_link_up() */
    err_t dhcp_err = dhcp_start(&s_netif);
#endif
    if (dhcp_err != ERR_OK)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_INIT_FAIL, 0U, 3);
        netif_remove(&s_netif);
        (void)usb_dev_set_func(USB_DEV_FUNC_ETH, false);
        return HAL_ERROR;
    }

    s_initialized = true;
    USB_ETH_LOG(USB_ETH_LOG_CODE_INIT_OK, 0U, USB_ETH_MODE);
    return HAL_OK;
}

HAL_StatusTypeDef USB_ETH_DeInit(void)
{
    if (usb_eth_in_isr())
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_API_ERROR, USB_DEV_API_ETH_DEINIT, USB_DEV_API_ERR_ISR);
        return HAL_ERROR;
    }
    if (!s_initialized || s_deiniting)
    {
        return HAL_OK;
    }
    if (s_in_process)
    {
        s_deinit_pending = true;   /* внутри колбэка lwIP удалять pcb и netif нельзя */
        return HAL_OK;
    }
    usb_eth_do_deinit();
    return HAL_OK;
}

void USB_ETH_SetNetCallback(USB_ETH_NetCallback_t cb)
{
    s_net_cb = cb;
    s_net_cb_pending = (cb != NULL) && s_net_up;
}

bool USB_ETH_IsNetUp(void)
{
    return s_net_up;
}

uint32_t USB_ETH_GetIp(void)
{
    if (!s_initialized || !s_link_up || !usb_eth_ip_ready())
    {
        return 0U;
    }
    return usb_eth_ip_from_lwip(netif_ip4_addr(&s_netif));
}

HAL_StatusTypeDef USB_ETH_GetStats(USB_ETH_Stats_t *stats)
{
    if (stats == NULL)
    {
        USB_ETH_LOG_ERR(USB_ETH_LOG_CODE_API_ERROR, USB_DEV_API_ETH_GET_STATS, USB_DEV_API_ERR_PARAM);
        return HAL_ERROR;
    }
    *stats = s_stats;
    stats->pbuf_pool_size = (uint16_t)USB_ETH_RX_PBUF_POOL_SIZE;
    stats->pbuf_pool_max_used = (uint16_t)lwip_stats.memp[MEMP_PBUF_POOL]->max;
    stats->tx_queue_len = (uint16_t)USB_ETH_TX_QUEUE_LEN;
    stats->heap_size = (uint16_t)USB_ETH_HEAP_SIZE;
    stats->heap_max_used = (uint16_t)lwip_stats.mem.max;
    return HAL_OK;
}

/* ------------------------------------------------------------------------- */
/*  Внутренний API для остальных модулей                                     */
/* ------------------------------------------------------------------------- */

bool usb_eth_is_ready(void)
{
    return s_initialized;
}

uint32_t usb_eth_rand(void)
{
    /* xorshift32, зерно - UID чипа: у разных плат разные DHCP xid и т.п. */
    uint32_t x = s_rand_state ^ HAL_GetTick();
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rand_state = (x != 0U) ? x : 0x2545F491UL;
    return s_rand_state;
}
