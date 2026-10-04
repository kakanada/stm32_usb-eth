/**
 ******************************************************************************
 * @file    usb_dev.c
 * @brief   Общая часть библиотеки: запуск TinyUSB, набор устройств в составе
 *          USB (сеть / COM / оба) и программное переподключение к ПК при его
 *          смене, обслуживание модулей из USB_Process().
 * @author  Mechanic
 * @date    03.10.2026
 * @version 2.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include "usb_dev.h"
#include "usb_dev_internal.h"

#include "tusb.h"

#if defined(TUP_USBIP_DWC2)
#include "portable/synopsys/dwc2/dwc2_common.h"   /* число конечных точек контроллера */
#endif

/* ------------------------------------------------------------------------- */
/*  Внутренние константы                                                     */
/* ------------------------------------------------------------------------- */

/** Конечных точек (с EP0) нужно для сети и COM одновременно: у каждого
 *  устройства IN-точка данных и IN-точка уведомлений, итого 4 IN + EP0. */
#define USB_DEV_EP_COMPOSITE  5U

/* ------------------------------------------------------------------------- */
/*  Состояние модуля                                                         */
/* ------------------------------------------------------------------------- */

static bool s_started;           /* TinyUSB запущен (tusb_init выполнен) */
static uint8_t s_req_funcs;      /* набор, запрошенный Init/DeInit */
static uint8_t s_active_funcs;   /* набор в текущих дескрипторах */
static bool s_connected;         /* плата видна ПК (подтяжка D+ включена) */
static bool s_reenum_wait;       /* идёт пауза переподключения */
static uint32_t s_reenum_tick;   /* начало паузы */
static volatile bool s_mounted;  /* ПК сконфигурировал устройство */
static volatile bool s_suspended;/* шина в режиме сна: ПК спит или кабель вынут */
static bool s_was_online;        /* последнее записанное в лог состояние связи с ПК */
static const usb_dev_eth_hooks_t *s_eth;   /* NULL - сеть ни разу не включалась */

/* ------------------------------------------------------------------------- */
/*  Внутренние функции                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief  Запускает TinyUSB (один раз) и сразу отключает плату от ПК:
 *         подключение произойдёт в USB_Process(), когда набор устройств
 *         известен - при Init обоих до главного цикла ПК увидит плату
 *         один раз и сразу целиком.
 * @return true - успех
 */
static bool usb_dev_start(void)
{
    if (s_started)
    {
        return true;
    }
#if defined(TUP_USBIP_DWC2)
    tud_configure_dwc2_t dwc2_cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
    dwc2_cfg.vbus_sensing = (USB_DEV_VBUS_SENSING != 0U);
    (void)tud_configure(USB_DEV_RHPORT, TUD_CFGID_DWC2, &dwc2_cfg);
#endif
    usb_dev_desc_build(0U);
    tusb_rhport_init_t dev_init =
    {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    if (!tusb_init(USB_DEV_RHPORT, &dev_init))
    {
        USB_DEV_LOG(USB_DEV_LOG_CODE_USB_START_FAIL, 0U, 0);
        return false;
    }
    (void)tud_disconnect();
    s_connected = false;
    s_started = true;
    return true;
}

/**
 * @brief Применяет запрошенный набор устройств: дескрипторы и подключение.
 */
static void usb_dev_apply(void)
{
    s_active_funcs = s_req_funcs;
    usb_dev_desc_build(s_active_funcs);
    if (s_active_funcs != 0U)
    {
        (void)tud_connect();
        s_connected = true;
    }
}

/**
 * @brief Конечный автомат смены набора устройств: отключиться от ПК,
 *        выждать USB_DEV_REENUM_MS, подключиться с новыми дескрипторами.
 */
static void usb_dev_update_funcs(void)
{
    if (s_reenum_wait)
    {
        if ((HAL_GetTick() - s_reenum_tick) >= USB_DEV_REENUM_MS)
        {
            s_reenum_wait = false;
            usb_dev_apply();
        }
        return;
    }
    if (s_req_funcs == s_active_funcs)
    {
        return;
    }
    if (!s_connected)
    {
        usb_dev_apply();   /* ПК ещё не видел плату - пауза не нужна */
        return;
    }
    USB_DEV_LOG(USB_DEV_LOG_CODE_REENUM, 0U, s_req_funcs);
    (void)tud_disconnect();
    s_connected = false;
    s_mounted = false;     /* TinyUSB узнает об отключении только при следующем сбросе шины */
    s_reenum_wait = true;
    s_reenum_tick = HAL_GetTick();
}

/* ------------------------------------------------------------------------- */
/*  Колбэки TinyUSB                                                          */
/* ------------------------------------------------------------------------- */

/**
 * @brief Колбэк TinyUSB: ПК сконфигурировал устройство.
 */
void tud_mount_cb(void)
{
    s_mounted = true;
    s_suspended = false;
}

/**
 * @brief Колбэк TinyUSB: устройство отключено от ПК (только при VBUS sensing).
 */
void tud_umount_cb(void)
{
    s_mounted = false;
}

/**
 * @brief Колбэк TinyUSB: шина уснула. Без VBUS sensing так же выглядит и
 *        вынутый кабель, поэтому сон считается отключением от ПК.
 * @param remote_wakeup_en разрешено ли будить ПК (не используется)
 */
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    s_suspended = true;
}

