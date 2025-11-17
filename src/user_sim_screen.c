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
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include "packet_header/packet_header.h"

#define MAX_BUFFER_SIZE 4096
#define LOG_FILENAME_MAX 256

int sock;
FILE* g_log_file = NULL;  // 全局日志文件句柄

// -------------------- 元数据解析与缓存 --------------------
typedef struct {
    char     name[128];      // 变量名
    char     type[32];       // 变量类型字符串，如 CSG_DOUBLE/CSG_UINT8 等
    size_t   size;           // 变量字节数
    int      is_step;        // 是否为 steps 字段（不在数据区内，单独在头里）
} var_def_t;

typedef struct {
    size_t    unit_size;     // variable_monitor_unit_bytes
    size_t    var_count;     // 变量个数
    var_def_t vars[64];      // 简易上限，足够一般使用
    size_t    interval_steps; // 从元数据解析的间隔步数
} meta_cache_t;

static inline void reset_meta_cache(meta_cache_t* cache) {
    memset(cache, 0, sizeof(*cache));
}

// --- 按 cmd_id 维护多路元数据缓存 ---
// 注：同一任务下 cmd_id 不超过 5 个，此处预留 32 个槽位支持多任务并发
#define MAX_META_ENTRIES 32
typedef struct {
    uint64_t     cmd_id;
    meta_cache_t cache;
    int          in_use;
} meta_entry_t;

static meta_entry_t g_meta_map[MAX_META_ENTRIES];

static inline void init_meta_map(void) {
    memset(g_meta_map, 0, sizeof(g_meta_map));
}

// -------------------- 日志文件管理 --------------------
// 打开日志文件，文件名格式：log_YYYYMMDD_HHMMSS.txt
static int open_log_file(void) {
    if (g_log_file != NULL) {
        return 0;  // 已经打开
    }
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    if (!tm_info) {
        return -1;
    }
    
    char log_filename[LOG_FILENAME_MAX];
    snprintf(log_filename, sizeof(log_filename), 
             "log_%04d%02d%02d_%02d%02d%02d.txt",
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec);
    
    g_log_file = fopen(log_filename, "a");
    if (g_log_file == NULL) {
        return -1;
    }
    
    // 设置文件缓冲为行缓冲，确保及时写入
    setvbuf(g_log_file, NULL, _IOLBF, 0);
    
    return 0;
}

// 关闭日志文件
static void close_log_file(void) {
    if (g_log_file != NULL) {
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

// 同时输出到终端和日志文件的printf函数
static void log_printf(const char* format, ...) {
    va_list args;
    char buffer[MAX_BUFFER_SIZE];
    
    // 格式化字符串
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len < 0) {
        return;  // 格式化失败
    }
    
    // 确保不超过缓冲区大小
    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
        buffer[len] = '\0';
    }
    
    // 输出到终端
    fputs(buffer, stdout);
    fflush(stdout);
    
    // 输出到日志文件
    if (g_log_file != NULL) {
        fputs(buffer, g_log_file);
        fflush(g_log_file);
    }
}

static inline meta_cache_t* meta_get_entry(uint64_t cmd_id, int create_if_missing) {
    // 查找已存在的项
    for (size_t i = 0; i < MAX_META_ENTRIES; i++) {
        if (g_meta_map[i].in_use && g_meta_map[i].cmd_id == cmd_id) {
            return &g_meta_map[i].cache;
        }
    }
    if (!create_if_missing) return NULL;
    // 分配新项
    for (size_t i = 0; i < MAX_META_ENTRIES; i++) {
        if (!g_meta_map[i].in_use) {
            g_meta_map[i].in_use = 1;
            g_meta_map[i].cmd_id = cmd_id;
            reset_meta_cache(&g_meta_map[i].cache);
            return &g_meta_map[i].cache;
        }
    }
    return NULL; // 已满
}

// 在 json 文本中查找 key 对应的无符号整数值（十进制），找不到则返回 default_value
static size_t parse_number_value(const char* json, const char* key, size_t default_value) {
    if (json == NULL || key == NULL) return default_value;
    const char* p = strstr(json, key);
    if (!p) return default_value;
    p += strlen(key);
    // 向后找到冒号
    p = strchr(p, ':');
    if (!p) return default_value;
    p++;
    // 跳过空白
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    // 读取数字
    char* endptr = NULL;
    unsigned long long v = strtoull(p, &endptr, 10);
    if (endptr == p) return default_value;
    return (size_t)v;
}

