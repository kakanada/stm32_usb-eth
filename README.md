# usb_eth

Библиотека превращает STM32 в сетевое устройство, подключаемое по USB: компьютер видит плату как
обычный сетевой адаптер (без установки драйверов), а прошивка работает с сетью так же, как с
Ethernet + lwIP — открывает TCP/UDP-порты и получает события подключения, приёма данных и обрыва
соединения через колбэки. Библиотека — тонкая прослойка между TinyUSB (класс CDC-NCM) и lwIP и
не требует настройки: достаточно выбрать режим адресации одним define.

## Возможности

- Плата определяется как сетевой адаптер USB CDC-NCM встроенными драйверами Windows 10/11, Linux
  и macOS — ничего устанавливать не нужно.
- Два режима адресации, выбор одним define `USB_ETH_MODE`:
  - `USB_ETH_MODE_HOST` (по умолчанию) — плата имеет фиксированный IP `192.168.7.1` и сама
    раздаёт адрес компьютеру по DHCP. Подключение «точка-точка» к ПК.
  - `USB_ETH_MODE_CLIENT` — плата получает IP по DHCP, как любое устройство в сети (роутер с
    USB-портом, ПК с общим доступом к сети). Для сетей, где уже много устройств.
- TCP-серверы: `USB_ETH_TCP_Listen(port, handlers)` и колбэки `on_connect`, `on_receive`,
  `on_sent`, `on_disconnect(reason)`; отправка «всё или ничего», закрытие с досылкой данных.
- UDP-сокеты: приём с колбэком, отправка на адрес или широковещательно.
- Событие «сеть поднялась / упала» с текущим IP.
- Безопасность использования:
  - все колбэки вызываются только из `USB_ETH_Process()` в главном цикле, никогда из прерывания;
  - вызов API из прерывания не ломает стек, а возвращает `HAL_ERROR`;
  - `on_disconnect` гарантированно вызывается ровно один раз на каждое соединение — при
    закрытии любой стороной, сбросе, пропаже клиента (TCP keepalive, ~8 с) и отключении USB;
  - без `malloc`: статические пулы серверов, соединений и сокетов;
  - ни одного бесконечного ожидания: передача в USB ограничена таймаутом.
- MAC-адрес и серийный номер USB вычисляются из уникального ID чипа — несколько плат в одной
  сети не конфликтуют.
- Опциональная запись событий в [stm32_logger](../stm32_logger) (`USB_ETH_LOG_ENABLE`).

## Требования к настройке в CubeMX

| Раздел CubeMX | Настройка | Зачем |
|---|---|---|
| Connectivity → USB_OTG_FS | Mode: **Device_Only** | USB-периферия в режиме устройства |
| USB_OTG_FS → Parameter Settings | **VBUS sensing: Disabled** (если пин VBUS не разведён) | иначе плата не определяется ПК; при разведённом VBUS — Enabled и `-DUSB_ETH_VBUS_SENSING=1` |
| USB_OTG_FS → NVIC | **USB On The Go FS global interrupt: Enabled** | CubeMX создаст `OTG_FS_IRQHandler`, приоритет задаётся здесь |
| Middleware → USB_DEVICE | **Не включать** | стек USB — TinyUSB, а не ST USB Device |
| Clock Configuration | тактирование USB **ровно 48 МГц** | на H7 удобнее всего отдельный HSI48 (см. ниже) |

**Тактирование USB на STM32H7.** Если PLL1Q/PLL3Q не дают ровно 48 МГц, включите HSI48 и выберите
его источником USB: в Clock Configuration мультиплексор USB → **HSI48**. Если CubeMX этого не
сохраняет, допишите вручную в `SystemClock_Config()` флаг `RCC_OSCILLATORTYPE_HSI48` и
`HSI48State = RCC_HSI48_ON`, а в `HAL_PCD_MspInit()` — `UsbClockSelection = RCC_USBCLKSOURCE_HSI48`.
Остальное (включение тактирования USB, GPIO PA11/PA12, USB voltage detector) CubeMX генерирует
сам в `MX_USB_OTG_FS_PCD_Init()` — эту функцию нужно по-прежнему вызывать из `main()`.

## Быстрый старт

### 1. Зависимости

| Библиотека | Версия (проверено) | Откуда |
|---|---|---|
| TinyUSB | 0.21.0 | <https://github.com/hathach/tinyusb> |
| lwIP | 2.2.1 | <https://github.com/lwip-tcpip/lwip> |
| stm32_logger | 1.7+ | только при `USB_ETH_LOG_ENABLE=1` |

TinyUSB и lwIP используются без изменений — вся их настройка лежит в папке `port/` этой
библиотеки.

### 2. Исходники и пути

Добавьте в сборку (CMake `target_sources` или STM32CubeIDE → «Add to build»):

```text
usb_eth/usb_eth.c  usb_eth/usb_eth_sock.c  usb_eth/usb_eth_desc.c

tinyusb/src/tusb.c
tinyusb/src/common/tusb_fifo.c
tinyusb/src/device/usbd.c
tinyusb/src/class/net/ncm_device.c
tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c
tinyusb/src/portable/synopsys/dwc2/dwc2_common.c
tinyusb/lib/networking/dhserver.c        (только режим HOST)

lwip/src/core/*.c
lwip/src/core/ipv4/*.c
lwip/src/netif/ethernet.c
```

