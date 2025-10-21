// #include "comm_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <execinfo.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <nanomsg/nn.h>
#include <nanomsg/pipeline.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include "packet_header/packet_header.h"

#define MAX_BUFFER_SIZE 4096

int sock;

void ctrlc_handle(int sig)
{
    size_t size, i;

    switch (sig) {
        case SIGSEGV:
        {
            void* array[10];
            char  err_msg[MAX_BUFFER_SIZE];

            // 获取堆栈帧地址
            size = backtrace(array, 10);
            snprintf(err_msg, MAX_BUFFER_SIZE, "Error code %d:\n", sig);
            char** symbols = backtrace_symbols(array, size);
            if (symbols == NULL) {
                snprintf(err_msg + strlen(err_msg), MAX_BUFFER_SIZE - strlen(err_msg) - 1, "Unable to get stack symbols\n");
                write(STDERR_FILENO, err_msg, strlen(err_msg));
                _exit(1);
            }

            for (i = 0; i < size; i++) {
                    strncat(err_msg, symbols[i], MAX_BUFFER_SIZE - strlen(err_msg) - 1);
                    strncat(err_msg, "\n", MAX_BUFFER_SIZE - strlen(err_msg) - 1);
                }
            write(STDERR_FILENO, err_msg, strlen(err_msg));
            _exit(1); 
        }
        case SIGINT:
        case SIGHUP:
        case SIGQUIT:
        case SIGABRT:
        case SIGFPE:
        case SIGBUS:
        case SIGPWR:
        case SIGTSTP:

        default: 
            if(sock > 0){
                nn_close(sock);
                sock = -1;
            }
        break;
    }
}

void slave_register_stop_signal()
{
    signal(SIGTERM, ctrlc_handle);   // 重启服务
    // signal(SIGINT, ctrlc_handle);    // 中断信号 ctrl + C
    signal(SIGHUP, ctrlc_handle);    // 挂起信号
    signal(SIGQUIT, ctrlc_handle);   // 退出信号 ctrl + '\'
    signal(SIGABRT, ctrlc_handle);   // 异常终止 断言失败
    signal(SIGSEGV, ctrlc_handle);   // 段错误
    signal(SIGFPE, ctrlc_handle);    // 浮点异常
    signal(SIGBUS, ctrlc_handle);    // 总线错误,访问对齐错误的内存
    signal(SIGPWR, ctrlc_handle);    // 电源故障信号
    signal(SIGTSTP, ctrlc_handle);   // 停止信号 ctrl + Z
}

void fatal(const char* func)
{
    fprintf(stderr, "%s: %s\n", func, nn_strerror(nn_errno()));
    exit(1);
}



void  parse_record_finish(const char *buf, size_t buf_size){
    if(buf == NULL){
        printf("buf is null\n");
        return;
    }
    const char *body = buf + PACKET_HEADER_SIZE;  // 跳过消息头
    size_t body_size = buf_size - PACKET_HEADER_SIZE;
    switch ((unsigned char)buf[2]){
        case FUNCTION_FINISH:
            printf("[###RECORD_FINISH###]\n");
            if(body_size > 0){
                printf("Message Body: %.*s\n", (int)body_size, body);
            }
            break;
    
        default:
            printf("[###UNKNOWN type %d###]\n",buf[2]);
            break;
    }
}


