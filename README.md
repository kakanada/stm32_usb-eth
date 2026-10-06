# usb_eth

Библиотека делает из STM32 USB-устройство, которое компьютер видит как **сетевой адаптер**, как
**виртуальный COM-порт** или как **оба сразу** по одному кабелю - без установки драйверов.

- **Сеть** (`USB_ETH_xxx`) - прошивка работает с сетью как с Ethernet + lwIP: открывает
  TCP/UDP-порты и получает события подключения, приёма данных и обрыва соединения через колбэки.
  Слой между TinyUSB (класс CDC-NCM) и lwIP, режим адресации выбирается одним define.
- **COM-порт** (`USB_COM_xxx`) - виртуальный последовательный порт (класс CDC-ACM). Передача
  устроена как `CDC_Transmit_FS()` из CubeMX, приём - через свой обработчик или чтением из буфера.
- **Общее** (`USB_xxx`) - обслуживание в главном цикле и прерывание USB.

Какие устройства работают, решают вызовы: был `USB_ETH_Init()` - есть сеть, был
`USB_COM_Init()` - есть COM-порт, были оба - работают оба, порядок вызовов любой.

## Возможности

- Встроенные драйверы Windows 10/11, Linux и macOS - ничего устанавливать не нужно; в составном
  устройстве Windows сама ставит драйвер сети (NCM) и драйвер COM-порта (usbser).
- Сеть, два режима адресации - define `USB_ETH_MODE`:
  - `USB_ETH_MODE_HOST` (по умолчанию) - плата имеет фиксированный IP `192.168.7.1` и сама
    раздаёт адрес компьютеру по DHCP. Подключение "точка-точка" к ПК.
  - `USB_ETH_MODE_CLIENT` - плата получает IP по DHCP, как любое устройство в сети (роутер с
    USB-портом, ПК с общим доступом к сети). Для сетей, где уже много устройств.
- TCP-серверы: `USB_ETH_TCP_Listen(port, handlers)` и колбэки `on_connect`, `on_receive`,
  `on_sent`, `on_disconnect(reason)`; отправка "всё или ничего", закрытие с досылкой данных.
- UDP-сокеты: приём с колбэком, отправка на адрес или широковещательно.
- Событие "сеть поднялась / упала" с текущим IP.
- COM-порт: `USB_COM_Transmit()` без ожидания (`HAL_BUSY`, если не помещается), обработчик
  приёма `USB_COM_SetRxCallback()` или чтение `USB_COM_Read()`, признак "порт открыт на ПК".
- Включение и выключение в любой момент: `USB_ETH_Init/DeInit`, `USB_COM_Init/DeInit`.
- Безопасность использования:
  - все колбэки вызываются только из `USB_Process()` в главном цикле, никогда из прерывания;
  - вызов API из прерывания не ломает стек, а возвращает `HAL_ERROR`;
  - `on_disconnect` гарантированно вызывается ровно один раз на каждое соединение - при
    закрытии любой стороной, сбросе, пропаже клиента (TCP keepalive, ~8 с), отключении USB и
    `USB_ETH_DeInit()`;
  - без `malloc`: статические пулы серверов, соединений и сокетов;
  - ни одного бесконечного ожидания: передача в USB ограничена таймаутом.
- MAC-адрес и серийный номер USB вычисляются из уникального ID чипа - несколько плат в одной
  сети не конфликтуют.
- Без сети прошивка не содержит lwIP: при COM без `USB_ETH_Init()` линковщик его выбрасывает.
- Опциональная запись событий в [stm32_logger](../stm32_logger) одним переключателем
  `USB_DEV_LOG_ENABLE`: подключение к ПК и отключение, открытие и закрытие COM-порта на ПК,
  подключение и обрыв TCP-соединений, все ошибки библиотеки, TinyUSB и lwIP.

## На каких микроконтроллерах работает

