# Copyright (C) 2025 Atym, Inc.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (LIB_SOCKETCAN ${CMAKE_CURRENT_LIST_DIR})
add_definitions (-DWASM_ENABLE_SOCKETCAN=1)
include_directories(${LIB_SOCKETCAN_DIR})
file (GLOB source_all ${LIB_SOCKETCAN}/*.c)
set (LIB_SOCKETCAN_SOURCE ${source_all})