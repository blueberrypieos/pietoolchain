/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_HOST_MACH_H
#define PIE_HOST_MACH_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#ifndef __printflike
#define __printflike(a, b) __attribute__((__format__(__printf__, a, b)))
#endif

typedef int kern_return_t;
typedef kern_return_t mach_msg_return_t;
typedef unsigned int natural_t;
typedef int integer_t;
typedef int boolean_t;
typedef natural_t mach_port_name_t;
typedef mach_port_name_t mach_port_t;
typedef natural_t mach_port_right_t;
typedef integer_t mach_port_delta_t;
typedef natural_t mach_port_type_t;
typedef unsigned int mach_msg_bits_t;
typedef natural_t mach_msg_size_t;
typedef integer_t mach_msg_id_t;
typedef unsigned int mach_msg_type_name_t;
typedef unsigned int mach_msg_descriptor_type_t;
typedef unsigned int mach_msg_copy_options_t;
typedef integer_t mach_msg_option_t;
typedef natural_t mach_msg_timeout_t;
typedef unsigned int mach_msg_trailer_type_t;
typedef unsigned int mach_msg_trailer_size_t;
typedef natural_t mach_port_seqno_t;
typedef uintptr_t vm_address_t;
typedef uintptr_t vm_size_t;
typedef int au_asid_t;

typedef struct {
	unsigned int val[8];
} audit_token_t;

typedef struct {
	unsigned int val[2];
} security_token_t;

typedef struct {
	mach_msg_bits_t msgh_bits;
	mach_msg_size_t msgh_size;
	mach_port_t msgh_remote_port;
	mach_port_t msgh_local_port;
	mach_port_name_t msgh_voucher_port;
	mach_msg_id_t msgh_id;
} mach_msg_header_t;

typedef struct {
	mach_msg_size_t msgh_descriptor_count;
} mach_msg_body_t;

typedef struct {
	natural_t pad1;
	mach_msg_size_t pad2;
	unsigned int pad3 : 24;
	mach_msg_descriptor_type_t type : 8;
} mach_msg_type_descriptor_t;

typedef struct {
	mach_port_t name;
	mach_msg_size_t pad1;
	unsigned int pad2 : 16;
	mach_msg_type_name_t disposition : 8;
	mach_msg_descriptor_type_t type : 8;
} mach_msg_port_descriptor_t;

#pragma pack(push, 4)
typedef struct {
	void *address;
	boolean_t deallocate : 8;
	mach_msg_copy_options_t copy : 8;
	unsigned int pad1 : 8;
	mach_msg_descriptor_type_t type : 8;
	mach_msg_size_t size;
} mach_msg_ool_descriptor_t;
#pragma pack(pop)

typedef struct {
	mach_msg_trailer_type_t msgh_trailer_type;
	mach_msg_trailer_size_t msgh_trailer_size;
	mach_port_seqno_t msgh_seqno;
	security_token_t msgh_sender;
	audit_token_t msgh_audit;
} mach_msg_audit_trailer_t;

typedef struct {
	mach_msg_audit_trailer_t audit;
	uint64_t context;
	uint32_t ad;
	uint32_t labels;
} mach_msg_max_trailer_t;

#define KERN_SUCCESS            0
#define KERN_INVALID_ARGUMENT   4
#define KERN_FAILURE            5
#define KERN_RESOURCE_SHORTAGE  6
#define KERN_INVALID_NAME       15
#define KERN_INVALID_RIGHT      17

#define MACH_PORT_NULL          0
#define MACH_PORT_DEAD          ((mach_port_t)~0)
#define MACH_PORT_VALID(name)   ((name) != MACH_PORT_NULL && (name) != MACH_PORT_DEAD)
#define MACH_PORT_INDEX(name)   ((name) >> 8)

#define MACH_PORT_RIGHT_SEND    ((mach_port_right_t)0)
#define MACH_PORT_RIGHT_RECEIVE ((mach_port_right_t)1)

#define MACH_MSG_TYPE_MOVE_RECEIVE   16
#define MACH_MSG_TYPE_MOVE_SEND      17
#define MACH_MSG_TYPE_MOVE_SEND_ONCE 18
#define MACH_MSG_TYPE_COPY_SEND      19
#define MACH_MSG_TYPE_MAKE_SEND      20
#define MACH_MSG_TYPE_MAKE_SEND_ONCE 21
#define MACH_MSG_TYPE_PORT_RECEIVE   MACH_MSG_TYPE_MOVE_RECEIVE
#define MACH_MSG_TYPE_PORT_SEND      MACH_MSG_TYPE_MOVE_SEND
#define MACH_MSG_TYPE_PORT_SEND_ONCE MACH_MSG_TYPE_MOVE_SEND_ONCE

#define MACH_MSGH_BITS_COMPLEX       0x80000000U
#define MACH_MSGH_BITS_REMOTE(bits)  ((bits) & 0x1f)
#define MACH_MSGH_BITS_LOCAL(bits)   (((bits) >> 8) & 0x1f)
#define MACH_MSGH_BITS_SET(remote, local, voucher, other) \
	((remote) | ((local) << 8) | ((voucher) << 16) | (other))

#define MACH_MSG_PORT_DESCRIPTOR 0
#define MACH_MSG_OOL_DESCRIPTOR  1
#define MACH_MSG_VIRTUAL_COPY    1

