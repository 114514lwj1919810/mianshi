/* AUTO-FILE-DOC
 * 文件职责：app_protocol 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
/**
 * @file app_protocol.h
 * @brief GD32 从机与 F103 手持端共用的应用命令、响应和分包定义。
 *
 * 本头文件定义的是 nRF24L01 DATA 包内部的业务帧类型；CRC、seq 和 payload 编解码在 main.c 中完成。
 * 与 F103 主机工程必须保持命令号、状态码、参数数量和分包数量一致。
 */

#ifndef __APP_PROTOCOL_H
#define __APP_PROTOCOL_H

#include <stdint.h>

#define APP_PARAM_COUNT             12U
#define APP_PARAM_VALUES_PER_ITEM   2U
#define APP_PARAM_VALUE_LEN         2U
#define APP_PARAM_ITEM_LEN          (APP_PARAM_VALUES_PER_ITEM * APP_PARAM_VALUE_LEN)
#define APP_PARAMS_PER_PACKET       4U
#define APP_PARAM_PART_COUNT        3U
#define APP_PARAM_PACKET_LEN        (4U + APP_PARAMS_PER_PACKET * APP_PARAM_ITEM_LEN)
#define APP_PARAM_VALUE_UNUSED      0xFFFFU
#define APP_SET_PARAM_PAYLOAD_LEN   6U

#define APP_ACCOUNT_COUNT           14U
#define APP_ACCOUNT_PER_PACKET      4U
#define APP_ACCOUNT_PART_COUNT      4U

#define APP_FAULT_NONE              0U
#define APP_FAULT_UNKNOWN           0xFFU
#define APP_TIME_PAYLOAD_LEN        8U

#define APP_FRAME_SOF               0xA5U
#define APP_FRAME_OVERHEAD          6U
#define APP_FRAME_MAX_PAYLOAD       20U
#define APP_FRAME_MAX_LEN           26U

#define APP_CMD_GET_PARAMS          0x11U
#define APP_CMD_SET_PARAM           0x12U
#define APP_CMD_CLEAR_FAULT         0x13U
#define APP_CMD_GET_ACCOUNT         0x14U
#define APP_CMD_HEARTBEAT           0x15U
#define APP_CMD_GET_FAULT           0x16U
#define APP_CMD_SET_TIME            0x17U
#define APP_CMD_EXIT                0x18U /* Payload: [18]; idempotent release. */

#define APP_RSP_PARAMS              0x91U
#define APP_RSP_SET_PARAM           0x92U
#define APP_RSP_CLEAR_FAULT         0x93U
#define APP_RSP_ACCOUNT             0x94U
#define APP_RSP_FAULT               0x96U
#define APP_RSP_SET_TIME            0x97U
#define APP_RSP_EXIT                0x98U /* Payload: [98, status], same seq. */

#define APP_STATUS_OK               0x00U
#define APP_STATUS_BAD_PARAM        0x01U
#define APP_STATUS_BAD_VALUE        0x02U
#define APP_STATUS_FAILED           0x03U

#endif