// 从 pos 开始解析形如 "key":"value" 的字符串值，写入 dst（带边界）
static int parse_string_value_after(const char* pos, const char* key, char* dst, size_t dst_cap) {
    const char* p = strstr(pos, key);
    if (!p) return 0;
    p += strlen(key);
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    if (*p != '"') return 0;
    p++;
    const char* q = strchr(p, '"');
    if (!q) return 0;
    size_t n = (size_t)(q - p);
    if (n >= dst_cap) n = dst_cap - 1;
    memcpy(dst, p, n);
    dst[n] = '\0';
    return 1;
}

// 解析 variables 数组，填充 g_meta_cache.vars，返回解析到的数量
static size_t parse_variables_from_meta(const char* json, meta_cache_t* cache) {
    if (!json || !cache) return 0;
    const char* p = json;
    size_t count = 0;
    while ((p = strstr(p, "\"name\"")) && count < (sizeof(cache->vars)/sizeof(cache->vars[0]))) {
        // 暂存一个对象，判断是否是 steps，steps 不进入数据区变量列表
        char name_buf[128] = {0};
        char type_buf[32]  = {0};
        size_t size_val    = 0;

        if (!parse_string_value_after(p, "\"name\"", name_buf, sizeof(name_buf))) break;
        parse_string_value_after(p, "\"type\"", type_buf, sizeof(type_buf));
        size_val = parse_number_value(p, "\"size\"", 0);

        int is_step = (strcmp(name_buf, "steps") == 0);
        if (!is_step) {
            var_def_t* vd = &cache->vars[count];
            strncpy(vd->name, name_buf, sizeof(vd->name)-1);
            strncpy(vd->type, type_buf, sizeof(vd->type)-1);
            vd->size = size_val;
            vd->is_step = 0;
            count++;
        }
        // 跳过本对象，移动到下一个 name
        const char* next = strstr(p + 6, "\"name\"");
        if (!next) break;
        p = next;
    }
    cache->var_count = count;
    return count;
}

// 根据类型字符串与字节数打印一个值；如未知类型则按 hex 打印
static void print_value_by_type(const char* name, const char* type, const char* data, size_t size) {
    if (type && strstr(type, "DOUBLE") && size == sizeof(double)) {
        double dv;
        memcpy(&dv, data, sizeof(double));
        log_printf("%s=%lf", name, dv);
        return;
    }
    if (type && strstr(type, "UINT64") && size == sizeof(uint64_t)) {
        uint64_t v; memcpy(&v, data, sizeof(uint64_t));
        log_printf("%s=%lu", name, (unsigned long)v);
        return;
    }
    if (type && strstr(type, "INT64") && size == sizeof(int64_t)) {
        int64_t v; memcpy(&v, data, sizeof(int64_t));
        log_printf("%s=%ld", name, (long)v);
        return;
    }
    if (type && strstr(type, "UINT32") && size == sizeof(uint32_t)) {
        uint32_t v; memcpy(&v, data, sizeof(uint32_t));
        log_printf("%s=%u", name, v);
        return;
    }
    if (type && strstr(type, "INT32") && size == sizeof(int32_t)) {
        int32_t v; memcpy(&v, data, sizeof(int32_t));
        log_printf("%s=%d", name, v);
        return;
    }
    if (type && strstr(type, "UINT16") && size == sizeof(uint16_t)) {
        uint16_t v; memcpy(&v, data, sizeof(uint16_t));
        log_printf("%s=%u", name, (unsigned)v);
        return;
    }
    if (type && strstr(type, "INT16") && size == sizeof(int16_t)) {
        int16_t v; memcpy(&v, data, sizeof(int16_t));
        log_printf("%s=%d", name, (int)v);
        return;
    }
    if (type && strstr(type, "UINT8") && size == sizeof(uint8_t)) {
        uint8_t v; memcpy(&v, data, sizeof(uint8_t));
        log_printf("%s=%u", name, (unsigned)v);
        return;
    }
    if (type && strstr(type, "INT8") && size == sizeof(int8_t)) {
        int8_t v; memcpy(&v, data, sizeof(int8_t));
        log_printf("%s=%d", name, (int)v);
        return;
    }
    // 未识别的类型，按十六进制输出
    log_printf("%s=0x", name);
    for (size_t i = 0; i < size; i++) {
        log_printf("%02x", (unsigned char)data[i]);
    }
}

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
            close_log_file();  // 关闭日志文件
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

