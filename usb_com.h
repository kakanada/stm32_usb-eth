/**
 ******************************************************************************
 * @file    usb_com.h
 * @brief   Виртуальный COM-порт по USB (CDC-ACM): публичный API. Работает сам
 *          по себе или вместе с сетью (usb_eth.h) по одному USB-кабелю.
 *          Логика передачи - как у CDC_Transmit_FS() из CubeMX: неблокирующая,
 *          HAL_BUSY, если данные сейчас не помещаются.
 * @author  Mechanic
 * @date    03.10.2026
 * @version 2.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef USB_COM_H
#define USB_COM_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "usb_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Обработчик принятых данных (из USB_Process(), не из прерывания).
 *        Поток байт: одно сообщение ПК может прийти несколькими вызовами,
 *        несколько - одним. data действителен только внутри вызова.
 * @param data принятые байты
 * @param len  их число (1..64)
 */
typedef void (*USB_COM_RxCallback_t)(const uint8_t *data, uint16_t len);

/**
 * @brief  Включает виртуальный COM-порт. Можно вызывать до или после
 *         USB_ETH_Init(), в любом порядке. Если сеть уже работает у ПК, плата
 *         переподключится к нему (~0,3 с, соединения сети оборвутся). Вызывать
 *         после MX_USB_OTG_FS_PCD_Init(). Повторный вызов возвращает HAL_OK.
 * @return HAL_OK; HAL_ERROR - микроконтроллер не тянет сеть и COM сразу (см.
 *         USB_IsCompositeSupported(), COM не включён, сеть работает дальше),
 *         ошибка запуска USB или вызов из прерывания
 */
HAL_StatusTypeDef USB_COM_Init(void);

/**
 * @brief  Выключает COM-порт: он пропадает у ПК (если работает сеть - плата
 *         переподключится к ПК уже без COM). Обработчик приёма сохраняется.
 * @return HAL_OK (в т.ч. если COM не был включён); HAL_ERROR - из прерывания
 */
HAL_StatusTypeDef USB_COM_DeInit(void);

/**
 * @brief Задаёт обработчик приёма. С обработчиком данные отдаются ему из
 *        USB_Process(); без него (NULL) копятся в буфере и читаются
 *        USB_COM_Read(). Можно вызывать в любой момент, в т.ч. до Init.
 * @param cb обработчик или NULL
 */
void USB_COM_SetRxCallback(USB_COM_RxCallback_t cb);

/**
 * @brief  Отправляет данные: целиком или ничего, без ожидания. Данные
 *         копируются - буфер можно переиспользовать сразу.
 *         Если порт на ПК не открыт, в буфере остаются последние
 *         USB_COM_TX_BUF_SIZE байт - их получит программа, открывшая порт.
 * @param  data данные
 * @param  len  длина (0 - ничего не делает, HAL_OK)
 * @return HAL_OK - данные в очереди; HAL_BUSY - не помещаются, повторить
 *         позже; HAL_ERROR - COM не включён/не подключён к ПК, data = NULL,
 *         вызов из прерывания
 */
HAL_StatusTypeDef USB_COM_Transmit(const uint8_t *data, uint16_t len);

/**
 * @brief  Отправка строки с завершающим нулём (без самого нуля).
 * @param  str строка (не длиннее 65535 символов)
 * @return как у USB_COM_Transmit()
 */
HAL_StatusTypeDef USB_COM_TransmitString(const char *str);

/**
 * @brief  Сколько байт сейчас гарантированно примет USB_COM_Transmit().
 * @return свободное место в буфере передачи; 0 - COM не подключён
 */
uint16_t USB_COM_GetFreeSpace(void);

/**
 * @brief  Сколько принятых байт ждут чтения (режим без обработчика).
 * @return число байт
 */
uint16_t USB_COM_Available(void);

/**
 * @brief  Читает принятые данные (режим без обработчика).
 * @param  buf     куда читать
 * @param  max_len размер buf
 * @return прочитано байт (0 - данных нет, COM не включён, buf = NULL, из прерывания)
 */
uint16_t USB_COM_Read(uint8_t *buf, uint16_t max_len);

/**
 * @brief  Подключён ли COM к ПК (ПК сконфигурировал устройство).
 * @return true - USB_COM_Transmit() работает
 */
bool USB_COM_IsReady(void);

/**
 * @brief  Открыт ли порт программой на ПК (терминал выставил сигнал DTR).
 * @return true - открыт
 */
bool USB_COM_IsOpen(void);

/**
 * @brief  Скорость, выбранная в программе на ПК. На передачу по USB не влияет -
 *         только для информации (например, если плата пересылает данные в UART).
 * @return бит/с; 0 - COM не подключён
 */
uint32_t USB_COM_GetBaudRate(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_COM_H */
