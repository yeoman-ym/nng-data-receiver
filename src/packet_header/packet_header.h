#ifndef PACKET_HEADER_H
#define PACKET_HEADER_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h> // htons, htonl, ntohs, ntohl
#include <endian.h>    // htobe64, be64toh

#define PACKET_HEADER_SIZE 24

typedef enum status_code
{
    STATUS_SUCCESS = 1,
    STATUS_ERROR   = 255,
} eStatusCode;


typedef enum msg_type
{
    RECORD_FUNCTION         = 1,
    EXTERND_RECORD_FUNCTION = 2,
    VAR_MON_FUNCTION        = 3,
    VAR_READ_FUNCTION       = 4,
    TASK_RUNNING_EVENT      = 5,
    TASK_LOG                = 6,
    STEP_DEBUG_FUNCTION     = 7,
} eMsgType;


typedef enum
{
    DEBUG_STATUS_OFF     = 0,
    DEBUG_STATUS_WAITING = 1,
    DEBUG_STATUS_SUSPEND = 2,
    DEBUG_STATUS_RUNNING = 3,
    DEBUG_STATUS_EXIT    = 4,
} step_status_code_t;

typedef enum funtion_event_id
{
    FUNCTION_META = 1,
    FUNCTION_DATA,
    FUNCTION_FINISH,
} eFunctionEventId;

typedef enum task_status_type
{
    SIMULATION_STARTED          = 1,     // 仿真任务启动完成
    INITIALIZING_CORE_RESOURCES = 2,     // 初始化核心资源
    LOADING_SO                  = 3,     // 加载so
    LOADING_MODEL               = 4,     // 加载模型
    EXECUTING_FB_INIT           = 5,     // 执行FB初始化函数
    WARMUP_STAGE                = 6,     // 仿真任务处于预热阶段
    RUNNING                     = 9,     // 仿真任务处于running状态中
    RESOURCE_COLLECTION         = 10,    // 任务结束资源收集状态
    SIMULATION_COMPLETED        = 11,    // 仿真任务正常结束
    SIMULATION_FAILED           = 255,   // 仿真任务异常结束
} eTaskStatusType;


typedef struct {
    uint8_t  msg_type;
    uint8_t  version;
    uint8_t  event_id;
    uint8_t  status_code;
    uint16_t msg_len;
    uint16_t node_id;
    uint64_t task_id;
    uint64_t cmd_id;
}nanomsg_header_t;

// 变量监控特有消息头，24字节
typedef struct {
    uint64_t unit_num;        // 监控单元的数量
    uint64_t record_start_id; // 记录的起始ID
    uint64_t record_end_id;   // 记录的结束ID
}varmon_header_t;

#define VARMON_HEADER_SIZE 24


#endif