Пути include: `usb_eth/`, `usb_eth/port/`, `tinyusb/src/`, `tinyusb/lib/networking/`,
`lwip/src/include/` (и папку `stm32_logger`, если логирование включено).

Если в проекте включён `-Werror`, для файлов TinyUSB/lwIP его стоит отключить
(`-Wno-error`): их исходники содержат безобидные предупреждения, а править чужой код не нужно.

### 3. Режим и параметры

Все параметры — **глобальные define проекта** (CMake `target_compile_definitions`,
STM32CubeIDE → Properties → C/C++ Build → Settings → MCU GCC Compiler → Preprocessor), а не
`#define` в коде: их должны одинаково видеть и библиотека, и исходники TinyUSB/lwIP. Без единого
define библиотека работает в режиме HOST.

| Define | По умолчанию | Назначение |
|---|---|---|
| `USB_ETH_MODE` | `USB_ETH_MODE_HOST` | `USB_ETH_MODE_HOST` — плата раздаёт IP; `USB_ETH_MODE_CLIENT` — получает IP по DHCP |
| `USB_ETH_HOST_IP` | `USB_ETH_IP4(192,168,7,1)` | IP платы в режиме HOST (маска /24, ПК получит .2) |
| `USB_ETH_HOSTNAME` | `"stm32-usb-eth"` | имя платы для DHCP-сервера роутера (режим CLIENT) |
| `USB_ETH_MAX_TCP_SERVERS` | `4` | сколько TCP-портов можно слушать одновременно |
| `USB_ETH_MAX_TCP_CONNECTIONS` | `4` | одновременных TCP-соединений (на все порты) |
| `USB_ETH_MAX_UDP_SOCKETS` | `4` | открытых UDP-портов |
| `USB_ETH_RHPORT` | `0` | `0` — OTG_FS, `1` — OTG_HS во встроенном FS PHY |
| `USB_ETH_VBUS_SENSING` | `0` | `1`, если пин VBUS разведён и включён в CubeMX |
| `USB_ETH_USB_VID` / `USB_ETH_USB_PID` | `0xCAFE` / `0x4011` | для серийного изделия — свой VID/PID |
| `USB_ETH_STR_MANUFACTURER` / `USB_ETH_STR_PRODUCT` | `"Mechanic"` / `"STM32 USB Ethernet"` | имена в диспетчере устройств |
| `USB_ETH_MAC_ADDR` | не задан (из UID) | фиксированный MAC, например `{0x02,0x11,0x22,0x33,0x44,0x56}` |
| `USB_ETH_LOG_ENABLE` | `0` | `1` — писать события в stm32_logger |

Пример для CMake:

```cmake
target_compile_definitions(${PROJECT_NAME} PRIVATE USB_ETH_MODE=USB_ETH_MODE_CLIENT)
```

### 4. Код

`main.c`:

```c
/* USER CODE BEGIN Includes */
#include "usb_eth.h"
/* USER CODE END Includes */

/* USER CODE BEGIN 0 */
static void App_OnNet(bool is_up, uint32_t ip)
{
    /* is_up: сеть готова; ip: USB_ETH_IP4_OCTET(ip, 0..3) */
}

static void App_OnReceive(USB_ETH_TcpConn_t *conn, const uint8_t *data, uint16_t len)
{
    (void)USB_ETH_TCP_Send(conn, data, len);   /* эхо */
}

static const USB_ETH_TcpHandlers_t s_app_handlers =
{
    .on_connect    = NULL,
    .on_receive    = App_OnReceive,
    .on_sent       = NULL,
    .on_disconnect = NULL
};
/* USER CODE END 0 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USB_OTG_FS_PCD_Init();          /* тактирование и GPIO USB */

    /* USER CODE BEGIN 2 */
    USB_ETH_Init();
    USB_ETH_SetNetCallback(App_OnNet);
    USB_ETH_TCP_Listen(7, &s_app_handlers, NULL);
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN 3 */
        USB_ETH_Process();             /* как можно чаще, без задержек в цикле */
        /* USER CODE END 3 */
    }
}
```

`stm32h7xx_it.c` — прерывание USB обслуживает TinyUSB, обработчик HAL вызываться не должен.
Код в секции USER CODE переживает перегенерацию CubeMX:

```c
void OTG_FS_IRQHandler(void)
{
    /* USER CODE BEGIN OTG_FS_IRQn 0 */
    USB_ETH_IRQHandler();
    return;
    /* USER CODE END OTG_FS_IRQn 0 */
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
    /* USER CODE BEGIN OTG_FS_IRQn 1 */
    /* USER CODE END OTG_FS_IRQn 1 */
}
```

Если используется stm32_logger, `LOGGER_Init()` вызывается до `USB_ETH_Init()`, а в проекте
определяется `LOGGER_ENABLE_USB_ETH` (коды `LOG_CODE_USB_ETH_*`, адресное пространство `0x42`).

