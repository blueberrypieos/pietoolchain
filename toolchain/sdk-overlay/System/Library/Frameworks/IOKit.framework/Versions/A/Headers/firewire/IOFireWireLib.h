/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _PIE_IOKIT_FIREWIRE_IOFIREWIRELIB_H_
#define _PIE_IOKIT_FIREWIRE_IOFIREWIRELIB_H_

#include <IOKit/IOReturn.h>

#define iokit_fw_err(return)    (sys_iokit | sub_iokit_firewire | (return))

#define kIOConfigNoEntry                        iokit_fw_err(0x1)
#define kIOFireWireResponseBase                 iokit_fw_err(0x10)
#define kIOFireWireBusReset                     iokit_fw_err(0x20)
#define kIOFireWireBogusDCLProgram              iokit_fw_err(0x101)
#define kIOFireWireCompleting                   iokit_fw_err(0x104)
#define kIOFWMessageServiceIsRequestingClose    iokit_fw_err(0x7D0)
#define kIOFWMessageTopologyChanged             iokit_fw_err(0x7D2)

#endif