// 将网络字节序的double转换为主机字节序
static inline double be64toh_double(const void* ptr) {
    uint64_t temp;
    memcpy(&temp, ptr, sizeof(uint64_t));
    temp = be64toh(temp);
    double result;
    memcpy(&result, &temp, sizeof(double));
    return result;
}



void  parse_record_finish(const char *buf, size_t buf_size){
    if(buf == NULL){
        log_printf("buf is null\n");
        return;
    }
    const char *body = buf + PACKET_HEADER_SIZE;  // 跳过消息头
    size_t body_size = buf_size - PACKET_HEADER_SIZE;
    switch ((unsigned char)buf[2]){
        case FUNCTION_FINISH:
            log_printf("[###RECORD_FINISH###]\n");
            if(body_size > 0){
                log_printf("Message Body: %.*s\n", (int)body_size, body);
            }
            break;
    
        default:
            log_printf("[###UNKNOWN type %d###]\n",buf[2]);
            break;
    }
}


void  parse_varmon_data(const char *buf, size_t buf_size){
    if(buf == NULL){
        log_printf("buf is null\n");
        return;
    }
    
    // 第一部分：nanomsg头 (24字节)
    const nanomsg_header_t *nanomsg_hdr = (const nanomsg_header_t *)buf;
    // 通用：指向通用头之后的负载区（META/FINISH 多为纯文本）
    const char *body_after_header = buf + PACKET_HEADER_SIZE;
    size_t body_after_header_size = buf_size - PACKET_HEADER_SIZE;
    
    switch (nanomsg_hdr->event_id)
    {
        case FUNCTION_META:
            log_printf("VarMon META (数据描述信息):\n");
            log_printf("  [nanomsg通用消息头]\n");
            log_printf("    msg_type: %u (VAR_MON_FUNCTION)\n", nanomsg_hdr->msg_type);  // 不需要转换
            log_printf("    version: %u\n", nanomsg_hdr->version);                      // 不需要转换
            log_printf("    event_id: %u (FUNCTION_META)\n", nanomsg_hdr->event_id);     // 不需要转换
            log_printf("    status_code: %u\n", nanomsg_hdr->status_code);               // 不需要转换
            log_printf("    msg_len: %u\n", ntohs(nanomsg_hdr->msg_len));               // 网络字节序
            log_printf("    node_id: %u\n", ntohs(nanomsg_hdr->node_id));               // 网络字节序
            log_printf("    task_id: %lu\n", be64toh(nanomsg_hdr->task_id));            // 网络字节序
            log_printf("    cmd_id: %lu\n", be64toh(nanomsg_hdr->cmd_id));              // 网络字节序
            if(body_after_header_size > 0){
                log_printf("  [元数据内容]\n");
                log_printf("    meta_data: %.*s\n", (int)body_after_header_size, body_after_header);

                // 解析并缓存元数据（按 cmd_id 分组）
                uint64_t cmd_id_host = be64toh(nanomsg_hdr->cmd_id);
                meta_cache_t* cache = meta_get_entry(cmd_id_host, 1);
                if (cache) {
                    reset_meta_cache(cache);
                    cache->unit_size = parse_number_value(body_after_header, "\"variable_monitor_unit_bytes\"", 0);
                    cache->interval_steps = parse_number_value(body_after_header, "\"interval_steps\"", 0);
                    parse_variables_from_meta(body_after_header, cache);
                    log_printf("  [已缓存元数据][cmd_id=%lu] unit_size=%zu, interval_steps=%zu, var_count=%zu\n", cmd_id_host, cache->unit_size, cache->interval_steps, cache->var_count);
                    for(size_t i = 0; i < cache->var_count; i++){
                        log_printf("    var[%zu]: name=\"%s\", type=\"%s\", size=%zu\n", i, cache->vars[i].name, cache->vars[i].type, cache->vars[i].size);
                    }
                } else {
                    log_printf("  [WARNING] 元数据缓存已满，无法为 cmd_id=%lu 建立缓存\n", cmd_id_host);
                }
            }
            break;
            
        case FUNCTION_DATA:
            // DATA 需要满足最小长度（含 varmon 头 + 步长）
            if (buf_size < PACKET_HEADER_SIZE + VARMON_HEADER_SIZE + sizeof(uint64_t)){
                log_printf("VarMon DATA packet too short: %zu bytes\n", buf_size);
                break;
            }
            {
                // 解析变量监控头与步长
                const varmon_header_t *varmon_hdr_raw = (const varmon_header_t *)(buf + PACKET_HEADER_SIZE);
                uint64_t unit_num = be64toh(varmon_hdr_raw->unit_num);
                const uint64_t *step_ptr = (const uint64_t *)(buf + PACKET_HEADER_SIZE + VARMON_HEADER_SIZE);
                uint64_t current_step = *step_ptr;
                uint64_t cmd_id_host = be64toh(nanomsg_hdr->cmd_id);
                meta_cache_t* cache = meta_get_entry(cmd_id_host, 0);
                if (!cache) {
                    log_printf("VarMon DATA [step=%lu, cmd_id=%lu]: 未找到对应META缓存，跳过详细解码\n", current_step, cmd_id_host);
                    break;
                }
                log_printf("VarMon DATA [step=%lu, cmd_id=%lu, interval_steps=%zu]:\n", current_step, cmd_id_host, cache->interval_steps);

                const char *data_ptr = buf + PACKET_HEADER_SIZE + VARMON_HEADER_SIZE + sizeof(uint64_t);
                size_t data_size = buf_size - PACKET_HEADER_SIZE - VARMON_HEADER_SIZE - sizeof(uint64_t);

                if(unit_num > 0 && data_size > 0){
                // 单元数量固定为1；数据区不含 steps，本地步数来自头部 current_step
                // 若元数据携带 unit_size，则以其为准，否则按缓存的变量 size 累计
                size_t expected_by_vars = 0;
                for(size_t vi = 0; vi < cache->var_count; vi++){
                    expected_by_vars += cache->vars[vi].size;
                }
                size_t unit_size = cache->unit_size ? cache->unit_size : expected_by_vars;

                if (data_size < unit_size) {
                    log_printf("  [WARNING] 数据总长度不足: data_size=%zu < unit_size=%zu\n", data_size, unit_size);
                }

                size_t offset = 0;
                // 打印变量名=值（按缓存的 size 与 type 解码），不包含 steps
                for(size_t vi = 0; vi < cache->var_count; vi++){
                    const var_def_t* vd = &cache->vars[vi];
                    if(offset + vd->size > data_size){
                        log_printf("  [WARNING] 数据不足: %s 需要%zu字节, 剩余%zu\n", vd->name, vd->size, data_size - offset);
                        break;
                    }
                    log_printf("  ");
                    print_value_by_type(vd->name, vd->type, data_ptr + offset, vd->size);
                    log_printf("\n");
                    offset += vd->size;
                }
                } else {
                    log_printf("  [WARNING] data_size=%zu, unit_num=%lu\n", data_size, unit_num);
                }
            }
            break;
            
        case FUNCTION_FINISH:
            log_printf("VarMon FINISH\n");
            if(body_after_header_size > 0){
                log_printf("  finish_data: %.*s\n", (int)body_after_header_size, body_after_header);
            }
            break;
            
        default:
            log_printf("[###UNKNOWN var mon type %d###]\n", nanomsg_hdr->event_id);
            break;
    }
}