Сеть и COM вместе требуют от USB-контроллера 5 конечных точек (EP0 и по две IN-точки на каждое
устройство). Где их меньше, работает любое одно устройство, а `Init` второго возвращает
`HAL_ERROR` - первое при этом работает дальше. Точный ответ для своей сборки даёт
`USB_IsCompositeSupported()`.

| Микроконтроллер | USB-контроллер | Сеть + COM | Проверено на железе |
|---|---|---|---|
| STM32H743/H753/H745/H747/H750 | OTG_FS (9 точек) | да | H743, Windows 10: сеть, COM, оба, `Init`/`DeInit` на лету, выдёргивание кабеля |
| STM32H72x/H73x/H7Ax/H7Bx | OTG_HS во встроенном FS PHY (9) | да | нет |
| **STM32F446** | OTG_FS (6) или OTG_HS (9) | да | нет (сборка и линковка проверены) |
| STM32F412, F413/F423, F469/F479 | OTG_FS (6) | да | нет |
| STM32F7xx | OTG_FS (6) | да | нет |
| STM32F405/F407/F415/F417/F427/F429/F437/F439 | OTG_HS во встроенном FS PHY (6) | да | нет |
| STM32F405/F407/F415/F417/F427/F429/F437/F439 | OTG_FS (4) | одно устройство | нет |
| STM32F401, F411, F2xx | OTG_FS (4) | одно устройство | нет |
| STM32 с USB FS device (F0, F1, G0, G4, L0, L4 без OTG) | USB FS | одно устройство* | нет |

Число точек - по CMSIS из STM32Cube (`USB_OTG_FS_MAX_IN_ENDPOINTS`) и описаниям контроллеров в
TinyUSB; для F446 и F401 ответ `USB_IsCompositeSupported()` проверен сборкой.
\* для USB FS device сеть и COM вместе не проверялись, поэтому библиотека разрешает только одно.

## Требования к настройке в CubeMX

Краткая сводка; подробное описание каждой настройки, допустимых значений и выбора контроллера
под конкретную серию STM32 - в [CUBEMX_SETUP.md](CUBEMX_SETUP.md).

| Раздел CubeMX | Настройка | Зачем |
|---|---|---|
| Connectivity -> USB_OTG_FS | Mode: **Device_Only** | USB-периферия в режиме устройства |
| USB_OTG_FS -> Parameter Settings | **VBUS sensing: Disabled** (если пин VBUS не разведён) | иначе плата не определяется ПК; при разведённом VBUS - Enabled и `-DUSB_DEV_VBUS_SENSING=1` |
| USB_OTG_FS -> NVIC | **USB On The Go FS global interrupt: Enabled** | CubeMX создаст `OTG_FS_IRQHandler`, приоритет задаётся здесь |
| Middleware -> USB_DEVICE | **Не включать** (в т.ч. "Communication Device Class") | стек USB - TinyUSB, а не ST USB Device |
| Clock Configuration | тактирование USB **ровно 48 МГц** | H7 - HSI48; F4 - PLLQ или PLLSAI |

Функцию `MX_USB_OTG_FS_PCD_Init()` нужно по-прежнему вызывать из `main()` до `USB_ETH_Init()` /
`USB_COM_Init()`: в ней CubeMX включает тактирование USB и настраивает выводы PA11/PA12.

## Быстрый старт

### 1. Зависимости

| Библиотека | Версия (проверено) | Откуда |
|---|---|---|
| TinyUSB | 0.21.0 | <https://github.com/hathach/tinyusb> |
| lwIP | 2.2.1 | <https://github.com/lwip-tcpip/lwip> |
| stm32_logger | 1.13+ | только при `USB_DEV_LOG_ENABLE=1` (таблица кодов `0x42`/`0x43` с 1.13) |

TinyUSB и lwIP используются без изменений - вся их настройка лежит в папке `port/` этой
библиотеки.

### 2. Исходники и пути

Добавьте в сборку (CMake `target_sources` или STM32CubeIDE -> "Add to build") - все файлы, даже
если нужен только COM-порт (лишнее уберёт линковщик при `-Wl,--gc-sections`, это настройка
CubeIDE по умолчанию):

