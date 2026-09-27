/**
 ******************************************************************************
 * @file    cc.h
 * @brief   Платформенная часть lwIP для usb_eth (GCC, bare-metal).
 * @author  Mechanic
 * @date    27.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef USB_ETH_ARCH_CC_H
#define USB_ETH_ARCH_CC_H

#include <stdint.h>

typedef int sys_prot_t;

#if defined(__GNUC__)
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
#endif

/* Случайные числа (DHCP xid, порты): зерно - уникальный ID чипа */
uint32_t usb_eth_rand(void);
#define LWIP_RAND() usb_eth_rand()

/* Диагностика lwIP не выводится (LWIP_DEBUG выключен) */
#define LWIP_PLATFORM_DIAG(x) do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { } while (0)

#endif /* USB_ETH_ARCH_CC_H */
