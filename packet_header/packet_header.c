#include "packet_header.h"


// 构造 nanomsg 头部并返回指针
nanomsg_header_t* construct_nanomsg_header(nanomsg_header_t *header, uint8_t msg_type, uint8_t version, uint8_t event_id, 
                                           uint8_t status_code, uint16_t msg_len, uint16_t node_id, 
                                           uint64_t task_id, uint64_t cmd_id) {
    if (!header) return NULL;

    header->msg_type = msg_type;
    header->version = version;
    header->event_id = event_id;
    header->status_code = status_code;
    header->msg_len = htons(msg_len);
    header->node_id = htons(node_id);
    header->task_id = htobe64(task_id);
    header->cmd_id = htobe64(cmd_id);

    return header;
}

// 解析 nanomsg 头部并返回指针
nanomsg_header_t* parse_nanomsg_header(nanomsg_header_t *net_header, nanomsg_header_t *host_header) {
    if (!net_header || !host_header) return NULL;

    host_header->msg_type = net_header->msg_type;
    host_header->version = net_header->version;
    host_header->event_id = net_header->event_id;
    host_header->status_code = net_header->status_code;
    host_header->msg_len = ntohs(net_header->msg_len);
    host_header->node_id = ntohs(net_header->node_id);
    host_header->task_id = be64toh(net_header->task_id);
    host_header->cmd_id = be64toh(net_header->cmd_id);

    return host_header;
}