```text
usb_eth/usb_dev.c  usb_eth/usb_dev_desc.c  usb_eth/usb_com.c
usb_eth/usb_eth.c  usb_eth/usb_eth_sock.c

tinyusb/src/tusb.c
tinyusb/src/common/tusb_fifo.c
tinyusb/src/device/usbd.c
tinyusb/src/class/cdc/cdc_device.c
tinyusb/src/class/net/ncm_device.c
tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c
tinyusb/src/portable/synopsys/dwc2/dwc2_common.c
tinyusb/lib/networking/dhserver.c

lwip/src/core/*.c
lwip/src/core/ipv4/*.c
lwip/src/netif/ethernet.c
```

Пути include: `usb_eth/`, `usb_eth/port/`, `tinyusb/src/`, `tinyusb/lib/networking/`,
`lwip/src/include/` (и папку `stm32_logger`, если логирование включено).

Если в проекте включён `-Werror`, для файлов TinyUSB/lwIP его стоит отключить
(`-Wno-error`): их исходники содержат безобидные предупреждения, а править чужой код не нужно.

### 3. Параметры

Все параметры - **глобальные define проекта** (CMake `target_compile_definitions`,
STM32CubeIDE -> Properties -> C/C++ Build -> Settings -> MCU GCC Compiler -> Preprocessor), а не
`#define` в коде: их должны одинаково видеть и библиотека, и исходники TinyUSB/lwIP. Без единого
define всё работает: сеть в режиме HOST, USB-порт OTG_FS.

| Define | По умолчанию | Назначение |
|---|---|---|
| `USB_DEV_RHPORT` | `0` | `0` - OTG_FS, `1` - OTG_HS во встроенном FS PHY; на H7 с одним USB (H72x/H73x/H7Ax/H7Bx) OTG_HS - это порт `0` |
| `USB_DEV_VBUS_SENSING` | `0` | `1`, если пин VBUS разведён и включён в CubeMX |
| `USB_DEV_VID` | `0xCAFE` | для серийного изделия - свой VID |
| `USB_DEV_PID_ETH` / `_COM` / `_ETH_COM` | `0x4011` / `0x4012` / `0x4013` | PID для "только сеть" / "только COM" / "оба" |
| `USB_DEV_STR_MANUFACTURER` / `USB_DEV_STR_PRODUCT` | `"Mechanic"` / `"STM32 USB Device"` | имена в диспетчере устройств |
| `USB_DEV_REENUM_MS` | `300` | пауза отключения от ПК при смене набора устройств, мс |
| `USB_COM_RX_BUF_SIZE` | `512` | буфер приёма COM, байт |
| `USB_COM_TX_BUF_SIZE` | `1024` | буфер передачи COM, байт |
| `USB_ETH_MODE` | `USB_ETH_MODE_HOST` | `USB_ETH_MODE_HOST` - плата раздаёт IP; `USB_ETH_MODE_CLIENT` - получает IP по DHCP |
| `USB_ETH_HOST_IP` | `USB_ETH_IP4(192,168,7,1)` | IP платы в режиме HOST (маска /24, ПК получит .2) |
| `USB_ETH_HOSTNAME` | `"stm32-usb-eth"` | имя платы для DHCP-сервера роутера (режим CLIENT) |
| `USB_ETH_MAX_TCP_SERVERS` | `4` | сколько TCP-портов можно слушать одновременно |
| `USB_ETH_MAX_TCP_CONNECTIONS` | `4` | одновременных TCP-соединений (на все порты) |
| `USB_ETH_MAX_UDP_SOCKETS` | `4` | открытых UDP-портов |
| `USB_ETH_RX_PBUF_POOL_SIZE` | `16` | буферов приёма lwIP (~1.5 КБ ОЗУ каждый); см. "Диагностика потерь" |
| `USB_ETH_MAC_ADDR` | не задан (из UID) | фиксированный MAC, например `{0x02,0x11,0x22,0x33,0x44,0x56}` |
| `USB_DEV_LOG_ENABLE` | `0` | `1` - писать события в stm32_logger (можно поменять прямо в начале `usb_dev_opts.h`) |
| `USB_DEV_LOG_MIN_INTERVAL_MS` | `1000` | ошибки одного кода пишутся в лог не чаще, мс; пропущенные считаются |