void  parse_varmon_data(const char *buf, size_t buf_size){
    if(buf == NULL){
        printf("buf is null\n");
        return;
    }
    
    // 检查最小长度：nanomsg头(24) + varmon头(24) + 步长(8) = 56字节
    if(buf_size < PACKET_HEADER_SIZE + VARMON_HEADER_SIZE + sizeof(uint64_t)){
        printf("VarMon packet too short: %zu bytes\n", buf_size);
        return;
    }
    
    static unsigned long long recv_num = 0;
    
    // 第一部分：nanomsg头 (24字节)
    const nanomsg_header_t *nanomsg_hdr = (const nanomsg_header_t *)buf;
    
    // 第二部分：变量监控特有消息头 (24字节)
    const varmon_header_t *varmon_hdr = (const varmon_header_t *)(buf + PACKET_HEADER_SIZE);
    
    // 第三部分：当前变量的步长 (8字节)
    const uint64_t *step_ptr = (const uint64_t *)(buf + PACKET_HEADER_SIZE + VARMON_HEADER_SIZE);
    uint64_t current_step = *step_ptr;
    
    // 第四部分：多个数据
    const char *data_ptr = buf + PACKET_HEADER_SIZE + VARMON_HEADER_SIZE + sizeof(uint64_t);
    size_t data_size = buf_size - PACKET_HEADER_SIZE - VARMON_HEADER_SIZE - sizeof(uint64_t);
    
    switch (nanomsg_hdr->event_id)
    {
        case FUNCTION_META:
            printf("VarMon META:\n");
            printf("  unit_num: %lu\n", varmon_hdr->unit_num);
            printf("  record_start_id: %lu\n", varmon_hdr->record_start_id);
            printf("  record_end_id: %lu\n", varmon_hdr->record_end_id);
            printf("  step: %lu\n", current_step);
            if(data_size > 0){
                printf("  meta_data: %.*s\n", (int)data_size, data_ptr);
            }
            break;
            
        case FUNCTION_DATA:
            recv_num++;
            printf("VarMon DATA:\n");
            printf("  unit_num: %lu\n", varmon_hdr->unit_num);
            printf("  record_start_id: %lu\n", varmon_hdr->record_start_id);
            printf("  record_end_id: %lu\n", varmon_hdr->record_end_id);
            printf("  step: %lu\n", current_step);
            printf("  recv_num: %llu\n", recv_num);
            
            // 根据监控单元数量解析多个数据
            if(varmon_hdr->unit_num > 0 && data_size >= sizeof(double) * varmon_hdr->unit_num){
                printf("  data values (%lu units): ", varmon_hdr->unit_num);
                for(uint64_t i = 0; i < varmon_hdr->unit_num; i++){
                    double value;
                    memcpy(&value, data_ptr + i * sizeof(double), sizeof(double));
                    printf("%lf ", value);
                }
                printf("\n");
            }
            break;
            
        case FUNCTION_FINISH:
            printf("VarMon FINISH:\n");
            printf("  unit_num: %lu\n", varmon_hdr->unit_num);
            printf("  record_start_id: %lu\n", varmon_hdr->record_start_id);
            printf("  record_end_id: %lu\n", varmon_hdr->record_end_id);
            printf("  step: %lu\n", current_step);
            if(data_size > 0){
                printf("  finish_data: %.*s\n", (int)data_size, data_ptr);
            }
            break;
            
        default:
            printf("[###UNKNOWN var mon type %d###]\n", nanomsg_hdr->event_id);
            break;
    }
}

void  parse_read_data(const char *buf, size_t buf_size){
    if(buf == NULL){
        printf("buf is null\n");
        return;
    }
    double data_value_double;
    uint64_t data_value_uint64;
    const char *body = buf + PACKET_HEADER_SIZE+8;  // 跳过消息头和步数
    const char *head = buf + PACKET_HEADER_SIZE;  // 跳过消息头
    size_t body_size = buf_size - PACKET_HEADER_SIZE;
    switch (buf[2])
    {
        case FUNCTION_META:
            printf("VarRead META: %.*s\n", (int)body_size, head);
            break;
        case FUNCTION_DATA:
            memcpy(&data_value_uint64, body, sizeof(uint64_t));
            memcpy(&data_value_double, body + sizeof(uint64_t), sizeof(double));
            printf("VarRead DATA: uint64_t:%lu, double:%lf\n", data_value_uint64, data_value_double);
            break;
        case FUNCTION_FINISH:
            printf("VarRead FINISH: %.*s\n", (int)body_size, head);
            break;
        default:
            printf("[###UNKNOWN var mon type %d###]\n",buf[2]);
            break;
    }
}

