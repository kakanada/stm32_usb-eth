/**
 ******************************************************************************
 * @file    usb_com.c
 * @brief   Виртуальный COM-порт (CDC-ACM) поверх TinyUSB: включение/выключение
 *          в составе USB, передача без ожидания, приём через обработчик или
 *          чтением из буфера.
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

#include "usb_com.h"
#include "usb_dev_internal.h"

#include "tusb.h"
#include "device/usbd_pvt.h"   /* usbd_edpt_busy() */

/* ------------------------------------------------------------------------- */
/*  Внутренние константы                                                     */
/* ------------------------------------------------------------------------- */

/** Порция, отдаваемая обработчику приёма за один вызов (= пакет USB FS). */
#define USB_COM_RX_CHUNK  64U

/* ------------------------------------------------------------------------- */
/*  Состояние модуля                                                         */
/* ------------------------------------------------------------------------- */

static bool s_initialized;
static USB_COM_RxCallback_t s_rx_cb;
static bool s_was_open;        /* последнее записанное в лог состояние "порт открыт" */
static bool s_tx_drop_pending; /* буфер передачи надо очистить, когда USB-передача завершится */

/* ------------------------------------------------------------------------- */
/*  Внутренние функции                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief Очищает буфер передачи, если это запрошено и безопасно. Пока идёт
 *        USB-передача, драйвер USB читает данные прямо из этого буфера в
 *        прерывании: очистка в этот момент оставляет передачу без данных, и
 *        прерывание USB повторяется без конца - программа зависает. Поэтому
 *        очистка откладывается до конца передачи. Только вне прерывания.
 */
static void usb_com_tx_drop_try(void)
{
    if (s_tx_drop_pending && !usbd_edpt_busy(USB_DEV_RHPORT, usb_dev_desc_com_ep_in()))
    {
        uint32_t lost = (uint32_t)USB_COM_TX_BUF_SIZE - tud_cdc_write_available();
        (void)tud_cdc_write_clear();
        s_tx_drop_pending = false;   /* до записи в лог: вывод лога может идти в этот же COM */
        if (lost != 0U)
        {
            USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_COM_TX_DISCARD, 0U, lost);
        }
    }
}

/**
 * @brief  Можно ли обмениваться данными: COM включён и подключён к ПК.
 * @return true - можно
 */
static bool usb_com_ready(void)
{
    return s_initialized && usb_dev_func_mounted(USB_DEV_FUNC_COM);
}

/* ------------------------------------------------------------------------- */
/*  Публичный API                                                            */
/* ------------------------------------------------------------------------- */

HAL_StatusTypeDef USB_COM_Init(void)
{
    if (usb_dev_in_isr())
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_COM_INIT, USB_DEV_API_ERR_ISR);
        return HAL_ERROR;
    }
    if (s_initialized)
    {
        return HAL_OK;
    }
    if (usb_dev_set_func(USB_DEV_FUNC_COM, true) != HAL_OK)
    {
        return HAL_ERROR;
    }
    s_initialized = true;
    USB_DEV_LOG(USB_DEV_LOG_CODE_COM_INIT, 0U, 0);
    return HAL_OK;
}

HAL_StatusTypeDef USB_COM_DeInit(void)
{
    if (usb_dev_in_isr())
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_COM_DEINIT, USB_DEV_API_ERR_ISR);
        return HAL_ERROR;
    }
    if (s_initialized)
    {
        s_initialized = false;
        (void)usb_dev_set_func(USB_DEV_FUNC_COM, false);
        USB_DEV_LOG(USB_DEV_LOG_CODE_COM_DEINIT, 0U, 0);
    }
    return HAL_OK;
}

void USB_COM_SetRxCallback(USB_COM_RxCallback_t cb)
{
    s_rx_cb = cb;
}

HAL_StatusTypeDef USB_COM_Transmit(const uint8_t *data, uint16_t len)
{
    if (usb_dev_in_isr())
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_COM_TRANSMIT, USB_DEV_API_ERR_ISR);
        return HAL_ERROR;
    }
    if ((data == NULL) && (len > 0U))
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_COM_TRANSMIT, USB_DEV_API_ERR_PARAM);
        return HAL_ERROR;
    }
    if (!s_initialized)
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_COM_TRANSMIT, USB_DEV_API_ERR_NOT_INIT);
        return HAL_ERROR;
    }
    if (!usb_com_ready())
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_COM_TX_NOT_READY, 0U, len);
        return HAL_ERROR;
    }
    if ((len == 0U) || !tud_cdc_connected())
    {
        /* Порт на ПК не открыт - данные никому не нужны. Не копим их: иначе после
         * открытия ПК получил бы обрывки старых записей (буфер затирается не по
         * границам вызовов). */
        return HAL_OK;
    }
    usb_com_tx_drop_try();
    /* Пока не очищен буфер со старыми данными, новые не принимаются: иначе
     * очистка стёрла бы и их. */
    if (s_tx_drop_pending || (tud_cdc_write_available() < len))
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_COM_TX_BUSY, 0U, len);
        return HAL_BUSY;
    }
    (void)tud_cdc_write(data, len);
    (void)tud_cdc_write_flush();
    return HAL_OK;
}