Пример для CMake:

```cmake
target_compile_definitions(${PROJECT_NAME} PRIVATE USB_ETH_MODE=USB_ETH_MODE_CLIENT)
```

Переход с версии 1.x: `USB_ETH_Process()` -> `USB_Process()`, `USB_ETH_IRQHandler()` ->
`USB_IRQHandler()`, define `USB_ETH_RHPORT`, `USB_ETH_VBUS_SENSING`, `USB_ETH_USB_VID`,
`USB_ETH_USB_PID`, `USB_ETH_STR_*` -> `USB_DEV_RHPORT`, `USB_DEV_VBUS_SENSING`, `USB_DEV_VID`,
`USB_DEV_PID_ETH`, `USB_DEV_STR_*`, `USB_ETH_LOG_ENABLE` -> `USB_DEV_LOG_ENABLE` (старые имена
дают ошибку компиляции, а не тихо игнорируются); файл `usb_eth_desc.c` заменён на
`usb_dev_desc.c`, добавлены `usb_dev.c`, `usb_com.c` и `tinyusb/src/class/cdc/cdc_device.c`.

Ресурсы (STM32H743, `-Os`): сеть + COM - около 37 КБ flash и 52 КБ ОЗУ, только COM - около
12 КБ flash и 7 КБ ОЗУ. Основная часть ОЗУ сети - буферы lwIP (`USB_ETH_RX_PBUF_POOL_SIZE` и
16 КБ кучи lwIP).

### 4. Код

`main.c` - сеть и COM-порт вместе:

```c
/* USER CODE BEGIN Includes */
#include "usb_eth.h"
#include "usb_com.h"
/* USER CODE END Includes */

/* USER CODE BEGIN 0 */
static void App_OnNet(bool is_up, uint32_t ip)
{
    /* is_up: сеть готова; ip: USB_ETH_IP4_OCTET(ip, 0..3) */
}

static void App_OnTcpReceive(USB_ETH_TcpConn_t *conn, const uint8_t *data, uint16_t len)
{
    (void)USB_ETH_TCP_Send(conn, data, len);   /* эхо по сети */
}

static void App_OnComReceive(const uint8_t *data, uint16_t len)
{
    (void)USB_COM_Transmit(data, len);         /* эхо по COM */
}

static const USB_ETH_TcpHandlers_t s_app_handlers =
{
    .on_connect    = NULL,
    .on_receive    = App_OnTcpReceive,
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
    USB_COM_SetRxCallback(App_OnComReceive);
    USB_COM_Init();
    USB_ETH_Init();                    /* на F401 и т.п. вернёт HAL_ERROR, COM работает дальше */
    USB_ETH_SetNetCallback(App_OnNet);
    USB_ETH_TCP_Listen(7, &s_app_handlers, NULL);
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN 3 */
        USB_Process();                 /* как можно чаще, без задержек в цикле */
        /* USER CODE END 3 */
    }
}
```

`stm32xxxx_it.c` - прерывание USB обслуживает TinyUSB, обработчик HAL вызываться не должен.
Код в секции USER CODE переживает перегенерацию CubeMX:

```c
void OTG_FS_IRQHandler(void)
{
    /* USER CODE BEGIN OTG_FS_IRQn 0 */
    USB_IRQHandler();
    return;
    /* USER CODE END OTG_FS_IRQn 0 */
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
    /* USER CODE BEGIN OTG_FS_IRQn 1 */
    /* USER CODE END OTG_FS_IRQn 1 */
}
```

