/**
 ******************************************************************************
 * @file    usb_eth_desc.c
 * @brief   USB-дескрипторы usb_eth: одно устройство CDC-NCM (Full Speed) +
 *          MS OS 2.0 дескриптор для автоматической установки драйвера Windows.
 * @author  Mechanic
 * @date    27.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include <string.h>

#include "usb_eth.h"
#include "tusb.h"
#include "class/net/net_device.h"

/* ------------------------------------------------------------------------- */
/*  Идентификаторы                                                           */
/* ------------------------------------------------------------------------- */

enum
{
    USB_ETH_STRID_LANGID = 0,
    USB_ETH_STRID_MANUFACTURER,
    USB_ETH_STRID_PRODUCT,
    USB_ETH_STRID_SERIAL,
    USB_ETH_STRID_INTERFACE,
    USB_ETH_STRID_MAC,
    USB_ETH_STRID_COUNT
};

enum
{
    USB_ETH_ITF_NUM_CDC = 0,
    USB_ETH_ITF_NUM_CDC_DATA,
    USB_ETH_ITF_NUM_TOTAL
};

#define USB_ETH_EP_NOTIF  0x81U
#define USB_ETH_EP_OUT    0x02U
#define USB_ETH_EP_IN     0x82U

/** Максимум символов в строковом дескрипторе. */
#define USB_ETH_STR_MAX_CHARS  32U

/* ------------------------------------------------------------------------- */
/*  Device / Configuration                                                   */
/* ------------------------------------------------------------------------- */

static const tusb_desc_device_t s_desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0201U,   /* >= 2.01 нужен для BOS / MS OS 2.0 */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_ETH_USB_VID,
    .idProduct          = USB_ETH_USB_PID,
    .bcdDevice          = 0x0100U,
    .iManufacturer      = USB_ETH_STRID_MANUFACTURER,
    .iProduct           = USB_ETH_STRID_PRODUCT,
    .iSerialNumber      = USB_ETH_STRID_SERIAL,
    .bNumConfigurations = 1U
};

#define USB_ETH_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_NCM_DESC_LEN)

static const uint8_t s_desc_config[] =
{
    TUD_CONFIG_DESCRIPTOR(1, USB_ETH_ITF_NUM_TOTAL, 0, USB_ETH_CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_NCM_DESCRIPTOR(USB_ETH_ITF_NUM_CDC, USB_ETH_STRID_INTERFACE, USB_ETH_STRID_MAC,
                           USB_ETH_EP_NOTIF, 64, USB_ETH_EP_OUT, USB_ETH_EP_IN,
                           64, CFG_TUD_NET_MTU, 50, NCM_NETWORK_CAPS_ETH_FILTER),
};

/**
 * @brief  Колбэк TinyUSB: дескриптор устройства.
 * @return указатель на дескриптор
 */
const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&s_desc_device;
}

/**
 * @brief  Колбэк TinyUSB: дескриптор конфигурации.
 * @param  index номер конфигурации (всегда 0)
 * @return указатель на дескриптор
 */
const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return s_desc_config;
}

/* ------------------------------------------------------------------------- */
/*  BOS + MS OS 2.0: Windows 10/11 сам ставит встроенный драйвер NCM         */
/* ------------------------------------------------------------------------- */

#define USB_ETH_BOS_TOTAL_LEN     (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define USB_ETH_MS_OS_20_DESC_LEN 0xB2U
#define USB_ETH_MS_OS_20_VENDOR   1U

static const uint8_t s_desc_bos[] =
{
    TUD_BOS_DESCRIPTOR(USB_ETH_BOS_TOTAL_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(USB_ETH_MS_OS_20_DESC_LEN, USB_ETH_MS_OS_20_VENDOR)
};

static const uint8_t s_desc_ms_os_20[] =
{
    /* Set header */
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000),
    U16_TO_U8S_LE(USB_ETH_MS_OS_20_DESC_LEN),
    /* Configuration subset header */
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0,
    U16_TO_U8S_LE(USB_ETH_MS_OS_20_DESC_LEN - 0x0A),
    /* Function subset header */
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), USB_ETH_ITF_NUM_CDC, 0,
    U16_TO_U8S_LE(USB_ETH_MS_OS_20_DESC_LEN - 0x0A - 0x08),
    /* Compatible ID "WINNCM" - встроенный класс-драйвер NCM */
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'N', 'C', 'M', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* Registry property DeviceInterfaceGUIDs */
    U16_TO_U8S_LE(USB_ETH_MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0,
    'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
    U16_TO_U8S_LE(0x0050),
    '{', 0, '8', 0, 'f', 0, '3', 0, 'a', 0, '3', 0, 'd', 0, '7', 0, 'c', 0, '-', 0,
    '1', 0, 'b', 0, '6', 0, 'e', 0, '-', 0, '4', 0, 'e', 0, '2', 0, 'a', 0, '-', 0,
    '9', 0, 'c', 0, '4', 0, 'a', 0, '-', 0, '2', 0, 'f', 0, '6', 0, 'b', 0, '1', 0,
    'a', 0, '7', 0, 'd', 0, '9', 0, 'e', 0, '1', 0, '0', 0, '}', 0, 0, 0, 0, 0
};

