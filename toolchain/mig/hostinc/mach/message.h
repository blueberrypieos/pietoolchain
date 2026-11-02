/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 *
 * Minimal host-side stand-in for <mach/message.h>, just enough to build
 * migcom on a non-Darwin host. Values and layouts mirror
 * xnu/osfmk/mach/message.h for a 64-bit (LP64) user-space target.
 */
#ifndef _PIE_HOST_MACH_MESSAGE_H_
#define _PIE_HOST_MACH_MESSAGE_H_

#include <stdint.h>
#include <mach/port.h>
#include <mach/kern_return.h>

typedef natural_t mach_msg_timeout_t;
#define MACH_MSG_TIMEOUT_NONE           ((mach_msg_timeout_t) 0)

#define MACH_MSGH_BITS_ZERO             0x00000000
#define MACH_MSGH_BITS_REMOTE_MASK      0x0000001f
#define MACH_MSGH_BITS_LOCAL_MASK       0x00001f00
#define MACH_MSGH_BITS_VOUCHER_MASK     0x001f0000
#define MACH_MSGH_BITS_PORTS_MASK \
	(MACH_MSGH_BITS_REMOTE_MASK | MACH_MSGH_BITS_LOCAL_MASK | MACH_MSGH_BITS_VOUCHER_MASK)
#define MACH_MSGH_BITS_COMPLEX          0x80000000U

#define MACH_MSGH_BITS(remote, local)   ((remote) | ((local) << 8))
#define MACH_MSGH_BITS_REMOTE(bits)     ((bits) & MACH_MSGH_BITS_REMOTE_MASK)
#define MACH_MSGH_BITS_LOCAL(bits)      (((bits) & MACH_MSGH_BITS_LOCAL_MASK) >> 8)
#define MACH_MSGH_BITS_VOUCHER(bits)    (((bits) & MACH_MSGH_BITS_VOUCHER_MASK) >> 16)

typedef unsigned int mach_msg_bits_t;
typedef natural_t mach_msg_size_t;
typedef integer_t mach_msg_id_t;

typedef struct {
	mach_msg_bits_t   msgh_bits;
	mach_msg_size_t   msgh_size;
	mach_port_t       msgh_remote_port;
	mach_port_t       msgh_local_port;
	mach_port_name_t  msgh_voucher_port;
	mach_msg_id_t     msgh_id;
} mach_msg_header_t;

typedef struct {
	mach_msg_size_t   msgh_descriptor_count;
} mach_msg_body_t;

typedef unsigned int mach_msg_type_name_t;
#define MACH_MSG_TYPE_MOVE_RECEIVE      16
#define MACH_MSG_TYPE_MOVE_SEND         17
#define MACH_MSG_TYPE_MOVE_SEND_ONCE    18
#define MACH_MSG_TYPE_COPY_SEND         19
#define MACH_MSG_TYPE_MAKE_SEND         20
#define MACH_MSG_TYPE_MAKE_SEND_ONCE    21
#define MACH_MSG_TYPE_COPY_RECEIVE      22
#define MACH_MSG_TYPE_DISPOSE_RECEIVE   24
#define MACH_MSG_TYPE_DISPOSE_SEND      25
#define MACH_MSG_TYPE_DISPOSE_SEND_ONCE 26

typedef unsigned int mach_msg_copy_options_t;
#define MACH_MSG_PHYSICAL_COPY          0
#define MACH_MSG_VIRTUAL_COPY           1
#define MACH_MSG_ALLOCATE               2

typedef unsigned int mach_msg_descriptor_type_t;
#define MACH_MSG_PORT_DESCRIPTOR                0
#define MACH_MSG_OOL_DESCRIPTOR                 1
#define MACH_MSG_OOL_PORTS_DESCRIPTOR           2
#define MACH_MSG_OOL_VOLATILE_DESCRIPTOR        3
#define MACH_MSG_GUARDED_PORT_DESCRIPTOR        4

#pragma pack(push, 4)
typedef struct {
	natural_t                       pad1;
	mach_msg_size_t                 pad2;
	unsigned int                    pad3 : 24;
	mach_msg_descriptor_type_t      type : 8;
} mach_msg_type_descriptor_t;

typedef struct {
	mach_port_t                     name;
	mach_msg_size_t                 pad1;
	unsigned int                    pad2 : 16;
	mach_msg_type_name_t            disposition : 8;
	mach_msg_descriptor_type_t      type : 8;
} mach_msg_port_descriptor_t;

typedef struct {
	void                           *address;
	boolean_t                       deallocate: 8;
	mach_msg_copy_options_t         copy: 8;
	unsigned int                    pad1: 8;
	mach_msg_descriptor_type_t      type: 8;
	mach_msg_size_t                 size;
} mach_msg_ool_descriptor_t;

typedef struct {
	void                           *address;
	boolean_t                       deallocate: 8;
	mach_msg_copy_options_t         copy: 8;
	mach_msg_type_name_t            disposition : 8;
	mach_msg_descriptor_type_t      type : 8;
	mach_msg_size_t                 count;
} mach_msg_ool_ports_descriptor_t;