/**
 * @brief Колбэк TinyUSB: шина проснулась.
 */
void tud_resume_cb(void)
{
    s_suspended = false;
}

/**
 * @brief Записывает в лог подключение/отключение от ПК (при смене состояния).
 */
static void usb_dev_log_link(void)
{
    bool online = usb_dev_func_mounted(USB_DEV_FUNC_ETH | USB_DEV_FUNC_COM);
    if (online != s_was_online)
    {
        s_was_online = online;
        USB_DEV_LOG(online ? USB_DEV_LOG_CODE_USB_CONNECTED : USB_DEV_LOG_CODE_USB_DISCONNECTED,
                    0U, s_active_funcs);
    }
}

/**
 * @brief  Колбэк TinyUSB: кадр от ПК (вызывается из tud_task()). Здесь, а не в
 *         usb_eth.c, - чтобы драйвер NCM не тянул lwIP в прошивку без сети.
 * @param  src  данные кадра
 * @param  size длина
 * @return true - кадр принят/отброшен; false - повторить позже
 */
bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    if (s_eth == NULL)
    {
        return true;
    }
    return s_eth->recv(src, size);
}

/* ------------------------------------------------------------------------- */
/*  Публичный API                                                            */
/* ------------------------------------------------------------------------- */

void USB_Process(void)
{
    if (!s_started || usb_dev_in_isr())
    {
        return;
    }
    tud_task();
    usb_dev_update_funcs();
    usb_dev_log_link();
    if (s_eth != NULL)
    {
        s_eth->process();
    }
    usb_com_process();
}

void USB_IRQHandler(void)
{
    if (s_started)
    {
        tusb_int_handler(USB_DEV_RHPORT, true);
    }
}

bool USB_IsCompositeSupported(void)
{
#if defined(TUP_USBIP_DWC2)
    if (USB_DEV_RHPORT >= TU_ARRAY_SIZE(_dwc2_controller))
    {
        return false;
    }
    return (_dwc2_controller[USB_DEV_RHPORT].ep_count >= USB_DEV_EP_COMPOSITE);
#else
    return false;   /* USB FS device (F0/F1/G4/L4...): не проверено - только одно устройство */
#endif
}

/* ------------------------------------------------------------------------- */
/*  Внутренний API для модулей                                               */
/* ------------------------------------------------------------------------- */

HAL_StatusTypeDef usb_dev_set_func(uint8_t func, bool on)
{
    uint8_t funcs = on ? (uint8_t)(s_req_funcs | func) : (uint8_t)(s_req_funcs & (uint8_t)~func);

    if ((funcs == (USB_DEV_FUNC_ETH | USB_DEV_FUNC_COM)) && !USB_IsCompositeSupported())
    {
        USB_DEV_LOG(USB_DEV_LOG_CODE_INIT_REFUSED, func, 0);
        return HAL_ERROR;
    }
    if (on && !usb_dev_start())
    {
        return HAL_ERROR;
    }
    s_req_funcs = funcs;
    return HAL_OK;
}

void usb_dev_set_eth_hooks(const usb_dev_eth_hooks_t *hooks)
{
    s_eth = hooks;
}

bool usb_dev_func_mounted(uint8_t func)
{
    return s_mounted && !s_suspended && s_connected && !s_reenum_wait &&
           ((s_active_funcs & func) != 0U);
}
