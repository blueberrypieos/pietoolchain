/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _OPENDIRECTORY_ODIPC_H_
#define _OPENDIRECTORY_ODIPC_H_

#define kODMachLibinfoPortName         "com.apple.system.opendirectoryd.libinfo"
#define kODMachLibinfoPortNameDebug    "com.apple.system.opendirectoryd.libinfo.debug"
#define kODMachMembershipPortName      "com.apple.system.opendirectoryd.membership"
#define kODMachMembershipPortNameDebug "com.apple.system.opendirectoryd.membership.debug"
#define kODAPIPortName                 "com.apple.system.opendirectoryd.api"

#define OD_RPC_NAME    "rpc_name"
#define OD_RPC_VERSION "rpc_version"
#define OD_RPC_RESULT  "result"
#define OD_RPC_ERROR   "error"

#define kODXPCMessageClientID "client_id"
#define kODXPCMessageReqType  "reqtype"
#define kODXPCMessageSession  "session"
#define kODXPCMessageNode     "node"
#define kODXPCMessageRequest  "request"

#define kODXPCReplyClientID   "client_id"
#define kODXPCReplyData       "data"
#define kODXPCReplyError      "error"
#define kODXPCReplyComplete   "complete"

#endif
