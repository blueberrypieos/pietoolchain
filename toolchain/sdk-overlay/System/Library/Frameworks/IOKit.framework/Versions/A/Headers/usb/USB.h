/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _PIE_IOKIT_USB_H_
#define _PIE_IOKIT_USB_H_

#include <IOKit/IOReturn.h>

#define iokit_usb_err(return)   (sys_iokit | sub_iokit_usb | (return))

#define kIOUSBCRCErr                                    iokit_usb_err(0x01)
#define kIOUSBBitstufErr                                iokit_usb_err(0x02)
#define kIOUSBDataToggleErr                             iokit_usb_err(0x03)
#define kIOUSBPIDCheckErr                               iokit_usb_err(0x06)
#define kIOUSBWrongPIDErr                               iokit_usb_err(0x07)
#define kIOUSBReserved1Err                              iokit_usb_err(0x0a)
#define kIOUSBReserved2Err                              iokit_usb_err(0x0b)
#define kIOUSBBufferOverrunErr                          iokit_usb_err(0x0c)
#define kIOUSBBufferUnderrunErr                         iokit_usb_err(0x0d)
#define kIOUSBNotSent1Err                               iokit_usb_err(0x0e)
#define kIOUSBNotSent2Err                               iokit_usb_err(0x0f)
#define kIOUSBLinkErr                                   iokit_usb_err(0x10)
#define kIOUSBDeviceNotHighSpeed                        iokit_usb_err(0x49)
#define kIOUSBSyncRequestOnWLThread                     iokit_usb_err(0x4a)
#define kIOUSBHighSpeedSplitError                       iokit_usb_err(0x4b)
#define kIOUSBLowLatencyFrameListNotPreviouslyAllocated iokit_usb_err(0x4c)
#define kIOUSBLowLatencyBufferNotPreviouslyAllocated    iokit_usb_err(0x4d)
#define kIOUSBInterfaceNotFound                         iokit_usb_err(0x4e)
#define kIOUSBPipeStalled                               iokit_usb_err(0x4f)
#define kIOUSBTransactionReturned                       iokit_usb_err(0x50)
#define kIOUSBTransactionTimeout                        iokit_usb_err(0x51)
#define kIOUSBConfigNotFound                            iokit_usb_err(0x56)
#define kIOUSBEndpointNotFound                          iokit_usb_err(0x57)
#define kIOUSBNotEnoughPowerErr                         iokit_usb_err(0x5d)
#define kIOUSBNotEnoughPipesErr                         iokit_usb_err(0x5e)
#define kIOUSBNoAsyncPortErr                            iokit_usb_err(0x5f)
#define kIOUSBTooManyPipesErr                           iokit_usb_err(0x60)
#define kIOUSBUnknownPipeErr                            iokit_usb_err(0x61)

#endif
