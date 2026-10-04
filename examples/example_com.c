/**
 ******************************************************************************
 * @file    example_com.c
 * @brief   Пример usb_com: виртуальный COM-порт с эхом принятых строк, вместе
 *          с сетью (если микроконтроллер тянет оба устройства) или один.
 *          Фрагменты вставляются в соответствующие USER CODE секции CubeMX.
 * @author  Mechanic
 * @date    03.10.2026
 * @version 2.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include "main.h"
#include "usb_com.h"
#include "usb_eth.h"

/**
 * @brief Обработчик приёма COM: отправляет принятое обратно (эхо).
 *        Вызывается из USB_Process(), можно сразу отвечать.
 * @param data принятые байты
 * @param len  их число
 */
static void Com_OnReceive(const uint8_t *data, uint16_t len)
{
    /* HAL_BUSY - буфер передачи полон (ПК не успевает читать): данные этого
     * вызова не отправлены, решать приложению - отбросить или повторить. */
    (void)USB_COM_Transmit(data, len);
}

int main(void)
{
    /* ... HAL_Init(), SystemClock_Config(), MX_GPIO_Init() ... */
    MX_USB_OTG_FS_PCD_Init();   /* тактирование и выводы USB - до Init библиотеки */

    /* USER CODE BEGIN 2 */
    USB_COM_SetRxCallback(Com_OnReceive);   /* NULL - читать самому через USB_COM_Read() */
    if (USB_COM_Init() != HAL_OK)
    {
        Error_Handler();
    }
    /* Сеть - по возможности: на STM32F401/F411 и т.п. USB_ETH_Init() вернёт
     * HAL_ERROR, а COM продолжит работать. */
    if (USB_IsCompositeSupported())
    {
        (void)USB_ETH_Init();
    }
    /* USER CODE END 2 */

    uint32_t last = HAL_GetTick();
    while (1)
    {
        /* USER CODE BEGIN 3 */
        USB_Process();

        /* Раз в секунду - строка в порт, только если его открыли на ПК */
        if (((HAL_GetTick() - last) >= 1000U) && USB_COM_IsOpen())
        {
            last = HAL_GetTick();
            (void)USB_COM_TransmitString("tick\r\n");
        }
        /* USER CODE END 3 */
    }
}

/* Режим без обработчика (как с CDC_Receive_FS из CubeMX, но без правки
 * сгенерированного файла) - USB_COM_SetRxCallback(NULL) и в цикле:
 *
 *     uint8_t buf[64];
 *     uint16_t n = USB_COM_Read(buf, sizeof(buf));
 *     if (n > 0U) { ...разбор... }
 *
 * Пока данные не прочитаны, они ждут в буфере USB_COM_RX_BUF_SIZE байт;
 * когда он полон, ПК приостанавливает передачу - байты не теряются.
 *
 * Замена кода CubeMX: CDC_Transmit_FS(buf, len) -> USB_COM_Transmit(buf, len),
 * USBD_OK/USBD_BUSY/USBD_FAIL -> HAL_OK/HAL_BUSY/HAL_ERROR. Буфер можно
 * менять сразу после вызова - данные скопированы.
 */

/* stm32xxxx_it.c:
 *
 * void OTG_FS_IRQHandler(void)
 * {
 *     USER CODE BEGIN OTG_FS_IRQn 0
 *     USB_IRQHandler();
 *     return;
 *     USER CODE END OTG_FS_IRQn 0
 *     HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);   <- больше не выполняется
 * }
 */
