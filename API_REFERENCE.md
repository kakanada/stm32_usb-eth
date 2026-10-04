# usb_eth - справочник по API

Полный список публичных функций и типов. Справочник - сжатый пересказ комментариев в `usb_dev.h`,
`usb_eth.h`, `usb_com.h` и `usb_dev_opts.h`; при расхождениях ориентируйтесь на `.h`-файлы, они
первичны. Как подключить библиотеку - см. `README.md`.

## Оглавление

- [Параметры сборки](#параметры-сборки)
- [Общие функции (usb_dev.h)](#общие-функции-usb_devh)
- [Сеть: типы (usb_eth.h)](#сеть-типы-usb_ethh)
- [Сеть: включение и состояние](#сеть-включение-и-состояние)
- [Сеть: TCP](#сеть-tcp)
- [Сеть: UDP](#сеть-udp)
- [COM-порт (usb_com.h)](#com-порт-usb_comh)
- [Коды логов](#коды-логов)

---

## Параметры сборки

Задаются глобальными define проекта (`-D...`), см. `usb_dev_opts.h` и таблицу в `README.md`.
Исключение - `USB_DEV_LOG_ENABLE`: его можно поменять и прямо в начале `usb_dev_opts.h`.

| Макрос | Назначение |
|---|---|
| `USB_ETH_MODE_HOST` / `USB_ETH_MODE_CLIENT` | значения `USB_ETH_MODE` |
| `USB_ETH_IP4(a, b, c, d)` | IPv4 в `uint32_t` (порядок байт хоста) |
| `USB_ETH_IP4_OCTET(ip, n)` | байт `n` адреса, `0` - старший (для печати `a.b.c.d`) |
| `USB_ETH_IP4_BROADCAST` | `0xFFFFFFFF` - широковещательный адрес для UDP |

Все IP-адреса в API - `uint32_t` в порядке байт хоста: `192.168.7.1` = `USB_ETH_IP4(192,168,7,1)`
= `0xC0A80701`.

## Общие функции (usb_dev.h)

Подключаются и через `usb_eth.h`, и через `usb_com.h`.

### `void USB_Process(void)`

Обслуживание USB, сети и COM-порта. Вызывать в `while(1)` как можно чаще. Единственное место,
откуда вызываются все колбэки библиотеки; здесь же применяется смена набора устройств после
`Init`/`DeInit`. До первого `Init` и из прерывания ничего не делает.

### `void USB_IRQHandler(void)`

Вызывать из `OTG_FS_IRQHandler()` (или `OTG_HS_IRQHandler()` при `USB_DEV_RHPORT=1`, а также на
STM32H7 с единственным USB - H72x/H73x/H7Ax/H7Bx, где OTG_HS работает как порт `0`) вместо
`HAL_PCD_IRQHandler()`.

### `bool USB_IsCompositeSupported(void)`

`true` - USB-контроллер этого микроконтроллера (порт `USB_DEV_RHPORT`) тянет сеть и COM
одновременно (5+ конечных точек: STM32H7, F446, F412, F413, F7...). `false` - работает одно
устройство, `Init` второго вернёт `HAL_ERROR`, первое продолжит работать.

## Сеть: типы (usb_eth.h)

### `USB_ETH_CloseReason_t`

| Значение | Когда |
|---|---|
| `USB_ETH_CLOSE_LOCAL` | закрыто вызовом `USB_ETH_TCP_Close()` |
| `USB_ETH_CLOSE_REMOTE` | клиент корректно закрыл соединение |
| `USB_ETH_CLOSE_RESET` | клиент сбросил соединение (RST) |
| `USB_ETH_CLOSE_TIMEOUT` | клиент пропал: keepalive (~8 с) или повторы передачи исчерпаны |
| `USB_ETH_CLOSE_LINK_LOST` | пропала сеть: отключён USB, потерян IP, переподключение USB или `USB_ETH_DeInit()` |
| `USB_ETH_CLOSE_ERROR` | прочая ошибка стека |

### `USB_ETH_TcpHandlers_t`

Обработчики TCP-сервера. Любой может быть `NULL`. Структура копируется при `Listen`.

| Поле | Сигнатура | Когда вызывается |
|---|---|---|
| `on_connect` | `void (USB_ETH_TcpConn_t *conn)` | клиент подключился |
| `on_receive` | `void (USB_ETH_TcpConn_t *conn, const uint8_t *data, uint16_t len)` | пришли данные; `data` действителен только внутри вызова; границы сообщений не сохраняются |
| `on_sent` | `void (USB_ETH_TcpConn_t *conn, uint16_t acked_len)` | клиент подтвердил `acked_len` байт - освободилось место для отправки |
| `on_disconnect` | `void (USB_ETH_TcpConn_t *conn, USB_ETH_CloseReason_t reason)` | соединение закрыто; ровно один раз; после возврата `conn` недействителен |

### `USB_ETH_TcpConn_t`

TCP-соединение (слот статического пула `USB_ETH_MAX_TCP_CONNECTIONS`).

| Поле | Доступ | Описание |
|---|---|---|
| `server` | чтение | сервер, принявший соединение |
| `remote_ip` | чтение | IP клиента |
| `remote_port` | чтение | порт клиента |
| `user_ctx` | чтение/запись | данные пользователя; при подключении = `server->user_ctx` |
| `pcb`, `used` | внутренние | не изменять |

### `USB_ETH_TcpServer_t`

Слушающий TCP-порт (пул `USB_ETH_MAX_TCP_SERVERS`). Поля `port`, `handlers`, `user_ctx` - только
чтение; `pcb`, `used` - внутренние.

### `USB_ETH_UdpSocket_t`

UDP-порт (пул `USB_ETH_MAX_UDP_SOCKETS`). Поля `port`, `on_receive`, `user_ctx` - только чтение;
`pcb`, `used` - внутренние.

### `USB_ETH_UdpReceive_t`

`void (USB_ETH_UdpSocket_t *sock, const uint8_t *data, uint16_t len, uint32_t remote_ip,
uint16_t remote_port)` - принята датаграмма; `data` действителен только внутри вызова.

### `USB_ETH_NetCallback_t`

`void (bool is_up, uint32_t ip)` - сеть готова (`is_up = true`, `ip` - адрес платы) или потеряна
(`false`, `0`).

### `USB_ETH_Stats_t`

Счётчики канала USB <-> lwIP (с момента запуска), см. `USB_ETH_GetStats()`.

| Поле | Описание |
|---|---|
| `rx_frames` | кадров от ПК передано в lwIP |
| `rx_drop_no_pbuf` | кадров от ПК потеряно: кончились буферы приёма |
| `rx_backpressure` | раз очередь приёма была полна - USB притормозил ПК (без потерь) |
| `tx_frames` | кадров отправлено в USB |
| `tx_drop_timeout` | кадров к ПК потеряно: USB занят дольше 20 мс |
| `tx_drop_no_usb` | кадров к ПК потеряно: USB не подключён |
| `pbuf_pool_size` | `USB_ETH_RX_PBUF_POOL_SIZE` |
| `pbuf_pool_max_used` | максимум одновременно занятых буферов приёма |

## Сеть: включение и состояние

### `HAL_StatusTypeDef USB_ETH_Init(void)`

Включает сеть: USB-устройство CDC-NCM, lwIP, DHCP-сервер (HOST) или DHCP-клиент (CLIENT).
Вызывать после `MX_USB_OTG_FS_PCD_Init()`; порядок относительно `USB_COM_Init()` любой. Если COM
уже работает у ПК, плата переподключится (~0,3 с).

| Возврат | Значение |
|---|---|
| `HAL_OK` | успех, либо сеть уже включена |
| `HAL_ERROR` | контроллер не тянет сеть и COM сразу (COM работает дальше); ошибка запуска USB/lwIP/DHCP; вызов из прерывания или из обработчика во время `USB_ETH_DeInit()` |

### `HAL_StatusTypeDef USB_ETH_DeInit(void)`

Выключает сеть: соединения закрываются (`on_disconnect` с `USB_ETH_CLOSE_LINK_LOST`), серверы и
UDP-сокеты удаляются (указатели на них недействительны), `on_net(false, 0)`, сеть пропадает у ПК.
Обработчик `USB_ETH_SetNetCallback()` сохраняется. Вызванная из колбэка сети выполняется по выходе
из него в том же `USB_Process()`.

| Возврат | Значение |
|---|---|
| `HAL_OK` | выключено (или не было включено) |
| `HAL_ERROR` | вызов из прерывания |

### `void USB_ETH_SetNetCallback(USB_ETH_NetCallback_t cb)`

| Параметр | Описание |
|---|---|
| `cb` | обработчик изменения состояния сети; `NULL` - отключить |

Если сеть уже готова, `cb(true, ip)` будет вызван при ближайшем `USB_Process()`.

### `bool USB_ETH_IsNetUp(void)`

`true` - USB подключён и IP назначен.

### `uint32_t USB_ETH_GetIp(void)`

Собственный IP платы или `0`, если сеть не готова.

### `HAL_StatusTypeDef USB_ETH_GetStats(USB_ETH_Stats_t *stats)`

Копирует счётчики (`USB_ETH_Stats_t`). `HAL_ERROR` - `stats = NULL`. Как читать - раздел
"Диагностика потерь сети" в `README.md`.

## Сеть: TCP

### `USB_ETH_TcpServer_t *USB_ETH_TCP_Listen(uint16_t port, const USB_ETH_TcpHandlers_t *handlers, void *user_ctx)`

Открывает порт на приём подключений. Можно вызывать сразу после `USB_ETH_Init()`, не дожидаясь
сети; порт продолжает работать после переподключения кабеля (до `USB_ETH_DeInit()`).

| Параметр | Описание |
|---|---|
| `port` | 1..65535 |
| `handlers` | обработчики (копируются), не `NULL` |
| `user_ctx` | значение по умолчанию для `conn->user_ctx` |

Возврат: сервер или `NULL` (неверные параметры, пул занят, ошибка lwIP, сеть не включена, из
прерывания). Повторный вызов с тем же портом возвращает существующий сервер без изменения
обработчиков. Если свободных слотов соединений нет, новый клиент отклоняется (сброс соединения).

### `HAL_StatusTypeDef USB_ETH_TCP_Send(USB_ETH_TcpConn_t *conn, const void *data, uint16_t len)`

Ставит данные в очередь отправки - целиком или ничего. Данные копируются.

| Возврат | Значение |
|---|---|
| `HAL_OK` | данные в очереди (`len = 0` - тоже `HAL_OK`) |
| `HAL_BUSY` | нет места - повторить позже (например, в `on_sent`) |
| `HAL_ERROR` | соединение закрыто, `data = NULL`, вызов из прерывания |

### `HAL_StatusTypeDef USB_ETH_TCP_SendString(USB_ETH_TcpConn_t *conn, const char *str)`

`USB_ETH_TCP_Send()` для строки с завершающим нулём (ноль не отправляется).

### `uint16_t USB_ETH_TCP_GetFreeSpace(const USB_ETH_TcpConn_t *conn)`

Сколько байт сейчас гарантированно примет `USB_ETH_TCP_Send()`; `0`, если соединение закрыто.

### `bool USB_ETH_TCP_IsOpen(const USB_ETH_TcpConn_t *conn)`

`true` - соединение открыто и с ним можно работать.

### `HAL_StatusTypeDef USB_ETH_TCP_Close(USB_ETH_TcpConn_t *conn)`

Закрывает соединение; данные, уже поставленные в очередь, будут досланы. `on_disconnect` с
`USB_ETH_CLOSE_LOCAL` вызывается прямо внутри этой функции.

| Возврат | Значение |
|---|---|
| `HAL_OK` | закрыто |
| `HAL_ERROR` | уже закрыто, `NULL`, вызов из прерывания |

## Сеть: UDP

### `USB_ETH_UdpSocket_t *USB_ETH_UDP_Bind(uint16_t port, USB_ETH_UdpReceive_t on_receive, void *user_ctx)`

| Параметр | Описание |
|---|---|
| `port` | 1..65535 |
| `on_receive` | обработчик датаграмм; `NULL` - сокет только для отправки |
| `user_ctx` | данные пользователя |

Возврат: сокет или `NULL`. Повторный вызов с тем же портом возвращает существующий сокет.
Принимаются и широковещательные датаграммы.

### `HAL_StatusTypeDef USB_ETH_UDP_SendTo(USB_ETH_UdpSocket_t *sock, uint32_t ip, uint16_t port, const void *data, uint16_t len)`

| Параметр | Описание |
|---|---|
| `sock` | сокет (определяет порт отправителя) |
| `ip` | IP получателя; `USB_ETH_IP4_BROADCAST` - всем |
| `port` | порт получателя, не `0` |
| `data`, `len` | данные, 1..1472 байт (копируются) |

| Возврат | Значение |
|---|---|
| `HAL_OK` | датаграмма отправлена |
| `HAL_BUSY` | нет памяти lwIP |
| `HAL_ERROR` | сеть не готова, неверные параметры, вызов из прерывания |

## COM-порт (usb_com.h)

### `USB_COM_RxCallback_t`

`void (const uint8_t *data, uint16_t len)` - приняты байты (1..64 за вызов), вызывается из
`USB_Process()`; `data` действителен только внутри вызова; границы сообщений не сохраняются.

### `HAL_StatusTypeDef USB_COM_Init(void)`

Включает виртуальный COM-порт (CDC-ACM). Порядок относительно `USB_ETH_Init()` любой; если сеть
уже работает у ПК, плата переподключится (~0,3 с).

| Возврат | Значение |
|---|---|
| `HAL_OK` | успех, либо COM уже включён |
| `HAL_ERROR` | контроллер не тянет сеть и COM сразу (сеть работает дальше), ошибка запуска USB, вызов из прерывания |

### `HAL_StatusTypeDef USB_COM_DeInit(void)`

Выключает COM-порт: он пропадает у ПК (при работающей сети - переподключение без COM).
Обработчик приёма сохраняется. `HAL_OK` всегда, кроме вызова из прерывания (`HAL_ERROR`).

### `void USB_COM_SetRxCallback(USB_COM_RxCallback_t cb)`

Обработчик приёма; `NULL` - данные копятся в буфере (`USB_COM_RX_BUF_SIZE`) и читаются
`USB_COM_Read()`. Пока буфер полон, ПК ждёт - байты не теряются. Можно вызывать в любой момент,
в т.ч. до `Init`.

### `HAL_StatusTypeDef USB_COM_Transmit(const uint8_t *data, uint16_t len)`

Отправка без ожидания - целиком или ничего; данные копируются. Аналог `CDC_Transmit_FS()`.

| Возврат | Значение |
|---|---|
| `HAL_OK` | данные в очереди (`len = 0` - тоже `HAL_OK`) |
| `HAL_BUSY` | не помещаются в буфер передачи - повторить позже |
| `HAL_ERROR` | COM не включён или не подключён к ПК, `data = NULL`, вызов из прерывания |

Если порт на ПК не открыт (нет DTR), данные отбрасываются и возвращается `HAL_OK`; при открытии
и закрытии порта буфер передачи очищается - после открытия ПК получает только новые данные,
начиная с целого вызова. `USB_COM_GetFreeSpace()` при закрытом порте возвращает
`USB_COM_TX_BUF_SIZE`.

### `HAL_StatusTypeDef USB_COM_TransmitString(const char *str)`

`USB_COM_Transmit()` для строки с завершающим нулём (ноль не отправляется).

### `uint16_t USB_COM_GetFreeSpace(void)`

Сколько байт сейчас гарантированно примет `USB_COM_Transmit()`; `0` - COM не подключён.

### `uint16_t USB_COM_Available(void)` / `uint16_t USB_COM_Read(uint8_t *buf, uint16_t max_len)`

Режим без обработчика: сколько принятых байт ждут чтения / прочитать до `max_len` байт
(возврат - прочитано байт, `0` - нет данных, COM не подключён, `buf = NULL`, из прерывания).

### `bool USB_COM_IsReady(void)`

`true` - COM включён и ПК сконфигурировал устройство (`USB_COM_Transmit()` работает).

### `bool USB_COM_IsOpen(void)`

`true` - порт открыт программой на ПК (сигнал DTR).

### `uint32_t USB_COM_GetBaudRate(void)`

Скорость, выбранная в программе на ПК, бит/с (только для информации - на USB не влияет); `0` -
COM не подключён.

## Коды логов

При `USB_DEV_LOG_ENABLE=1` события пишутся через `LOGGER_Log(code, source_id, value)`.

### Сеть - `0x42`, `LOGGER_ENABLE_USB_ETH`

| Код | Имя (`USB_ETH_LOG_CODE_*` / `LOG_CODE_USB_ETH_*`) | Приоритет | `source_id` | `value` |
|---|---|---|---|---|
| `0x4200` | `INIT_OK` | LOW | 0 | режим (0 HOST, 1 CLIENT) |
| `0x4201` | `INIT_FAIL` | HIGH | 0 | этап: 1 USB, 2 netif, 3 DHCP |
| `0x4202` | `USB_MOUNTED` | LOW | 0 | 0 - сеть видна ПК |
| `0x4203` | `USB_UNMOUNTED` | MEDIUM | 0 | 0 - сеть пропала у ПК |
| `0x4204` | `NET_UP` | LOW | 0 | IP платы |
| `0x4205` | `NET_DOWN` | MEDIUM | 0 | 0 |
| `0x4206` | `DHCP_TIMEOUT` | MEDIUM | 0 | мс ожидания (15000) |
| `0x4207` | `TCP_ACCEPT` | LOW | порт | IP клиента |
| `0x4208` | `TCP_CLOSED` | LOW | порт | `USB_ETH_CloseReason_t` |
| `0x4209` | `TCP_POOL_FULL` | HIGH | порт | 0 |
| `0x420A` | `TCP_ERROR` | MEDIUM | порт | код ошибки lwIP |
| `0x420B` | `RX_DROP` | MEDIUM | 0 | длина кадра |
| `0x420C` | `TX_TIMEOUT` | MEDIUM | 0 | длина кадра |
| `0x420D` | `SEND_NO_MEM` | MEDIUM | порт | запрошено байт |
| `0x420E` | `LISTEN_FAIL` | HIGH | порт | код ошибки lwIP |
| `0x420F` | `UDP_BIND_FAIL` | HIGH | порт | код ошибки lwIP |

### USB и COM - `0x43`, `LOGGER_ENABLE_USB_DEV`

| Код | Имя (`USB_DEV_LOG_CODE_*` / `LOG_CODE_USB_DEV_*`) | Приоритет | `source_id` | `value` |
|---|---|---|---|---|
| `0x4300` | `USB_START_FAIL` | HIGH | 0 | 0 |
| `0x4301` | `INIT_REFUSED` | HIGH | устройство: 1 сеть, 2 COM | 0 |
| `0x4302` | `USB_CONNECTED` | LOW | 0 | набор: бит 0 сеть, бит 1 COM |
| `0x4303` | `USB_DISCONNECTED` | MEDIUM | 0 | набор |
| `0x4304` | `REENUM` | LOW | 0 | новый набор |
| `0x4305` | `COM_INIT` | LOW | 0 | 0 |
| `0x4306` | `COM_DEINIT` | LOW | 0 | 0 |
| `0x4307` | `COM_OPEN` | LOW | 0 | скорость, бит/с |
| `0x4308` | `COM_CLOSE` | LOW | 0 | 0 |
| `0x4309` | `COM_TX_BUSY` | MEDIUM | 0 | длина (раз на серию отказов) |
| `0x430A` | `ETH_DEINIT` | LOW | 0 | 0 |