HAL_StatusTypeDef USB_COM_TransmitString(const char *str)
{
    if (str == NULL)
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_COM_TRANSMIT, USB_DEV_API_ERR_PARAM);
        return HAL_ERROR;
    }
    size_t len = strlen(str);
    if (len > 0xFFFFU)
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_COM_TRANSMIT, USB_DEV_API_ERR_PARAM);
        return HAL_ERROR;
    }
    return USB_COM_Transmit((const uint8_t *)str, (uint16_t)len);
}

uint16_t USB_COM_GetFreeSpace(void)
{
    if (usb_dev_in_isr())
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_COM_READ, USB_DEV_API_ERR_ISR);
        return 0U;
    }
    if (!usb_com_ready())
    {
        return 0U;
    }
    if (!tud_cdc_connected())
    {
        return (uint16_t)USB_COM_TX_BUF_SIZE;   /* порт закрыт - Transmit примет и отбросит всё */
    }
    usb_com_tx_drop_try();
    if (s_tx_drop_pending)
    {
        return 0U;
    }
    return (uint16_t)tud_cdc_write_available();
}

uint16_t USB_COM_Available(void)
{
    if (usb_dev_in_isr())
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_COM_READ, USB_DEV_API_ERR_ISR);
        return 0U;
    }
    if (!usb_com_ready())
    {
        return 0U;
    }
    return (uint16_t)tud_cdc_available();
}

uint16_t USB_COM_Read(uint8_t *buf, uint16_t max_len)
{
    if (usb_dev_in_isr())
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_COM_READ, USB_DEV_API_ERR_ISR);
        return 0U;
    }
    if ((buf == NULL) || (max_len == 0U))
    {
        USB_DEV_LOG_ERR(USB_DEV_LOG_CODE_API_ERROR, USB_DEV_API_COM_READ, USB_DEV_API_ERR_PARAM);
        return 0U;
    }
    if (!usb_com_ready())
    {
        return 0U;
    }
    return (uint16_t)tud_cdc_read(buf, max_len);
}

bool USB_COM_IsReady(void)
{
    return usb_com_ready();
}

bool USB_COM_IsOpen(void)
{
    return usb_com_ready() && tud_cdc_connected();
}

uint32_t USB_COM_GetBaudRate(void)
{
    if (!usb_com_ready())
    {
        return 0U;
    }
    cdc_line_coding_t coding;
    tud_cdc_get_line_coding(&coding);
    return coding.bit_rate;
}

/* ------------------------------------------------------------------------- */
/*  Колбэки TinyUSB                                                          */
/* ------------------------------------------------------------------------- */

/**
 * @brief Колбэк TinyUSB: ПК открыл/закрыл порт (DTR) - вызывается из tud_task().
 *        Буфер передачи очищается, чтобы ПК после открытия получил только
 *        новые данные, начиная с целого вызова USB_COM_Transmit(). Если идёт
 *        USB-передача - очистка после её конца (см. usb_com_tx_drop_try()).
 * @param itf номер CDC-интерфейса (всегда 0)
 * @param dtr сигнал DTR
 * @param rts сигнал RTS (не используется)
 */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf;
    (void)dtr;
    (void)rts;
    s_tx_drop_pending = true;
    usb_com_tx_drop_try();
}

/**
 * @brief Колбэк TinyUSB: USB-передача завершена (из tud_task(), до запуска
 *        следующей) - момент выполнить отложенную очистку буфера.
 * @param itf номер CDC-интерфейса (всегда 0)
 */
void tud_cdc_tx_complete_cb(uint8_t itf)
{
    (void)itf;
    usb_com_tx_drop_try();
}

/* ------------------------------------------------------------------------- */
/*  Внутренний API                                                           */
/* ------------------------------------------------------------------------- */

void usb_com_process(void)
{
    uint8_t chunk[USB_COM_RX_CHUNK];
    /* Не больше одного буфера приёма за проход - главный цикл не застревает здесь,
     * даже если ПК шлёт без остановки. */
    uint32_t budget = (USB_COM_RX_BUF_SIZE / USB_COM_RX_CHUNK) + 1U;

    /* Сброс шины (кабель вынут и вставлен) обрывает передачу без колбэка
     * завершения - отложенная очистка выполняется здесь. */
    usb_com_tx_drop_try();

    bool open = USB_COM_IsOpen();
    if (open != s_was_open)
    {
        s_was_open = open;
        USB_DEV_LOG(open ? USB_DEV_LOG_CODE_COM_OPEN : USB_DEV_LOG_CODE_COM_CLOSE, 0U,
                    open ? USB_COM_GetBaudRate() : 0U);
    }

    /* Обработчик вызывается только отсюда, а не из tud_cdc_rx_cb(): tud_task()
     * вызывается ещё и при отправке сетевых кадров, и колбэк пользователя
     * оказался бы внутри обработки сети. */
    while ((budget-- > 0U) && (s_rx_cb != NULL) && usb_com_ready() && (tud_cdc_available() > 0U))
    {
        uint32_t len = tud_cdc_read(chunk, sizeof(chunk));
        if (len == 0U)
        {
            break;
        }
        s_rx_cb(chunk, (uint16_t)len);
    }
}