void  parse_read_data(const char *buf, size_t buf_size){
    if(buf == NULL){
        log_printf("buf is null\n");
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
            log_printf("VarRead META: %.*s\n", (int)body_size, head);
            break;
        case FUNCTION_DATA:
            memcpy(&data_value_uint64, body, sizeof(uint64_t));
            memcpy(&data_value_double, body + sizeof(uint64_t), sizeof(double));
            log_printf("VarRead DATA: uint64_t:%lu, double:%lf\n", data_value_uint64, data_value_double);
            break;
        case FUNCTION_FINISH:
            log_printf("VarRead FINISH: %.*s\n", (int)body_size, head);
            break;
        default:
            log_printf("[###UNKNOWN var mon type %d###]\n",buf[2]);
            break;
    }
}

void  parse_step_debug_data(const char *buf, size_t buf_size){
    if(buf == NULL){
        log_printf("buf is null\n");
        return;
    }
    const char *head = buf + PACKET_HEADER_SIZE;  // 跳过消息头
    size_t body_size = buf_size - PACKET_HEADER_SIZE;
    switch (buf[2])
    {
        case DEBUG_STATUS_OFF:
            log_printf("DEBUG_STATUS_OFF\n");
            break;
        case DEBUG_STATUS_WAITING:
            log_printf("DEBUG_STATUS_WAITING\n");
            break;
        case DEBUG_STATUS_SUSPEND:
            log_printf("DEBUG_STATUS_SUSPEND\n");
            log_printf("Step debug message:\n %.*s\n", (int)body_size, head);
            break;
        case DEBUG_STATUS_RUNNING:
            log_printf("DEBUG_STATUS_RUNNING\n");
            break;
        case DEBUG_STATUS_EXIT:
            log_printf("DEBUG_STATUS_EXIT\n");
            break;
        default:
            log_printf("[###UNKNOWN var mon type %d###]\n",buf[2]);
            break;
    }
}