typedef union {
	mach_msg_port_descriptor_t      port;
	mach_msg_ool_descriptor_t       out_of_line;
	mach_msg_ool_ports_descriptor_t ool_ports;
	mach_msg_type_descriptor_t      type;
} mach_msg_descriptor_t;
#pragma pack(pop)

typedef unsigned int mach_msg_trailer_type_t;
#define MACH_MSG_TRAILER_FORMAT_0       0
typedef unsigned int mach_msg_trailer_size_t;

typedef struct {
	mach_msg_trailer_type_t         msgh_trailer_type;
	mach_msg_trailer_size_t         msgh_trailer_size;
	unsigned int                    msgh_seqno;
	struct { unsigned int val[2]; } msgh_sender;
	struct { unsigned int val[8]; } msgh_audit;
	uint64_t                        msgh_context;
	int                             msgh_ad;
	struct { unsigned int sender; } msgh_labels;
} mach_msg_max_trailer_t;

typedef natural_t mach_msg_type_size_t;
typedef natural_t mach_msg_type_number_t;

#define MACH_MSG_TYPE_PORT_NONE         0
#define MACH_MSG_TYPE_PORT_NAME         15
#define MACH_MSG_TYPE_PORT_RECEIVE      MACH_MSG_TYPE_MOVE_RECEIVE
#define MACH_MSG_TYPE_PORT_SEND         MACH_MSG_TYPE_MOVE_SEND
#define MACH_MSG_TYPE_PORT_SEND_ONCE    MACH_MSG_TYPE_MOVE_SEND_ONCE
#define MACH_MSG_TYPE_LAST              22
#define MACH_MSG_TYPE_POLYMORPHIC       ((mach_msg_type_name_t) -1)

#define MACH_MSG_TYPE_PORT_ANY(x) \
	(((x) >= MACH_MSG_TYPE_MOVE_RECEIVE) && ((x) <= MACH_MSG_TYPE_MAKE_SEND_ONCE))
#define MACH_MSG_TYPE_PORT_ANY_SEND(x) \
	(((x) >= MACH_MSG_TYPE_MOVE_SEND) && ((x) <= MACH_MSG_TYPE_MAKE_SEND_ONCE))
#define MACH_MSG_TYPE_PORT_ANY_RIGHT(x) \
	(((x) >= MACH_MSG_TYPE_MOVE_RECEIVE) && ((x) <= MACH_MSG_TYPE_MOVE_SEND_ONCE))

typedef integer_t mach_msg_option_t;
#define MACH_MSG_OPTION_NONE    0x00000000
#define MACH_SEND_MSG           0x00000001
#define MACH_RCV_MSG            0x00000002
#define MACH_RCV_LARGE          0x00000004
#define MACH_RCV_LARGE_IDENTITY 0x00000008
#define MACH_SEND_TIMEOUT       0x00000010
#define MACH_SEND_OVERRIDE      0x00000020
#define MACH_SEND_INTERRUPT     0x00000040
#define MACH_SEND_NOTIFY        0x00000080
#define MACH_SEND_FILTER_NONFATAL 0x00010000
#define MACH_SEND_TRAILER       0x00020000
#define MACH_SEND_NOIMPORTANCE  0x00040000
#define MACH_SEND_NODENAP       MACH_SEND_NOIMPORTANCE
#define MACH_SEND_IMPORTANCE    0x00080000
#define MACH_SEND_SYNC_OVERRIDE 0x00100000
#define MACH_SEND_PROPAGATE_QOS 0x00200000
#define MACH_SEND_SYNC_USE_THRPRI MACH_SEND_PROPAGATE_QOS
#define MACH_SEND_KERNEL        0x00400000
#define MACH_SEND_SYNC_BOOTSTRAP_CHECKIN 0x00800000
#define MACH_RCV_TIMEOUT        0x00000100
#define MACH_RCV_NOTIFY         0x00000000
#define MACH_RCV_INTERRUPT      0x00000400
#define MACH_RCV_VOUCHER        0x00000800
#define MACH_RCV_OVERWRITE      0x00000000
#define MACH_RCV_GUARDED_DESC   0x00001000
#define MACH_RCV_SYNC_WAIT      0x00004000
#define MACH_RCV_SYNC_PEEK      0x00008000
#define MACH_MSG_STRICT_REPLY   0x00000200

#define MACH_RCV_TRAILER_NULL   0
#define MACH_RCV_TRAILER_SEQNO  1
#define MACH_RCV_TRAILER_SENDER 2
#define MACH_RCV_TRAILER_AUDIT  3
#define MACH_RCV_TRAILER_CTX    4
#define MACH_RCV_TRAILER_AV     7
#define MACH_RCV_TRAILER_LABELS 8
#define MACH_RCV_TRAILER_TYPE(x)     (((x) & 0xf) << 28)
#define MACH_RCV_TRAILER_ELEMENTS(x) (((x) & 0xf) << 24)
#define MACH_RCV_TRAILER_MASK        ((0xf << 24))

typedef kern_return_t mach_msg_return_t;
#define MACH_MSG_SUCCESS                0x00000000
#define MACH_SEND_INVALID_DEST          0x10000003
#define MACH_SEND_TIMED_OUT             0x10000004
#define MACH_RCV_TIMED_OUT              0x10004003

#endif /* _PIE_HOST_MACH_MESSAGE_H_ */
