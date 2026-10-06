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
/*  Логирование: ограничение частоты и запрет повторного входа               */
/* ------------------------------------------------------------------------- */

#if USB_DEV_LOG_ENABLE
#include "logger.h"

/** Сколько разных кодов одновременно отслеживает ограничитель частоты. */
#define USB_DEV_LOG_SLOTS  16U

typedef struct
{
    uint32_t last_tick;    /* время последней записи этого кода */
    uint16_t code;         /* 0 - слот свободен */
    uint16_t suppressed;   /* пропущено записей с последней */
    bool     has_tick;     /* last_tick действителен */
} usb_dev_log_slot_t;

static usb_dev_log_slot_t s_log_slots[USB_DEV_LOG_SLOTS];
static volatile bool s_log_active;      /* идёт LOGGER_Log() из библиотеки */
static volatile bool s_log_suppressed;  /* есть подавленные записи */

/**
 * @brief  Слот кода: найденный или свободный (вызывать с запретом прерываний).
 * @param  code код лога
 * @param  now  текущее время
 * @return слот или NULL (все заняты - ограничения для кода нет)
 */
static usb_dev_log_slot_t *usb_dev_log_slot(uint16_t code, uint32_t now)
{
    usb_dev_log_slot_t *free_slot = NULL;
    for (uint32_t i = 0U; i < USB_DEV_LOG_SLOTS; i++)
    {
        usb_dev_log_slot_t *s = &s_log_slots[i];
        if (s->code == code)
        {
            return s;
        }
        /* Свободен или давно не нужен: интервал истёк, долгов нет */
        bool expired = (s->suppressed == 0U) &&
                       (!s->has_tick || ((now - s->last_tick) >= USB_DEV_LOG_MIN_INTERVAL_MS));
        if ((free_slot == NULL) && ((s->code == 0U) || expired))
        {
            free_slot = s;
        }
    }
    if (free_slot != NULL)
    {
        free_slot->code = code;
        free_slot->suppressed = 0U;
        free_slot->has_tick = false;
    }
    return free_slot;
}

void usb_dev_log(uint16_t code, uint16_t source_id, int32_t value, bool limited)
{
    uint32_t now = HAL_GetTick();
    uint16_t owed = 0U;
    bool emit;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    bool busy = s_log_active;
    usb_dev_log_slot_t *slot = (limited || busy) ? usb_dev_log_slot(code, now) : NULL;
    emit = !busy;
    if (emit && limited && (slot != NULL) && slot->has_tick &&
        ((now - slot->last_tick) < USB_DEV_LOG_MIN_INTERVAL_MS))
    {
        emit = false;
    }
    if (!emit)
    {
        if ((slot != NULL) && (slot->suppressed < 0xFFFFU))
        {
            slot->suppressed++;
            s_log_suppressed = true;
        }
    }
    else
    {
        s_log_active = true;
        if (slot != NULL)
        {
            slot->last_tick = now;
            slot->has_tick = true;
            owed = slot->suppressed;
            slot->suppressed = 0U;
        }
    }
    __set_PRIMASK(primask);

    if (!emit)
    {
        return;
    }
    if (owed != 0U)
    {
        LOGGER_Log(USB_DEV_LOG_CODE_LOG_SUPPRESSED, code, (int32_t)owed);
    }
    LOGGER_Log(code, source_id, value);
    s_log_active = false;
}

/**
 * @brief Выводит счётчики подавленных записей, по которым новых записей не
 *        было (ошибка прекратилась) - из USB_Process().
 */
static void usb_dev_log_poll(void)
{
    if (!s_log_suppressed || s_log_active)
    {
        return;
    }
    uint32_t now = HAL_GetTick();
    bool left = false;
    for (uint32_t i = 0U; i < USB_DEV_LOG_SLOTS; i++)
    {
        usb_dev_log_slot_t *s = &s_log_slots[i];
        uint16_t owed = 0U;
        uint16_t code = 0U;

        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        if (s->suppressed != 0U)
        {
            if (!s->has_tick || ((now - s->last_tick) >= USB_DEV_LOG_MIN_INTERVAL_MS))
            {
                owed = s->suppressed;
                code = s->code;
                s->suppressed = 0U;
                s->last_tick = now;
                s->has_tick = true;
                s_log_active = true;
            }
            else
            {
                left = true;
            }
        }
        __set_PRIMASK(primask);

        if (owed != 0U)
        {
            LOGGER_Log(USB_DEV_LOG_CODE_LOG_SUPPRESSED, code, (int32_t)owed);
            s_log_active = false;
        }
    }
    s_log_suppressed = left;
}

/**
 * @brief Вызывается TinyUSB при срабатывании внутренней проверки (TU_ASSERT,
 *        через CFG_TUSB_DEBUG_BREAKPOINT в tusb_config.h). Пишет в лог адрес
 *        места ошибки (искать в .map/.elf) и, как TinyUSB по умолчанию,
 *        останавливается под отладчиком. Может вызываться из прерывания.
 */
void usb_dev_tusb_assert(void)
{
    USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_TUSB_ASSERT, 0U, (uintptr_t)__builtin_return_address(0));
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_MAIN__) || \
    defined(__ARM_ARCH_8_1M_MAIN__)
    if (((*(volatile uint32_t *)0xE000EDF0UL) & 1UL) != 0U)   /* DHCSR.C_DEBUGEN */
    {
        __asm volatile ("bkpt #0");
    }
#endif
}
#else
#define usb_dev_log_poll() ((void)0)
#endif /* USB_DEV_LOG_ENABLE */

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
    if (!tud_disconnect())
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_USB_CONNECT_FAIL, 0U, 0);
    }
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
        if (!tud_connect())
        {
            USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_USB_CONNECT_FAIL, 0U, 1);
        }
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
    if (!tud_disconnect())
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_USB_CONNECT_FAIL, 0U, 0);
    }
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
    if (usb_dev_in_isr())
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_PROCESS, USB_DEV_API_ERR_ISR);
        return;
    }
    if (!s_started)
    {
        return;   /* не ошибка: Init ещё не вызывался */
    }
    tud_task();
    usb_dev_update_funcs();
    usb_dev_log_link();
    if (s_eth != NULL)
    {
        s_eth->process();
    }
    usb_com_process();
    usb_dev_log_poll();
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