Логирование в stm32_logger включается одной строкой в начале `usb_dev_opts.h`
(`#define USB_DEV_LOG_ENABLE 1`) или глобальным `-DUSB_DEV_LOG_ENABLE=1`. Тогда `LOGGER_Init()`
вызывается до `USB_ETH_Init()` / `USB_COM_Init()`, а в `logger_codes.h` проекта определяются
`LOGGER_ENABLE_USB_ETH` (события сети, коды `0x42xx`) и `LOGGER_ENABLE_USB_DEV` (USB и COM,
`0x43xx`). Коды - в `API_REFERENCE.md`. Ошибки одного кода пишутся не чаще раза в
`USB_DEV_LOG_MIN_INTERVAL_MS`, пропущенные считаются. Если логгер сам выводит записи в этот же
COM-порт или в сеть, зацикливания нет: запись изнутри другой записи не выполняется, а считается.

### 5. Переход с COM-порта CubeMX (USB_DEVICE -> CDC)

| Было (CubeMX) | Стало |
|---|---|
| Middleware -> USB_DEVICE -> Communication Device Class | выключить USB_DEVICE; USB_OTG_FS -> Device_Only, прерывание включено |
| `MX_USB_DEVICE_Init()` | `MX_USB_OTG_FS_PCD_Init()` + `USB_COM_Init()` |
| `CDC_Transmit_FS(buf, len)` | `USB_COM_Transmit(buf, len)` |
| `USBD_OK` / `USBD_BUSY` / `USBD_FAIL` | `HAL_OK` / `HAL_BUSY` / `HAL_ERROR` |
| свой код в `CDC_Receive_FS()` в `usbd_cdc_if.c` | функция-обработчик + `USB_COM_SetRxCallback()`, или чтение `USB_COM_Read()` в цикле |
| ничего в цикле | `USB_Process()` в `while (1)` |

Отличия в лучшую сторону: данные копируются (буфер можно менять сразу после вызова), очередь
передачи - `USB_COM_TX_BUF_SIZE` байт, а не один пакет; обработчик приёма вызывается из главного
цикла, а не из прерывания, - в нём можно делать что угодно.

### 6. Примеры

| Файл | Что показывает |
|---|---|
| `examples/example_echo.c` | минимальная интеграция сети: TCP эхо-сервер, индикация состояния сети |
| `examples/example_cmd_server.c` | построчные команды из потока TCP, контекст на клиента, большой ответ порциями через `on_sent`, закрытие по команде |
| `examples/example_udp_discovery.c` | поиск платы в сети широковещательным UDP-запросом (для режима CLIENT) |
| `examples/example_com.c` | COM-порт: эхо через обработчик, периодическая отправка, сеть "по возможности", чтение без обработчика |

### 7. Проверка с компьютера

Сеть, режим HOST: после подключения кабеля в системе появляется сетевой адаптер, ПК получает
адрес `192.168.7.2`.

```bash
ping 192.168.7.1
ncat 192.168.7.1 7
```

Сеть, режим CLIENT: плата получает адрес от DHCP-сервера сети. Варианты подключения - роутер с
USB-портом и драйвером CDC-NCM (например, OpenWrt с пакетом `kmod-usb-net-cdc-ncm`, интерфейс
`usb0` добавляется в мост LAN) или ПК с общим доступом к интернету (ICS) на USB-адаптер. IP платы
виден в списке клиентов роутера (по имени `USB_ETH_HOSTNAME`) или находится примером
UDP-обнаружения.

COM-порт: в диспетчере устройств Windows появляется "Последовательное устройство USB (COMx)",
в Linux - `/dev/ttyACM0`. Открыть любым терминалом (PuTTY, Tera Term, `screen`); скорость в
терминале не важна.

Если Windows не видит устройство:

- в диспетчере устройств устройство с восклицательным знаком - Windows запомнила старый драйвер
  для этого VID/PID после изменений дескрипторов; удалите устройство (`pnputil /remove-device`)
  или смените PID;