void  parse_step_debug_data(const char *buf, size_t buf_size){
    if(buf == NULL){
        printf("buf is null\n");
        return;
    }
    const char *head = buf + PACKET_HEADER_SIZE;  // 跳过消息头
    size_t body_size = buf_size - PACKET_HEADER_SIZE;
    switch (buf[2])
    {
        case DEBUG_STATUS_OFF:
            printf("DEBUG_STATUS_OFF\n");
            break;
        case DEBUG_STATUS_WAITING:
            printf("DEBUG_STATUS_WAITING\n");
            break;
        case DEBUG_STATUS_SUSPEND:
            printf("DEBUG_STATUS_SUSPEND\n");
            printf("Step debug message:\n %.*s\n", (int)body_size, head);
            break;
        case DEBUG_STATUS_RUNNING:
            printf("DEBUG_STATUS_RUNNING\n");
            break;
        case DEBUG_STATUS_EXIT:
            printf("DEBUG_STATUS_EXIT\n");
            break;
        default:
            printf("[###UNKNOWN var mon type %d###]\n",buf[2]);
            break;
    }
}

void print_message_body(const char *buf, size_t buf_size) {
    const char *body = buf + PACKET_HEADER_SIZE;  // 跳过消息头
    size_t body_size = buf_size - PACKET_HEADER_SIZE;

    printf("Log message:\n %.*s\n", (int)body_size, body);
}

void  parse_task_status(char* buf){
    if(buf == NULL){
        printf("buf is null\n");
        return;
    }
    switch ((unsigned char)buf[2])
    {
        case SIMULATION_STARTED:
            printf("[###SIMULATION_STARTED###]\n");
            break;
        case INITIALIZING_CORE_RESOURCES:
            printf("[###INITIALIZING_CORE_RESOURCES###]\n");
            break;
        case LOADING_SO:
            printf("[###LOADING_SO###]\n");
            break;
        case LOADING_MODEL:
            printf("[###LOADING_MODEL###]\n");
            break;
        case EXECUTING_FB_INIT:
            printf("[###EXECUTING_FB_INIT###]\n");
            break;
        case WARMUP_STAGE:
            printf("[###WARMUP_STAGE###]\n");
            break;
        case RUNNING:
            printf("[###RUNNING###]\n");
            break;
        case RESOURCE_COLLECTION:
            printf("[###RESOURCE_COLLECTION###]\n");
            break;
        case SIMULATION_COMPLETED:
            printf("[###SIMULATION_COMPLETED###]\n");
            break;
        case SIMULATION_FAILED:
            printf("[###SIMULATION_FAILED###]\n");
            break;
        default:
            printf("[###UNKNOWN type %d###]\n",buf[2]);
            break;
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: %s tcp://0.0.0.0:11112\n", argv[0]);
        return 1;
    }
    
    printf("The input string is: %s\n", argv[1]);
    sock = -1;

    slave_register_stop_signal();

    int rv;

    if ((sock = nn_socket(AF_SP, NN_PULL)) < 0) {
        fatal("nn_socket");
    }

    if ((rv = nn_bind(sock, argv[1])) < 0) {
        fatal("nn_bind");
    }
    printf("nanomsg server start at:%s\n", argv[1]);
    
    for (;;) {
        char* buf = NULL;
        int   bytes;

        do {
            bytes = nn_recv(sock, &buf, NN_MSG, 0);
        } while (bytes < 0 && errno == EINTR);  // 继续尝试

        if (bytes < 0) {
            fatal("nn_recv");
        }

        if (bytes < PACKET_HEADER_SIZE) {
            printf("Received packet is too short: %d bytes\n", bytes);
            nn_freemsg(buf);
        }
        
        switch (buf[0])
        {
            case RECORD_FUNCTION:
                parse_record_finish(buf, bytes);
                break;
            case VAR_MON_FUNCTION:
                parse_varmon_data(buf, bytes);
                break;
            case VAR_READ_FUNCTION:
                parse_read_data(buf, bytes);
                break;
            case TASK_RUNNING_EVENT:
                parse_task_status(buf);
                break;
            case TASK_LOG:
                print_message_body(buf, bytes);
                break;
            case STEP_DEBUG_FUNCTION:
                parse_step_debug_data(buf, bytes);
                break;
            default:
                printf("[###UNKNOWN type %d###]\n",buf[0]);
                break;
        }
        nn_freemsg(buf);
    }
    
    return 0;
}