#define MACH_MSG_TIMEOUT_NONE    0
#define MACH_SEND_MSG            0x00000001
#define MACH_RCV_MSG             0x00000002
#define MACH_RCV_LARGE           0x00000004
#define MACH_SEND_TIMEOUT        0x00000010
#define MACH_RCV_TIMEOUT         0x00000100
#define MACH_MSG_TRAILER_FORMAT_0 0
#define MACH_RCV_TRAILER_AUDIT   3
#define MACH_RCV_TRAILER_TYPE(x)     (((x) & 0xf) << 28)
#define MACH_RCV_TRAILER_ELEMENTS(x) (((x) & 0xf) << 24)

#define MACH_MSG_MASK              0x00003e00
#define MACH_MSG_IPC_SPACE         0x00002000
#define MACH_MSG_VM_SPACE          0x00001000
#define MACH_SEND_INVALID_DATA     0x10000002
#define MACH_SEND_INVALID_DEST     0x10000003
#define MACH_SEND_TIMED_OUT        0x10000004
#define MACH_SEND_INVALID_VOUCHER  0x10000005
#define MACH_SEND_MSG_TOO_SMALL    0x10000008
#define MACH_SEND_INVALID_REPLY    0x10000009
#define MACH_SEND_INVALID_OPTIONS  0x10000013
#define MACH_SEND_MSG_FILTERED     0x10000017
#define MACH_SEND_INVALID_RIGHT    0x1000000a
#define MACH_SEND_INVALID_MEMORY   0x1000000c
#define MACH_SEND_NO_BUFFER        0x1000000d
#define MACH_SEND_TOO_LARGE        0x1000000e
#define MACH_SEND_INVALID_TYPE     0x1000000f
#define MACH_SEND_INVALID_HEADER   0x10000010
#define MACH_RCV_INVALID_NAME      0x10004002
#define MACH_RCV_TIMED_OUT         0x10004003
#define MACH_RCV_TOO_LARGE         0x10004004
#define MACH_RCV_BODY_ERROR        0x1000400c

#define round_msg(x) (((mach_msg_size_t)(x) + sizeof(natural_t) - 1) & ~(sizeof(natural_t) - 1))

mach_port_t mach_task_self(void);
kern_return_t mach_port_deallocate(mach_port_t task, mach_port_name_t name);
kern_return_t mach_port_mod_refs(mach_port_t task, mach_port_name_t name, mach_port_right_t right,
    mach_port_delta_t delta);
kern_return_t vm_deallocate(mach_port_t task, vm_address_t address, vm_size_t size);
kern_return_t mach_port_extract_right(mach_port_t task, mach_port_name_t name,
    mach_msg_type_name_t desired, mach_port_t *right, mach_msg_type_name_t *acquired);
mach_msg_return_t mach_msg(mach_msg_header_t *msg, mach_msg_option_t option,
    mach_msg_size_t send_size, mach_msg_size_t rcv_size, mach_port_name_t rcv_name,
    mach_msg_timeout_t timeout, mach_port_name_t notify);
void mach_msg_destroy(mach_msg_header_t *msg);
int heapsort(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *));

typedef natural_t mach_msg_type_number_t;
typedef natural_t mach_port_mscount_t;
typedef natural_t mach_port_msgcount_t;
typedef int mach_port_flavor_t;
typedef integer_t *mach_port_info_t;

typedef struct {
	mach_port_msgcount_t mpl_qlimit;
} mach_port_limits_t;

typedef struct {
	natural_t mps_pset;
	mach_port_seqno_t mps_seqno;
	mach_port_mscount_t mps_mscount;
	mach_port_msgcount_t mps_qlimit;
	mach_port_msgcount_t mps_msgcount;
	natural_t mps_sorights;
	boolean_t mps_srights;
	boolean_t mps_pdrequest;
	boolean_t mps_nsrequest;
	natural_t mps_flags;
} mach_port_status_t;

typedef struct {
	uint32_t flags;
	mach_port_limits_t mpl;
	uint64_t reserved[2];
} mach_port_options_t;

#define MACH_PORT_LIMITS_INFO          1
#define MACH_PORT_RECEIVE_STATUS       2
#define MACH_PORT_LIMITS_INFO_COUNT \
	((mach_msg_type_number_t)(sizeof(mach_port_limits_t) / sizeof(natural_t)))
#define MACH_PORT_RECEIVE_STATUS_COUNT \
	((mach_msg_type_number_t)(sizeof(mach_port_status_t) / sizeof(natural_t)))
#define MACH_PORT_QLIMIT_ZERO          0
#define MACH_PORT_QLIMIT_LARGE         1024
#define MPO_QLIMIT                     0x02
#define MPO_INSERT_SEND_RIGHT          0x10

kern_return_t mach_port_insert_right(mach_port_t task, mach_port_name_t name, mach_port_t poly,
    mach_msg_type_name_t poly_poly);
kern_return_t mach_port_construct(mach_port_t task, mach_port_options_t *options,
    uint64_t context, mach_port_name_t *name);
kern_return_t mach_port_request_notification(mach_port_t task, mach_port_name_t name,
    mach_msg_id_t msgid, mach_port_mscount_t sync, mach_port_t notify,
    mach_msg_type_name_t notify_poly, mach_port_t *previous);
kern_return_t mach_port_set_attributes(mach_port_t task, mach_port_name_t name,
    mach_port_flavor_t flavor, mach_port_info_t info, mach_msg_type_number_t count);
kern_return_t mach_port_get_attributes(mach_port_t task, mach_port_name_t name,
    mach_port_flavor_t flavor, mach_port_info_t info, mach_msg_type_number_t *count);
mach_port_t mig_get_reply_port(void);
void mig_dealloc_reply_port(mach_port_t port);

#endif
