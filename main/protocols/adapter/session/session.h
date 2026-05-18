#ifndef __SESSION_H__
#define __SESSION_H__
#include "osip_adapter.h"



typedef enum {
    SESSION_STATUS_IDLE = 0,
    SESSION_STATUS_REGISTERING,
    SESSION_STATUS_INVITING,
    SESSION_STATUS_IN_CALL,
    SESSION_STATUS_TERMINATING
} session_status_t ;



typedef struct {
    int protocol_inited;
    char uid[64];
    char session_id[64];
    session_status_t session_status;
    uint32_t seq;
    uint32_t last_keepalive_ms; // 上次命令时间ms, 包括invite的INVITE、ACK、BYE、REGISTER消息
    uint32_t last_req_message_ms;  // 上次发出消息时间 ms
    int last_req_message_seq; // 上次发出消息seq
    uint32_t last_traffic_ms;  // 上次的语音流量的时间ms, 用于判断通话是否活跃
    received_sip_message_ptr invite_200_ok_resp_message;
    char device_ip[16];
} session_state_machine_t;

int check_if_session_in_call();

void session_checking(void * param);


#endif