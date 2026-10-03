/**
 ******************************************************************************
 * @file    usb_eth.c
 * @brief   Ядро usb_eth: связка TinyUSB (CDC-NCM) <-> lwIP, режимы HOST
 *          (DHCP-сервер) / CLIENT (DHCP-клиент), отслеживание состояния сети.
 * @author  Mechanic
 * @date    03.10.2026
 * @version 1.1
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include <string.h>

#include "usb_eth.h"
#include "usb_eth_internal.h"

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

/** Очередь входящих кадров между TinyUSB и lwIP (в кадрах). */
#define USB_ETH_RX_QUEUE_LEN      4U

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
static bool s_usb_mounted;        /* выставляется из tud_mount_cb/tud_umount_cb */
static bool s_link_up;            /* состояние, уже применённое к netif */
static bool s_net_up;             /* состояние, уже сообщённое пользователю */
static bool s_net_cb_pending;     /* сообщить текущее состояние новому колбэку */
static USB_ETH_NetCallback_t s_net_cb;
static uint32_t s_rand_state;

static struct pbuf *s_rx_queue[USB_ETH_RX_QUEUE_LEN];
static uint8_t s_rx_head;
static uint8_t s_rx_count;

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
/*  lwIP <-> TinyUSB                                                         */
/* ------------------------------------------------------------------------- */

/**
 * @brief  Отправка кадра lwIP в USB (netif->linkoutput). Ждёт освобождения
 *         передатчика не дольше USB_ETH_TX_TIMEOUT_MS, иначе кадр теряется
 *         (TCP перешлёт его сам).
 * @param  netif интерфейс
 * @param  p     кадр
 * @return ERR_OK - отправлен; ERR_IF - USB не готов; ERR_TIMEOUT - таймаут
 */
static err_t usb_eth_linkoutput(struct netif *netif, struct pbuf *p)
{
    uint32_t start = HAL_GetTick();
    (void)netif;

    for (;;)
    {
        if (!tud_ready())
        {
            s_stats.tx_drop_no_usb++;
            return ERR_IF;
        }
        if (tud_network_can_xmit(p->tot_len))
        {
            /* tud_network_xmit копирует кадр сразу (tud_network_xmit_cb) */
            tud_network_xmit(p, 0U);
            s_stats.tx_frames++;
            return ERR_OK;
        }
        if ((HAL_GetTick() - start) >= USB_ETH_TX_TIMEOUT_MS)
        {
            s_stats.tx_drop_timeout++;
            USB_ETH_LOG(USB_ETH_LOG_CODE_TX_TIMEOUT, 0U, p->tot_len);
            return ERR_TIMEOUT;
        }
        /* Продвигаем USB, чтобы завершилась предыдущая передача. Входящие
         * кадры при этом только ставятся в очередь (tud_network_recv_cb) и
         * не попадают в lwIP рекурсивно. */
        tud_task();
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
 * @brief  Приём кадра от TinyUSB (вызывается из tud_task()). Кадр только
 *         копируется в очередь, в lwIP он уйдёт из USB_ETH_Process().
 * @param  src  данные кадра
 * @param  size длина
 * @return true - кадр принят/отброшен; false - очередь полна, TinyUSB
 *         повторит после tud_network_recv_renew()
 */
bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    if (size == 0U)
    {
        return true;
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
        USB_ETH_LOG(USB_ETH_LOG_CODE_RX_DROP, 0U, size);
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
 * @brief Колбэк TinyUSB: хост сконфигурировал устройство.
 */
void tud_mount_cb(void)
{
    s_usb_mounted = true;
}

/**
 * @brief Колбэк TinyUSB: устройство отключено от хоста.
 */
void tud_umount_cb(void)
{
    s_usb_mounted = false;
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
    while (s_rx_count > 0U)
    {
        struct pbuf *p = s_rx_queue[s_rx_head];
        s_rx_queue[s_rx_head] = NULL;
        s_rx_head = (uint8_t)((s_rx_head + 1U) % USB_ETH_RX_QUEUE_LEN);
        s_rx_count--;

        s_stats.rx_frames++;
        if (s_netif.input(p, &s_netif) != ERR_OK)
        {
            pbuf_free(p);
        }
    }
    tud_network_recv_renew();
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
    if (s_usb_mounted != s_link_up)
    {
        s_link_up = s_usb_mounted;
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

/* ------------------------------------------------------------------------- */
/*  Публичный API                                                            */
/* ------------------------------------------------------------------------- */

HAL_StatusTypeDef USB_ETH_Init(void)
{
    if (usb_eth_in_isr())
    {
        return HAL_ERROR;
    }
    if (s_initialized)
    {
        return HAL_OK;
    }

    usb_eth_make_mac();
    s_rand_state = usb_eth_uid_hash(HAL_GetTick()) | 1U;

    /* --- USB --- */
#if defined(TUP_USBIP_DWC2)
    tud_configure_dwc2_t dwc2_cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
    dwc2_cfg.vbus_sensing = (USB_ETH_VBUS_SENSING != 0U);
    (void)tud_configure(USB_ETH_RHPORT, TUD_CFGID_DWC2, &dwc2_cfg);
#endif
    tusb_rhport_init_t dev_init =
    {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    if (!tusb_init(USB_ETH_RHPORT, &dev_init))
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_INIT_FAIL, 0U, 1);
        return HAL_ERROR;
    }

    /* --- lwIP --- */
    lwip_init();

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
        return HAL_ERROR;
    }
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);   /* link поднимется при подключении USB */

#if (USB_ETH_MODE == USB_ETH_MODE_HOST)
    uint32_t net = USB_ETH_HOST_IP & 0xFFFFFF00UL;
    for (uint32_t i = 0U; i < (uint32_t)s_dhcp_config.num_entry; i++)
    {
        ip4_addr_set_u32(&s_dhcp_entries[i].addr, lwip_htonl(net | (2U + i)));
    }
    if (dhserv_init(&s_dhcp_config) != ERR_OK)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_INIT_FAIL, 0U, 3);
        return HAL_ERROR;
    }
#else
    /* Пока link down, DHCP ждёт и стартует сам при netif_set_link_up() */
    if (dhcp_start(&s_netif) != ERR_OK)
    {
        USB_ETH_LOG(USB_ETH_LOG_CODE_INIT_FAIL, 0U, 3);
        return HAL_ERROR;
    }
#endif

    tud_network_link_state(USB_ETH_RHPORT, true);
    s_initialized = true;
    USB_ETH_LOG(USB_ETH_LOG_CODE_INIT_OK, 0U, USB_ETH_MODE);
    return HAL_OK;
}

void USB_ETH_Process(void)
{
    if (!s_initialized || usb_eth_in_isr())
    {
        return;
    }
    tud_task();
    usb_eth_rx_drain();
    sys_check_timeouts();
    usb_eth_update_state();
}

void USB_ETH_IRQHandler(void)
{
    tusb_int_handler(USB_ETH_RHPORT, true);
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
        return HAL_ERROR;
    }
    *stats = s_stats;
    stats->pbuf_pool_size = (uint16_t)USB_ETH_RX_PBUF_POOL_SIZE;
    stats->pbuf_pool_max_used = (uint16_t)lwip_stats.memp[MEMP_PBUF_POOL]->max;
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
