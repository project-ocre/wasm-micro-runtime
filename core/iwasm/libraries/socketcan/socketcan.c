/*
 * Copyright (C) Atym, Inc.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#ifdef BH_PLATFORM_LINUX
#include <linux/can.h>
#include <linux/can/raw.h>
#endif

#ifdef BH_PLATFORM_ZEPHYR
#include <zephyr/drivers/can.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socketcan.h>
#include <zephyr/net/socketcan_utils.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_socket_can_sample, LOG_LEVEL_DBG);
#endif

#include "bh_common.h"
#include "bh_log.h"
#include "wasm_export.h"
#include "wasm_runtime_common.h"
#include "rights.h"
#include "../interpreter/wasm.h"
#include "../common/wasm_runtime_common.h"

#define CAN_BITRATE 125000

// Filter: "Allow All"
// Match logic: <received_can_id> & mask == can_id & mask
static struct socketcan_filter sock_filter = {
    .can_id = 0x0,
    .can_mask = 0x0,
    .flags = 0U
};

/* socketcan_open */
uint32
socketcan_start(wasm_exec_env_t exec_env, const char *ifname)
{
    /* Stub implementation - always return error for now */
    LOG_WARNING("socketcan_start called with interface: %s",
                ifname ? ifname : "NULL");

#if defined(BH_PLATFORM_LINUX)
// Nothing for now
#elif defined(BH_PLATFORM_ZEPHYR)
    const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
    int ret;

    struct can_timing timing;
    ret = can_calc_timing(dev, &timing, CAN_BITRATE, 875);
    if (ret > 0) {
        LOG_INF("Sample-Point error: %d", ret);
    }

    if (ret < 0) {
        LOG_ERR("Failed to calc a valid timing");
        return -1;
    }

    ret = can_set_timing(dev, &timing);
    if (ret != 0) {
        LOG_ERR("Failed to set timing");
    }

    ret = can_start(dev);
    if (ret != 0) {
        LOG_ERROR("Cannot start CAN controller (%d)", ret);
        return -1;
    }

    /* Let the device start before doing anything */
    k_sleep(K_SECONDS(2));
#endif

    return 0;
}

// Inserts a numerical file descriptor into the file descriptor table.
__wasi_errno_t
fd_table_insert_fd(wasm_exec_env_t exec_env, struct fd_table *ft,
                   os_file_handle in, __wasi_filetype_t type,
                   __wasi_rights_t rights_base,
                   __wasi_rights_t rights_inheriting, __wasi_fd_t *out);

int32
get_sockfds(wasm_exec_env_t exec_env, int sock)
{
    int32_t sockfd;
    __wasi_filetype_t wasi_type = __WASI_FILETYPE_CHARACTER_DEVICE;
    __wasi_rights_t max_base = RIGHTS_ALL, max_inheriting = RIGHTS_ALL;
    __wasi_errno_t error;

    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    struct WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    struct fd_table *curfds = wasi_ctx->curfds;

#if defined(BH_PLATFORM_ZEPHYR)
    os_file_handle in = (os_file_handle)malloc(sizeof(*in));
    if (!in) {
        LOG_ERR("malloc failed for os_file_handle");
        return -1;
    }
    in->fd = sock;
    in->is_sock = true;
#elif defined(BH_PLATFORM_LINUX)
    os_file_handle in = (os_file_handle)sock;
#endif

    // TODO: base rights and inheriting rights ?
    error = fd_table_insert_fd(exec_env, curfds, in, wasi_type, max_base,
                               max_inheriting, &sockfd);
    if (error != __WASI_ESUCCESS) {
        LOG_ERR("socketcan_socket: fd_table_insert_fd failed: %d", error);
        return error;
    }

    return sockfd;
}

/* socketcan_open */
uint32
socketcan_open(wasm_exec_env_t exec_env, const char *ifname)
{
    /* Stub implementation - always return error for now */
    LOG_WARNING("socketcan_bind called with interface: %s",
                ifname ? ifname : "NULL");

    /* Create socket */
    int sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        LOG_ERROR("socketcan_socket: failed to create socket");
        return -1;
    }

    struct sockaddr_can can_addr;

#if defined(BH_PLATFORM_LINUX)
    /* Specify CAN interface */
    struct ifreq ifr;
    strcpy(ifr.ifr_name, ifname);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        LOG_ERROR("socketcan_socket: failed to get interface index");
        close(sock);
        return -1;
    }
    memset(&can_addr, 0, sizeof(can_addr));
    can_addr.can_ifindex = ifr.ifr_ifindex;
    can_addr.can_family = AF_CAN;

#elif defined(BH_PLATFORM_ZEPHYR)
    (void)ifname;
    struct net_if *iface;

    iface = net_if_get_first_by_type(&NET_L2_GET_NAME(CANBUS_RAW));
    if (!iface) {
        LOG_ERR("No CANBUS network interface found!");
        close(sock);
        return -ENOENT;
    }

    /* Ensure interface is up */
    if (!net_if_is_up(iface)) {
        LOG_INF("Bringing CAN iface up");
        int up_ret = net_if_up(iface);
        if (up_ret < 0) {
            LOG_ERR("Failed to bring CAN iface up (%d)", up_ret);
            close(sock);
            return -EIO;
        }
        /* small delay to let interface initialize */
        k_sleep(K_MSEC(50));
    }

    memset(&can_addr, 0, sizeof(can_addr));
    can_addr.can_family = AF_CAN;
    can_addr.can_ifindex = net_if_get_by_iface(iface);
#endif

    LOG_DBG("Binding socket to can_ifindex=%d", can_addr.can_ifindex);

    /* Bind socket to CAN interface */
    if (bind(sock, (struct sockaddr *)&can_addr, sizeof(can_addr)) < 0) {
        LOG_ERR("socketcan_socket: failed to bind socket");
        close(sock);
        return -1;
    }

// Set up CAN filter
// #if defined(BH_PLATFORM_ZEPHYR)
    // socketcan_from_can_filter(&zfilter, &sock_filter);
    if(setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER, &sock_filter,
                     sizeof(sock_filter))) {
        LOG_ERR("socketcan_socket: failed to set CAN filter");
        close(sock);
        return -1;
    }
// #endif

    int sockfd = get_sockfds(exec_env, sock);
    LOG_DBG("socketcan_socket: assigned sockfd=%d", sockfd);
    if (sockfd < 0) {
        return -1;
    }

    return sockfd;
}

uint32
socketcan_start_wrapper(wasm_exec_env_t exec_env, const char *ifname)
{
    if (!ifname || *ifname == '\0') {
        LOG_ERROR("Invalid interface name");
        return -1;
    }

    return socketcan_start(exec_env, ifname);
}

uint32
socketcan_open_wrapper(wasm_exec_env_t exec_env, const char *ifname)
{
    if (!ifname || *ifname == '\0') {
        LOG_ERROR("Invalid interface name");
        return -1;
    }

    return socketcan_open(exec_env, ifname);
}

/* clang-format off */
#define REG_NATIVE_FUNC(func_name, signature) \
    { #func_name, func_name##_wrapper, signature, NULL }
/* clang-format on */

static NativeSymbol native_symbols_socketcan[] = {
    REG_NATIVE_FUNC(socketcan_start, "($)i"),
    REG_NATIVE_FUNC(socketcan_open, "($)i"),
};

uint32
get_lib_socketcan_export_apis(NativeSymbol **p_socketcan_apis)
{
    *p_socketcan_apis = native_symbols_socketcan;
    return sizeof(native_symbols_socketcan) / sizeof(NativeSymbol);
}
