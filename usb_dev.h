/**
 ******************************************************************************
 * @file    usb_dev.h
 * @brief   Общая часть библиотеки: обслуживание USB, прерывание, проверка
 *          возможности работать сетью и COM-портом одновременно. Сеть -
 *          usb_eth.h (USB_ETH_xxx), виртуальный COM-порт - usb_com.h (USB_COM_xxx).
 * @author  Mechanic
 * @date    03.10.2026
 * @version 2.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef USB_DEV_H
#define USB_DEV_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "usb_dev_opts.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Обслуживание USB, сети и COM-порта: вызывать в while(1) как можно
 *        чаще, без задержек. Все колбэки библиотеки (сети и COM) вызываются
 *        только отсюда. До USB_ETH_Init()/USB_COM_Init() и из прерывания
 *        ничего не делает.
 */
void USB_Process(void);

/**
 * @brief Обработчик прерывания USB. Вызывать из OTG_FS_IRQHandler() (или
 *        OTG_HS_IRQHandler(), см. USB_DEV_RHPORT) в секции USER CODE BEGIN 0 с
 *        последующим return - HAL_PCD_IRQHandler вызываться не должен.
 */
void USB_IRQHandler(void);

#if USB_ETH_PROFILE
/** Этапы, по которым ведутся счётчики времени (USB_ETH_PROFILE = 1). Этапы вложены друг в
 *  друга (время вложенного входит в объемлющий): USB_PROF_PROCESS содержит TUD_TASK, RX_INPUT,
 *  TIMEOUTS, SOCK_OUTPUT, TX_DRAIN; TUD_TASK содержит RECV_CB; RX_INPUT - APP_RX;
 *  SOCK_OUTPUT и TCP_WRITE (вызываемый из кода пользователя) - независимо; SOCK_OUTPUT содержит
 *  LINKOUT; LINKOUT содержит XMIT_CB и TX_WAIT; TX_WAIT содержит TUD_TASK_WAIT.
 *  Время прерывания USB из этапов основного кода вычтено (оно в USB_PROF_ISR). */
typedef enum
{
    USB_PROF_ISR = 0,    /**< прерывание USB целиком (TinyUSB: FIFO, события), bytes не ведётся */
    USB_PROF_PROCESS,    /**< весь USB_Process() за проход (calls - число проходов цикла) */
    USB_PROF_TUD_TASK,   /**< tud_task() из USB_Process() */
    USB_PROF_RECV_CB,    /**< приём кадра от ПК в очередь (копия в буфер lwIP), bytes - кадры */
    USB_PROF_RX_INPUT,   /**< ethernet_input() lwIP на принятый кадр вместе с кодом пользователя */
    USB_PROF_APP_RX,     /**< обработчики on_receive пользователя, bytes - данные TCP */
    USB_PROF_TIMEOUTS,   /**< sys_check_timeouts() */
    USB_PROF_SOCK_OUTPUT,/**< отложенный tcp_output() всех соединений */
    USB_PROF_TX_DRAIN,   /**< очередь кадров к ПК -> буферы USB (копия в NTB) */
    USB_PROF_LINKOUT,    /**< отправка кадра lwIP в USB целиком, bytes - кадры */
    USB_PROF_XMIT_CB,    /**< копия кадра в буфер USB (NTB), bytes - кадры */
    USB_PROF_TX_WAIT,    /**< ожидание свободного места при полной очереди отправки */
    USB_PROF_TUD_TASK_WAIT, /**< tud_task() внутри ожидания (часть TX_WAIT) */
    USB_PROF_TCP_WRITE,  /**< tcp_write() внутри USB_ETH_TCP_Send(): копия и сумма, bytes - данные */
    USB_PROF_COUNT
} USB_ProfId_t;

/** Накопленное по одному этапу. */
typedef struct
{
    uint64_t cycles;   /**< такты ядра за окно */
    uint32_t calls;    /**< число вызовов */
    uint32_t bytes;    /**< байт (смысл - в описании этапа) */
} USB_ProfItem_t;

/** Снимок счётчиков с момента USB_ProfileReset(). */
typedef struct
{
    USB_ProfItem_t item[USB_PROF_COUNT];
    uint32_t window_ms;   /**< длительность окна, мс */
    uint32_t cpu_hz;      /**< частота ядра, Гц (доля этапа = cycles / (window_ms * cpu_hz / 1000)) */
} USB_Profile_t;

/** Обнуляет счётчики и начинает новое окно измерения. Из прерывания не вызывать. */
void USB_ProfileReset(void);

/**
 *   Копия счётчиков с начала окна (согласованная, прерывания на время копии запрещены).
 *   out куда скопировать
 *  HAL_OK; HAL_ERROR - out = NULL или вызов из прерывания
 */
HAL_StatusTypeDef USB_GetProfile(USB_Profile_t *out);
#endif /* USB_ETH_PROFILE */

/**
 * @brief  Может ли USB-контроллер этого микроконтроллера работать сетью и
 *         COM-портом одновременно. Если нет - работает любое одно из них, а
 *         Init второго возвращает HAL_ERROR.
 * @return true - сеть и COM одновременно (STM32H7, F446, F412, F413, F7...);
 *         false - только одно (STM32F401, F405/F407 на OTG_FS, F411...)
 */
bool USB_IsCompositeSupported(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_DEV_H */