- не используйте Zadig/libwdi для этого устройства: установленный им WinUSB-драйвер имеет
  приоритет над встроенными драйверами. Проверка:
  `Get-ChildItem C:\Windows\INF\oem*.inf | Select-String "VID_CAFE"`; найденные пакеты удалить
  `pnputil /delete-driver oemNN.inf /uninstall`.

## Модель событий

- **Контекст.** Все колбэки (сети и COM) вызываются из `USB_Process()`. Внутри колбэков можно
  вызывать любые функции API (отправку, закрытие, открытие портов, `Init`/`DeInit`). Функции API
  нельзя вызывать из прерываний - они вернут `HAL_ERROR`/`NULL`/`0`.
- **Набор устройств.** `Init`/`DeInit` только запоминают, какие устройства нужны; ПК узнаёт об
  этом в ближайшем `USB_Process()`. Если оба `Init` вызваны до главного цикла, ПК сразу видит
  оба устройства. Если набор меняется, когда ПК уже видит плату, она отключается от ПК на
  `USB_DEV_REENUM_MS` и подключается заново: TCP-соединения закрываются
  (`USB_ETH_CLOSE_LINK_LOST`), открытый на ПК COM-порт закрывается, - программам на ПК нужно
  переподключиться. Учтите:
  - у каждого набора свой PID, поэтому Windows считает их разными устройствами и даёт COM-порту
    разные номера (например, COM10 в наборе "сеть + COM" и COM11 в наборе "только COM").
    Программа на ПК должна искать порт по VID/PID, а не по номеру;
  - данные, отправленные по COM или сети непосредственно перед `Init`/`DeInit`, меняющим набор,
    до ПК могут не дойти: плата отключается раньше, чем ПК их заберёт. Если ответ на команду
    важен, меняйте набор после его доставки (например, через 100-200 мс).
- **Сеть.** `on_net(true, ip)` - USB подключён и IP назначен (HOST: сразу после подключения
  кабеля; CLIENT: после ответа DHCP). `on_net(false, 0)` - кабель отключён (или ПК уснул: без
  VBUS sensing плата их не различает), адрес потерян или вызван `USB_ETH_DeInit()`; все
  открытые TCP-соединения при этом закрываются с `USB_ETH_CLOSE_LINK_LOST`. Порты, открытые
  через `Listen`/`Bind`, продолжают работать после повторного подключения кабеля - открывать их
  заново не нужно. `USB_ETH_DeInit()` удаляет порты:
  после нового `USB_ETH_Init()` их открывают заново.
- **Жизнь соединения.** `on_connect` -> (`on_receive` / `on_sent`)* -> `on_disconnect`. После
  возврата из `on_disconnect` указатель `conn` недействителен (слот будет выдан следующему
  клиенту). Поле `conn->user_ctx` - для своих данных клиента.
- **TCP и COM - поток байт.** Одно сообщение может прийти несколькими вызовами обработчика,
  несколько сообщений - одним. Протокол должен сам определять границы (перевод строки, длина в
  заголовке) - см. `example_cmd_server.c`.
- **Отправка.** `USB_ETH_TCP_Send` и `USB_COM_Transmit` копируют данные целиком или не принимают
  ничего (`HAL_BUSY`). Большие объёмы отправляются порциями: сколько примет сейчас
  (`USB_ETH_TCP_GetFreeSpace` / `USB_COM_GetFreeSpace`), остальное - позже (для TCP - в
  `on_sent`).
- **COM-порт не открыт на ПК.** Пока программа на ПК не открыла порт (сигнал DTR),
  `USB_COM_Transmit` отбрасывает данные и возвращает `HAL_OK` - как UART, к которому ничего не
  подключено. При открытии и закрытии порта буфер передачи очищается, поэтому ПК получает только
  данные, отправленные после открытия, начиная с целого вызова `Transmit`. Исключение: если порт
  закрыли посреди USB-передачи, после открытия первым придёт её недоставленный хвост (до
  `USB_COM_TX_BUF_SIZE` байт, может начинаться с середины строки) - начатую передачу прервать
  нельзя. Пока он не ушёл, `Transmit` возвращает `HAL_BUSY`. Узнать, открыт ли порт, -
  `USB_COM_IsOpen()`. Программа на ПК должна выставлять DTR:
  PuTTY, Tera Term, pyserial делают это сами, в .NET `SerialPort` нужно `DtrEnable = true`.