### 5. Примеры

| Файл | Что показывает |
|---|---|
| `examples/example_echo.c` | минимальная интеграция: TCP эхо-сервер, индикация состояния сети |
| `examples/example_cmd_server.c` | построчные команды из потока TCP, контекст на клиента, большой ответ порциями через `on_sent`, закрытие по команде |
| `examples/example_udp_discovery.c` | поиск платы в сети широковещательным UDP-запросом (для режима CLIENT) |

### 6. Проверка с компьютера

Режим HOST: после подключения кабеля в системе появляется сетевой адаптер, ПК получает адрес
`192.168.7.2`.

```bash
ping 192.168.7.1
ncat 192.168.7.1 7
```

Режим CLIENT: плата получает адрес от DHCP-сервера сети. Варианты подключения — роутер с USB-портом
и драйвером CDC-NCM (например, OpenWrt с пакетом `kmod-usb-net-cdc-ncm`, интерфейс `usb0`
добавляется в мост LAN) или ПК с общим доступом к интернету (ICS) на USB-адаптер. IP платы виден
в списке клиентов роутера (по имени `USB_ETH_HOSTNAME`) или находится примером UDP-обнаружения.

Если Windows не видит сетевой адаптер:

- в диспетчере устройств устройство с восклицательным знаком — Windows запомнила старый драйвер
  для этого VID/PID после изменений дескрипторов; удалите устройство (`pnputil /remove-device`)
  или смените PID;
- не используйте Zadig/libwdi для этого устройства: установленный им WinUSB-драйвер имеет
  приоритет над встроенным драйвером NCM. Проверка:
  `Get-ChildItem C:\Windows\INF\oem*.inf | Select-String "VID_CAFE"`; найденные пакеты удалить
  `pnputil /delete-driver oemNN.inf /uninstall`.

## Модель событий

- **Контекст.** Все колбэки вызываются из `USB_ETH_Process()`. Внутри колбэков можно вызывать
  любые функции API (отправку, закрытие, открытие портов). Функции API нельзя вызывать из
  прерываний — они вернут `HAL_ERROR`/`NULL`.
- **Сеть.** `on_net(true, ip)` — USB подключён и IP назначен (HOST: сразу после подключения
  кабеля; CLIENT: после ответа DHCP). `on_net(false, 0)` — кабель отключён или адрес потерян; все
  открытые TCP-соединения при этом закрываются с `USB_ETH_CLOSE_LINK_LOST`. Порты, открытые через
  `Listen`/`Bind`, продолжают работать после повторного подключения — открывать их заново не нужно.
- **Жизнь соединения.** `on_connect` → (`on_receive` / `on_sent`)* → `on_disconnect`. После
  возврата из `on_disconnect` указатель `conn` недействителен (слот будет выдан следующему
  клиенту). Поле `conn->user_ctx` — для своих данных клиента.
- **TCP — поток байт.** Одно сообщение клиента может прийти несколькими `on_receive`, несколько
  сообщений — одним. Протокол должен сам определять границы (перевод строки, длина в заголовке) —
  см. `example_cmd_server.c`.
- **Отправка.** `USB_ETH_TCP_Send` копирует данные целиком или не принимает ничего (`HAL_BUSY`).
  Большие объёмы отправляются порциями: сколько примет сейчас (`USB_ETH_TCP_GetFreeSpace`),
  остальное — в `on_sent`.

## Честные ограничения

- Проверено на железе только на STM32H743 (OTG_FS, режим HOST, Windows). Режим CLIENT, другие
  серии STM32 (определяются автоматически) и хосты Linux/macOS/роутеры собраны и логически
  проверены, но на реальном железе не испытывались.
- Только Full Speed (12 Мбит/с), реальная скорость TCP — единицы Мбит/с.
- Только IPv4. Нет DNS-клиента и исходящих TCP-соединений (плата — только TCP-сервер; UDP —
  в обе стороны).
- Без RTOS: библиотека рассчитана на главный цикл. С RTOS все вызовы API и `USB_ETH_Process()`
  должны выполняться из одной задачи.
- Открытый TCP-порт или UDP-сокет нельзя закрыть — они живут до перезагрузки.
- Режим HOST: маска всегда /24, ПК выдаются адреса `.2` и `.3`, без шлюза и DNS — через плату
  нет доступа в интернет (это и не требуется).
- Режим CLIENT без DHCP-сервера в сети адрес не получит (статический адрес в этом режиме не
  поддерживается).
- Размер одной датаграммы UDP — до 1472 байт; фрагментация IP не поддерживается.
- VID `0xCAFE` — тестовый VID TinyUSB, для изделия нужен собственный VID/PID.

## Лицензия

Библиотека распространяется на условиях **PolyForm Noncommercial License 1.0.0** — свободное
использование, копирование, изменение и распространение для любых НЕКОММЕРЧЕСКИХ целей, при
условии сохранения уведомления об авторских правах. Коммерческое использование требует отдельного
разрешения правообладателя. Полный текст — файл [LICENSE](LICENSE).
