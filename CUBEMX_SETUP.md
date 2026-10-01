# usb_eth — настройка CubeMX

Развёрнутая версия таблицы из раздела `## Требования к настройке в CubeMX` файла `README.md`.
Описаны только настройки, от которых зависит работа usb_eth. Стек USB в библиотеке — TinyUSB: от
CubeMX нужны лишь тактирование, выводы и прерывание USB, поэтому многие параметры периферии ни на
что не влияют — TinyUSB заново настраивает USB-ядро при `USB_ETH_Init()`.

## Какой USB-контроллер выбрать

| Микроконтроллер | Периферия в CubeMX | `USB_ETH_RHPORT` | Обработчик прерывания |
|---|---|---|---|
| STM32H743/H753/H745/H747/H750 и подобные с двумя USB | USB_OTG_FS (PA11/PA12) | `0` | `OTG_FS_IRQHandler` |
| STM32H72x/H73x/H7Ax/H7Bx (один USB) | USB_OTG_HS в режиме Internal FS Phy (PA11/PA12) | `0` | `OTG_HS_IRQHandler` |
| STM32F4/F7 с двумя USB, разведён OTG_FS | USB_OTG_FS | `0` | `OTG_FS_IRQHandler` |
| STM32F4/F7, разведён OTG_HS во встроенном FS PHY (PB14/PB15) | USB_OTG_HS, Internal FS Phy | `1` | `OTG_HS_IRQHandler` |

Проверено на железе только первое сочетание (STM32H743, OTG_FS).

## Connectivity → USB_OTG_FS (или USB_OTG_HS) → Mode

| Параметр CubeMX | Диапазон | Рекомендуется | Пояснение |
|---|---|---|---|
| Mode | Disable / Host_Only / Device_Only / OTG/Dual_Role | **Device_Only** | плата работает только как USB-устройство (сетевой адаптер для ПК) |
| External Phy (только OTG_HS) | Disable / ULPI / Internal FS Phy | **Internal FS Phy** | библиотека работает только на Full Speed через встроенный PHY; внешний ULPI-PHY не поддерживается |
| Activate_SOF | галка | **выключено** | сигнал SOF на вывод не нужен |
| Activate_VBUS | галка | **по разводке** | включать, только если вывод VBUS (PA9 / PB13) разведён на разъём USB |

## Parameter Settings

| Параметр CubeMX | Диапазон | Рекомендуется | Пояснение |
|---|---|---|---|
| Speed | Full Speed 12MBit/s / High Speed | **Full Speed** | скорость, на которой работает TinyUSB в этой библиотеке |
| VBUS sensing | Disabled / Enabled | **Disabled**, если VBUS не разведён | при Enabled без разведённого VBUS контроллер считает, что кабель не подключён, и ПК не видит устройство. Значение должно совпадать с глобальным define `USB_ETH_VBUS_SENSING` (`0` — Disabled, по умолчанию; `1` — Enabled) |
| Signal start of frame | Disabled / Enabled | **Disabled** | не используется |
| Low power | Disabled / Enabled | **Disabled** | режим пониженного потребления USB библиотека не поддерживает |
| Link Power Management | Disabled / Enabled | **Disabled** | не поддерживается |
| Use dedicated end point 1 interrupt | Disabled / Enabled | **Disabled** | TinyUSB обслуживает все конечные точки одним прерыванием |
| Physical interface, Endpoints, DMA | любые | не важно | TinyUSB перенастраивает ядро при `USB_ETH_Init()` |

## NVIC Settings

| Параметр CubeMX | Диапазон | Рекомендуется | Пояснение |
|---|---|---|---|
| USB On The Go FS/HS global interrupt | галка | **включено** | CubeMX создаст функцию `OTG_FS_IRQHandler` / `OTG_HS_IRQHandler`, в её секцию `USER CODE BEGIN ... 0` вставляется `USB_ETH_IRQHandler(); return;`. Если галку не ставить, обработчик нужно написать вручную в `USER CODE BEGIN 1` того же `*_it.c` |
| Preemption Priority | 0..15 | **5..10** (ниже SysTick, если от него зависят тайминги) | в прерывании выполняется только короткий разбор событий USB, сеть обслуживается в главном цикле; критичных требований к приоритету нет |

Включает прерывание в NVIC сама TinyUSB при `USB_ETH_Init()`, а приоритет берётся из CubeMX.
Вызывать `HAL_PCD_IRQHandler()` нельзя: HAL и TinyUSB будут одновременно обрабатывать одни и те же
флаги USB.

## Middleware

| Параметр CubeMX | Рекомендуется | Пояснение |
|---|---|---|
| USB_DEVICE (ST USB Device Library) | **не включать** | стек USB — TinyUSB; два стека на одной периферии несовместимы |
| LWIP (из CubeMX) | **не включать** | используется lwIP из состава зависимостей библиотеки с её собственным `port/lwipopts.h` |
| FREERTOS | не обязателен | библиотека рассчитана на главный цикл; с RTOS все вызовы usb_eth — из одной задачи |

## Clock Configuration

| Параметр | Диапазон | Рекомендуется | Пояснение |
|---|---|---|---|
| Частота тактирования USB (вход мультиплексора USB) | ровно **48 МГц** | 48 МГц | USB Full Speed требует 48 МГц с точностью около ±0,25%; при другой частоте ПК не определит устройство или будут ошибки передачи |
| Источник 48 МГц на STM32H7 | PLL1Q / PLL3Q / HSI48 | **HSI48** | независим от остальных делителей PLL — не нужно менять тактирование другой периферии. Точности HSI48 на H743 хватило без CRS |
| Источник 48 МГц на STM32F4/F7 | PLL48CLK (PLLQ / PLLSAI) | **PLLQ с ровно 48 МГц** | на F4 частота PLLQ задаётся делителем Q при выборе SYSCLK — подбирать вместе |
| CRS (Clock Recovery System, при HSI48) | выкл / по SOF USB | **выкл**, включать при сбоях | если появляются периодические ошибки CRC или отвалы USB — включить CRS с синхронизацией по USB SOF |

**Если CubeMX не сохраняет HSI48 для USB на H7**, допишите вручную:

- в `SystemClock_Config()` (секции USER CODE) — флаг `RCC_OSCILLATORTYPE_HSI48` в
  `OscillatorType` и `HSI48State = RCC_HSI48_ON`;
- в `HAL_PCD_MspInit()` — `PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48`.

## Что сгенерированный код делает за библиотеку

Функцию `MX_USB_OTG_FS_PCD_Init()` (или `MX_USB_OTG_HS_PCD_Init()`) нужно по-прежнему вызывать из
`main()` **до** `USB_ETH_Init()`. Сам драйвер HAL PCD библиотека дальше не использует, но в этой
функции через `HAL_PCD_MspInit()` выполняется то, без чего USB не заработает:

- включение тактирования USB-контроллера;
- выбор источника 48 МГц для USB;
- настройка выводов PA11/PA12 (D-/D+) в альтернативную функцию USB;
- на STM32H7 — включение детектора напряжения USB (`HAL_PWREx_EnableUSBVoltageDetector()`).
