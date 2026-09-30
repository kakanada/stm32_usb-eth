# usb_eth — справочник по API

Полный список публичных функций и типов. Справочник — сжатый пересказ комментариев в `usb_eth.h`
и `usb_eth_opts.h`; при расхождениях ориентируйтесь на `.h`-файлы, они первичны. Как подключить
библиотеку — см. `README.md`.

## Оглавление

- [Параметры сборки](#параметры-сборки)
- [Типы](#типы)
- [Инициализация и обслуживание](#инициализация-и-обслуживание)
- [Состояние сети](#состояние-сети)
- [TCP](#tcp)
- [UDP](#udp)
- [Коды логов](#коды-логов)

---

## Параметры сборки

Задаются глобальными define проекта (`-D...`), см. `usb_eth_opts.h` и таблицу в `README.md`.

| Макрос | Назначение |
|---|---|
| `USB_ETH_MODE_HOST` / `USB_ETH_MODE_CLIENT` | значения `USB_ETH_MODE` |
| `USB_ETH_IP4(a, b, c, d)` | IPv4 в `uint32_t` (порядок байт хоста) |
| `USB_ETH_IP4_OCTET(ip, n)` | байт `n` адреса, `0` — старший (для печати `a.b.c.d`) |
| `USB_ETH_IP4_BROADCAST` | `0xFFFFFFFF` — широковещательный адрес для UDP |

Все IP-адреса в API — `uint32_t` в порядке байт хоста: `192.168.7.1` = `USB_ETH_IP4(192,168,7,1)`
= `0xC0A80701`.

## Типы

### `USB_ETH_CloseReason_t`

| Значение | Когда |
|---|---|
| `USB_ETH_CLOSE_LOCAL` | закрыто вызовом `USB_ETH_TCP_Close()` |
| `USB_ETH_CLOSE_REMOTE` | клиент корректно закрыл соединение |
| `USB_ETH_CLOSE_RESET` | клиент сбросил соединение (RST) |
| `USB_ETH_CLOSE_TIMEOUT` | клиент пропал: keepalive (~8 с) или повторы передачи исчерпаны |
| `USB_ETH_CLOSE_LINK_LOST` | пропала сеть: отключён USB или потерян IP |
| `USB_ETH_CLOSE_ERROR` | прочая ошибка стека |

### `USB_ETH_TcpHandlers_t`

Обработчики TCP-сервера. Любой может быть `NULL`. Структура копируется при `Listen`.

| Поле | Сигнатура | Когда вызывается |
|---|---|---|
| `on_connect` | `void (USB_ETH_TcpConn_t *conn)` | клиент подключился |
| `on_receive` | `void (USB_ETH_TcpConn_t *conn, const uint8_t *data, uint16_t len)` | пришли данные; `data` действителен только внутри вызова; границы сообщений не сохраняются |
| `on_sent` | `void (USB_ETH_TcpConn_t *conn, uint16_t acked_len)` | клиент подтвердил `acked_len` байт — освободилось место для отправки |
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

Слушающий TCP-порт (пул `USB_ETH_MAX_TCP_SERVERS`). Поля `port`, `handlers`, `user_ctx` — только
чтение; `pcb`, `used` — внутренние.

### `USB_ETH_UdpSocket_t`

UDP-порт (пул `USB_ETH_MAX_UDP_SOCKETS`). Поля `port`, `on_receive`, `user_ctx` — только чтение;
`pcb`, `used` — внутренние.

### `USB_ETH_UdpReceive_t`

`void (USB_ETH_UdpSocket_t *sock, const uint8_t *data, uint16_t len, uint32_t remote_ip,
uint16_t remote_port)` — принята датаграмма; `data` действителен только внутри вызова.

### `USB_ETH_NetCallback_t`

`void (bool is_up, uint32_t ip)` — сеть готова (`is_up = true`, `ip` — адрес платы) или потеряна
(`false`, `0`).

## Инициализация и обслуживание

### `HAL_StatusTypeDef USB_ETH_Init(void)`

Запускает TinyUSB, lwIP, DHCP-сервер (HOST) или DHCP-клиент (CLIENT). Вызывать один раз после
`MX_USB_OTG_FS_PCD_Init()`.

| Возврат | Значение |
|---|---|
| `HAL_OK` | успех, либо библиотека уже инициализирована |
| `HAL_ERROR` | ошибка запуска USB/lwIP/DHCP или вызов из прерывания |

### `void USB_ETH_Process(void)`

Обслуживание стека. Вызывать в `while(1)` как можно чаще. Единственное место, откуда вызываются
все колбэки библиотеки. До `USB_ETH_Init()` и из прерывания ничего не делает.

### `void USB_ETH_IRQHandler(void)`

Вызывать из `OTG_FS_IRQHandler()` (или `OTG_HS_IRQHandler()` при `USB_ETH_RHPORT=1`, а также на
STM32H7 с единственным USB — H72x/H73x/H7Ax/H7Bx, где OTG_HS работает как порт `0`) вместо
`HAL_PCD_IRQHandler()`.

## Состояние сети

### `void USB_ETH_SetNetCallback(USB_ETH_NetCallback_t cb)`

| Параметр | Описание |
|---|---|
| `cb` | обработчик изменения состояния сети; `NULL` — отключить |

Если сеть уже готова, `cb(true, ip)` будет вызван при ближайшем `USB_ETH_Process()`.

### `bool USB_ETH_IsNetUp(void)`

`true` — USB подключён и IP назначен.

### `uint32_t USB_ETH_GetIp(void)`

Собственный IP платы или `0`, если сеть не готова.

## TCP

### `USB_ETH_TcpServer_t *USB_ETH_TCP_Listen(uint16_t port, const USB_ETH_TcpHandlers_t *handlers, void *user_ctx)`

Открывает порт на приём подключений. Можно вызывать сразу после `USB_ETH_Init()`, не дожидаясь
сети; порт продолжает работать после переподключения USB.

| Параметр | Описание |
|---|---|
| `port` | 1..65535 |
| `handlers` | обработчики (копируются), не `NULL` |
| `user_ctx` | значение по умолчанию для `conn->user_ctx` |

Возврат: сервер или `NULL` (неверные параметры, пул занят, ошибка lwIP, до `Init`, из прерывания).
Повторный вызов с тем же портом возвращает существующий сервер без изменения обработчиков.
Если свободных слотов соединений нет, новый клиент отклоняется (сброс соединения).

### `HAL_StatusTypeDef USB_ETH_TCP_Send(USB_ETH_TcpConn_t *conn, const void *data, uint16_t len)`

Ставит данные в очередь отправки — целиком или ничего. Данные копируются.

| Возврат | Значение |
|---|---|
| `HAL_OK` | данные в очереди (`len = 0` — тоже `HAL_OK`) |
| `HAL_BUSY` | нет места — повторить позже (например, в `on_sent`) |
| `HAL_ERROR` | соединение закрыто, `data = NULL`, вызов из прерывания |

### `HAL_StatusTypeDef USB_ETH_TCP_SendString(USB_ETH_TcpConn_t *conn, const char *str)`

`USB_ETH_TCP_Send()` для строки с завершающим нулём (ноль не отправляется).

### `uint16_t USB_ETH_TCP_GetFreeSpace(const USB_ETH_TcpConn_t *conn)`

Сколько байт сейчас гарантированно примет `USB_ETH_TCP_Send()`; `0`, если соединение закрыто.

### `bool USB_ETH_TCP_IsOpen(const USB_ETH_TcpConn_t *conn)`

`true` — соединение открыто и с ним можно работать.

### `HAL_StatusTypeDef USB_ETH_TCP_Close(USB_ETH_TcpConn_t *conn)`

Закрывает соединение; данные, уже поставленные в очередь, будут досланы. `on_disconnect` с
`USB_ETH_CLOSE_LOCAL` вызывается прямо внутри этой функции.

| Возврат | Значение |
|---|---|
| `HAL_OK` | закрыто |
| `HAL_ERROR` | уже закрыто, `NULL`, вызов из прерывания |

## UDP

### `USB_ETH_UdpSocket_t *USB_ETH_UDP_Bind(uint16_t port, USB_ETH_UdpReceive_t on_receive, void *user_ctx)`

| Параметр | Описание |
|---|---|
| `port` | 1..65535 |
| `on_receive` | обработчик датаграмм; `NULL` — сокет только для отправки |
| `user_ctx` | данные пользователя |

Возврат: сокет или `NULL`. Повторный вызов с тем же портом возвращает существующий сокет.
Принимаются и широковещательные датаграммы.

### `HAL_StatusTypeDef USB_ETH_UDP_SendTo(USB_ETH_UdpSocket_t *sock, uint32_t ip, uint16_t port, const void *data, uint16_t len)`

| Параметр | Описание |
|---|---|
| `sock` | сокет (определяет порт отправителя) |
| `ip` | IP получателя; `USB_ETH_IP4_BROADCAST` — всем |
| `port` | порт получателя, не `0` |
| `data`, `len` | данные, 1..1472 байт (копируются) |

| Возврат | Значение |
|---|---|
| `HAL_OK` | датаграмма отправлена |
| `HAL_BUSY` | нет памяти lwIP |
| `HAL_ERROR` | сеть не готова, неверные параметры, вызов из прерывания |

## Коды логов

При `USB_ETH_LOG_ENABLE=1` события пишутся через `LOGGER_Log(code, source_id, value)`.
Адресное пространство `0x42` закреплено в stm32_logger (`LOGGER_ENABLE_USB_ETH`).

| Код | Имя (`USB_ETH_LOG_CODE_*` / `LOG_CODE_USB_ETH_*`) | Приоритет | `source_id` | `value` |
|---|---|---|---|---|
| `0x4200` | `INIT_OK` | LOW | 0 | режим (0 HOST, 1 CLIENT) |
| `0x4201` | `INIT_FAIL` | HIGH | 0 | этап: 1 USB, 2 netif, 3 DHCP |
| `0x4202` | `USB_MOUNTED` | LOW | 0 | 0 |
| `0x4203` | `USB_UNMOUNTED` | MEDIUM | 0 | 0 |
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