## Диагностика потерь сети

Признак потерь кадров - редкие задержки ответа около 300 мс (Windows досылает потерянный
TCP-сегмент через ~300 мс). Где теряется, покажет `USB_ETH_GetStats()`:

| Растёт счётчик | Причина | Что делать |
|---|---|---|
| `rx_drop_no_pbuf` | кончились буферы приёма lwIP | увеличить `USB_ETH_RX_PBUF_POOL_SIZE` |
| `tx_drop_timeout` | ПК не забирает данные из USB дольше 20 мс | обычно - ПК занят; разовые случаи не страшны |
| `rx_backpressure` | `USB_Process()` вызывается редко, USB притормаживает ПК | потерь нет, но растёт задержка - вызывать `USB_Process` чаще |

Размер пула: каждый входящий кадр (даже 60 байт) занимает целый буфер. Одновременно их держат
очередь приёма (до 4) и сегменты TCP, пришедшие не по порядку (до окна 4 x 1460 байт на
соединение). Если `pbuf_pool_max_used` достигает `pbuf_pool_size`, пул стоит увеличить.

## Честные ограничения

- На железе проверено только на STM32H743 (OTG_FS, режим HOST, Windows 10): сеть, COM-порт,
  оба вместе, `Init` в любом порядке, `DeInit`/`Init` на лету, выдёргивание кабеля. STM32F446
  собран и слинкован; F446 и другие серии, режим CLIENT, хосты Linux/macOS/роутеры на реальном
  железе не испытывались.
- Только Full Speed (12 Мбит/с) - сеть и COM делят эту полосу; реальная скорость TCP - единицы
  Мбит/с. Пока COM молчит, сеть ничего не теряет.
- COM-порт один; сигналы DTR/RTS только читаются (`USB_COM_IsOpen`), BREAK и управление
  потоком не поддерживаются.
- Только IPv4. Нет DNS-клиента и исходящих TCP-соединений (плата - только TCP-сервер; UDP -
  в обе стороны).
- Без RTOS: библиотека рассчитана на главный цикл. С RTOS все вызовы API и `USB_Process()`
  должны выполняться из одной задачи.
- Отдельный TCP-порт или UDP-сокет нельзя закрыть - только все сразу через `USB_ETH_DeInit()`.
- Режим HOST: маска всегда /24, ПК выдаются адреса `.2` и `.3`, без шлюза и DNS - через плату
  нет доступа в интернет (это и не требуется).
- Режим CLIENT без DHCP-сервера в сети адрес не получит (статический адрес в этом режиме не
  поддерживается).
- Размер одной датаграммы UDP - до 1472 байт; фрагментация IP не поддерживается.
- Под нагрузкой мелкими TCP-сегментами изредка (~0,2% обменов на H743 + Windows) ответ
  задерживается на 0,3-1,2 с: Windows досылает потерянный сегмент. Данные не теряются, счётчики
  `USB_ETH_GetStats()` при этом не растут; причина не найдена (предположительно в TinyUSB NCM).
- VID `0xCAFE` - тестовый VID TinyUSB, для изделия нужен собственный VID/PID.

## Лицензия

Библиотека распространяется на условиях **PolyForm Noncommercial License 1.0.0** — свободное
использование, копирование, изменение и распространение для любых НЕКОММЕРЧЕСКИХ целей, при
условии сохранения уведомления об авторских правах. Коммерческое использование требует отдельного
разрешения правообладателя. Полный текст — файл [LICENSE](LICENSE).