void print_message_body(const char *buf, size_t buf_size) {
    const char *body = buf + PACKET_HEADER_SIZE;  // 跳过消息头
    size_t body_size = buf_size - PACKET_HEADER_SIZE;
    
    if (body_size == 0) {
        return;
    }
    
    // 检查日志内容是否已经包含换行符
    // 如果推送方是一行一行推送，直接输出日志内容，确保有换行符结尾
    int has_newline = 0;
    if (body_size > 0 && body[body_size - 1] == '\n') {
        has_newline = 1;
    }
    
    // 直接输出日志内容，不添加额外前缀（推送方已经是一行一行推送）
    if (has_newline) {
        // 已经包含换行符，直接输出
        log_printf("%.*s", (int)body_size, body);
    } else {
        // 没有换行符，添加换行符
        log_printf("%.*s\n", (int)body_size, body);
    }
}

void  parse_task_status(char* buf){
    if(buf == NULL){
        log_printf("buf is null\n");
        return;
    }
    switch ((unsigned char)buf[2])
    {
        case SIMULATION_STARTED:
            log_printf("[###SIMULATION_STARTED###]\n");
            break;
        case INITIALIZING_CORE_RESOURCES:
            log_printf("[###INITIALIZING_CORE_RESOURCES###]\n");
            break;
        case LOADING_SO:
            log_printf("[###LOADING_SO###]\n");
            break;
        case LOADING_MODEL:
            log_printf("[###LOADING_MODEL###]\n");
            break;
        case EXECUTING_FB_INIT:
            log_printf("[###EXECUTING_FB_INIT###]\n");
            break;
        case WARMUP_STAGE:
            log_printf("[###WARMUP_STAGE###]\n");
            break;
        case RUNNING:
            log_printf("[###RUNNING###]\n");
            break;
        case RESOURCE_COLLECTION:
            log_printf("[###RESOURCE_COLLECTION###]\n");
            break;
        case SIMULATION_COMPLETED:
            log_printf("[###SIMULATION_COMPLETED###]\n");
            break;
        case SIMULATION_FAILED:
            log_printf("[###SIMULATION_FAILED###]\n");
            break;
        default:
            log_printf("[###UNKNOWN type %d###]\n",buf[2]);
            break;
    }
}

int main(int argc, char* argv[])
{
    char bind_addr[256];
    char* ip = "0.0.0.0";
    char* port;
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port> | %s <ip> <port>\n", argv[0], argv[0]);
        fprintf(stderr, "  <port>: 监听端口号 (IP默认为 0.0.0.0)\n");
        fprintf(stderr, "  <ip> <port>: IP地址和端口号\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s 11112\n", argv[0]);
        fprintf(stderr, "  %s 127.0.0.1 11112\n", argv[0]);
        fprintf(stderr, "  %s 192.168.1.100 11112\n", argv[0]);
        return 1;
    }
    
    if (argc == 2) {
        // 只有一个参数，作为端口号，IP使用默认值
        port = argv[1];
    } else {
        // 两个参数，第一个是IP，第二个是端口
        ip = argv[1];
        port = argv[2];
    }
    
    snprintf(bind_addr, sizeof(bind_addr), "tcp://%s:%s", ip, port);
    
    sock = -1;

    slave_register_stop_signal();

    // 初始化按 cmd_id 的元数据映射
    init_meta_map();

    int rv;

    if ((sock = nn_socket(AF_SP, NN_PULL)) < 0) {
        fatal("nn_socket");
    }

    if ((rv = nn_bind(sock, bind_addr)) < 0) {
        fatal("nn_bind");
    }
    
    // 打开日志文件
    if (open_log_file() != 0) {
        fprintf(stderr, "Warning: Failed to open log file, logging to file disabled\n");
    }
    
    log_printf("nanomsg server start at: %s\n", bind_addr);
    
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
            log_printf("Received packet is too short: %d bytes\n", bytes);
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
                log_printf("[###UNKNOWN type %d###]\n",buf[0]);
                break;
        }
        nn_freemsg(buf);
    }
    
    close_log_file();
    return 0;
}