TU_VERIFY_STATIC(sizeof(s_desc_ms_os_20) == USB_ETH_MS_OS_20_DESC_LEN, "MS OS 2.0 size");

/**
 * @brief  Колбэк TinyUSB: BOS-дескриптор.
 * @return указатель на дескриптор
 */
const uint8_t *tud_descriptor_bos_cb(void)
{
    return s_desc_bos;
}

/**
 * @brief  Колбэк TinyUSB: vendor-запрос; отдаёт MS OS 2.0 дескриптор Windows.
 * @param  rhport  USB-порт
 * @param  stage   стадия control-передачи
 * @param  request запрос
 * @return true - запрос обработан
 */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    if (stage != CONTROL_STAGE_SETUP)
    {
        return true;
    }
    if ((request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR) &&
        (request->bRequest == USB_ETH_MS_OS_20_VENDOR) && (request->wIndex == 7U))
    {
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)s_desc_ms_os_20,
                                USB_ETH_MS_OS_20_DESC_LEN);
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/*  Строки                                                                   */
/* ------------------------------------------------------------------------- */

static uint16_t s_desc_str[USB_ETH_STR_MAX_CHARS + 1U];

/**
 * @brief  Записывает байт в строковый дескриптор как 2 hex-символа.
 * @param  pos   позиция символа
 * @param  value байт
 * @return новая позиция
 */
static uint32_t usb_eth_str_put_hex(uint32_t pos, uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    s_desc_str[1U + pos] = (uint16_t)hex[value >> 4];
    s_desc_str[2U + pos] = (uint16_t)hex[value & 0x0FU];
    return pos + 2U;
}

/**
 * @brief  Колбэк TinyUSB: строковый дескриптор (UTF-16).
 * @param  index  номер строки
 * @param  langid язык (не используется)
 * @return указатель на дескриптор или NULL
 */
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    uint32_t count = 0U;
    const char *str = NULL;
    (void)langid;

    switch (index)
    {
        case USB_ETH_STRID_LANGID:
            s_desc_str[1] = 0x0409U;   /* English (US) */
            count = 1U;
            break;
        case USB_ETH_STRID_SERIAL:
        {
            /* Серийный номер = UID чипа: у каждой платы свой, Windows не
             * путает несколько одинаковых устройств */
            uint32_t uid[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
            for (uint32_t w = 0U; w < 3U; w++)
            {
                for (int32_t shift = 24; shift >= 0; shift -= 8)
                {
                    count = usb_eth_str_put_hex(count, (uint8_t)(uid[w] >> shift));
                }
            }
            break;
        }
        case USB_ETH_STRID_MAC:
            for (uint32_t i = 0U; i < sizeof(tud_network_mac_address); i++)
            {
                count = usb_eth_str_put_hex(count, tud_network_mac_address[i]);
            }
            break;
        case USB_ETH_STRID_MANUFACTURER:
            str = USB_ETH_STR_MANUFACTURER;
            break;
        case USB_ETH_STRID_PRODUCT:
            str = USB_ETH_STR_PRODUCT;
            break;
        case USB_ETH_STRID_INTERFACE:
            str = "USB Ethernet";
            break;
        default:
            return NULL;
    }

    if (str != NULL)
    {
        count = (uint32_t)strlen(str);
        if (count > USB_ETH_STR_MAX_CHARS)
        {
            count = USB_ETH_STR_MAX_CHARS;
        }
        for (uint32_t i = 0U; i < count; i++)
        {
            s_desc_str[1U + i] = (uint16_t)(uint8_t)str[i];
        }
    }

    s_desc_str[0] = (uint16_t)(((uint16_t)TUSB_DESC_STRING << 8) | (uint16_t)((2U * count) + 2U));
    return s_desc_str;
}
