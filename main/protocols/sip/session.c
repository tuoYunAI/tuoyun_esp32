#include "sip.h"
#include "session.h"
#include "osip_adapter.h"
#include "osipparser2/osip_list.h"
#include "string.h"
/**
 * 用来表示被修饰的指针必须使用malloc申请, 将由函数移动所有权（即函数内部会释放该指针），
 * 调用者在调用后不应再使用该指针。
 */
#define MOVE

#define ADAPTER_LOG_TAG      "[SESSION]"
#define LOG_LEVEL_ENABLED    LOG_INFO_LEVEL
#include "adapter.h"

static void send_invite_ack();

static session_state_machine_t m_session_state = {
    .protocol_inited = 0,
    .uid = {0},
    .session_status = SESSION_STATUS_IDLE,
    .seq = 0,
    .last_keepalive_ms = 0,
    .invite_200_ok_resp_message = NULL,
    .device_ip = {0}
};

media_parameter_t g_audio_enc_media_param = {
    .ip = {0},
    .port = 0,
    .codec = "opus",
    .transport = "udp",
    .sample_rate = 16000,
    .channels = 1,
    .frame_duration = OPUS_FRAME_DURATION_MS,
    .encryption = "aes-128-cbc",
    .nonce = {0},
    .aes_key = {0}
};

media_parameter_t g_audio_dec_media_param = {0};

static osip_list_t m_received_sip_list;

int check_if_session_in_call(){
    return m_session_state.session_status == SESSION_STATUS_IN_CALL;
}

void test_print_session_state(){
    LOG_INFO("+++++++++++++++++++++session status+++++++++++++++\r\n"
        "        protocol_inited: %d\r\n"
        "        session_status: %d\r\n"
        "        seq: %ld\r\n"
        "        last_keepalive_ms: %ld\r\n"
        "        last_req_message_ms: %ld\r\n"
        "        last_req_message_seq: %d\r\n"
        "        invite_200_ok_resp_message: %p\r\n"
        "        device_ip: %s\r\n"
        "++++++++++++++++++++++++++++++++++++++++++++++++\r\n",
         m_session_state.protocol_inited,
         m_session_state.session_status,
         m_session_state.seq,
         m_session_state.last_keepalive_ms,
         m_session_state.last_req_message_ms,
         m_session_state.last_req_message_seq,
         m_session_state.invite_200_ok_resp_message,
         m_session_state.device_ip[0] ? m_session_state.device_ip : "null"         
    );    
}


static int transmit_sip(char *message){
    if (!message){
        return -1;
    }

    void *root = adapter_create_json_object();
    adapter_put_json_string_value(root, "protocol", "SIP");
    adapter_put_json_string_value(root, "payload", message);

    char* json_string = adapter_serialize_json_to_string(root);
    if (json_string) {
        adapter_transmit_mqtt_message(json_string);
    }

    adapter_delete_json_object(root);
    return 0;
}

sip_ret_t transmit_mcp_over_sip(const char *message){
    if (!message){
        return RET_ERROR;
    }

    void *root = adapter_create_json_object();
    adapter_put_json_string_value(root, "protocol", "MCP");
    adapter_put_json_string_value(root, "payload", message);

    char* json_string = adapter_serialize_json_to_string(root);
    if (json_string) {
        adapter_transmit_mqtt_message(json_string);
    }

    adapter_delete_json_object(root);
    return RET_OK;
}


static void proc_response_register(MOVE received_sip_message_ptr  message){
    LOG_INFO("Processing REGISTER response");
    adapter_lock_sip_mutex();
    do{
        if (m_session_state.session_status != SESSION_STATUS_REGISTERING){
            break;
        }
        if (is_response_ok(message)){
            m_session_state.last_keepalive_ms = adapter_get_system_ms();
        }
        m_session_state.session_status = SESSION_STATUS_IDLE;
        m_session_state.last_keepalive_ms = m_session_state.last_req_message_ms;
        m_session_state.last_req_message_ms = 0;
        m_session_state.last_req_message_seq = 0;
    }while(0);
    //test_print_session_state();
    adapter_unlock_sip_mutex();
    free(message);

    return;
}

static int clear_session(){
    LOG_INFO("Clearing session state");
    adapter_lock_sip_mutex();
    m_session_state.session_status = SESSION_STATUS_IDLE;
    m_session_state.last_req_message_ms = 0;
    m_session_state.last_req_message_seq = 0;

    if (m_session_state.invite_200_ok_resp_message){
        free(m_session_state.invite_200_ok_resp_message);
        m_session_state.invite_200_ok_resp_message = NULL;
    }
    adapter_unlock_sip_mutex();
    return 0;
}



static int proc_response_invite(MOVE received_sip_message_ptr message){
    LOG_INFO("Processing INVITE response");
    adapter_lock_sip_mutex();
    int ret = 0;
    do
    {
        if (m_session_state.session_status != SESSION_STATUS_INVITING){
            ret = -1;
            break;
        }

        if (is_response_ok(message) && message->cseq_num == m_session_state.last_req_message_seq) {

            // 进入通话状态
            m_session_state.invite_200_ok_resp_message = message;

            m_session_state.session_status = SESSION_STATUS_IN_CALL;
            m_session_state.last_keepalive_ms = adapter_get_system_ms();
            m_session_state.last_req_message_ms = 0;
            m_session_state.last_req_message_seq = 0;
            strncpy(m_session_state.session_id, message->call_id, sizeof(m_session_state.session_id)-1);

            downlink_sdp_parameter_t sdp = {0};
            if (message->body_length > 0) {
                // 解析 SDP
                if (parse_sdp(message->message_body, &sdp) == 0) {
                    // 这里可以根据解析结果配置音频参数
                    strncpy(g_audio_dec_media_param.ip, sdp.ip, sizeof(g_audio_dec_media_param.ip)-1);
                    g_audio_dec_media_param.port = sdp.port;
                    strncpy(g_audio_dec_media_param.codec, sdp.codec, sizeof(g_audio_dec_media_param.codec)-1);
                    strncpy(g_audio_dec_media_param.transport, sdp.transport, sizeof(g_audio_dec_media_param.transport)-1);
                    g_audio_dec_media_param.sample_rate = sdp.sample_rate;
                    g_audio_dec_media_param.channels = sdp.channels;
                    g_audio_dec_media_param.frame_duration = sdp.frame_duration;
                    strncpy(g_audio_dec_media_param.encryption, sdp.encryption, sizeof(g_audio_dec_media_param.encryption)-1);
                    memcpy(g_audio_dec_media_param.nonce, sdp.nonce, sizeof(g_audio_dec_media_param.nonce));
                    memcpy(g_audio_dec_media_param.aes_key, sdp.aes_key, sizeof(g_audio_dec_media_param.aes_key));

                    if (adapter_start_traffic_tunnel(&g_audio_dec_media_param) == 0){
                        on_call_established(m_session_state.session_id, &g_audio_dec_media_param);
                        // 发送 ACK 确认
                        send_invite_ack();
                        break;
                    }
                }
            }
        }else{
            if(message->status_code == 403){
                if (message->x_reason_code == CALL_ERROR_MEMBERSHIP_INVALID){
                    on_call_ack_error(CALL_ERROR_MEMBERSHIP_INVALID);
                }

            }
        }
        ret = -1;
    } while (0);

    if (ret != 0){
        m_session_state.session_status = SESSION_STATUS_IDLE;
        m_session_state.last_req_message_ms = 0;
        m_session_state.last_req_message_seq = 0;
        m_session_state.invite_200_ok_resp_message = NULL;
        free(message);
    }
    adapter_unlock_sip_mutex();
    return ret;
}


static void proc_response_info(MOVE received_sip_message_ptr message){

    if (message->cseq_num != m_session_state.last_req_message_seq){
        return;
    }
    if (is_response_ok(message)) {
        LOG_INFO("INFO request acknowledged");
    }
    m_session_state.last_req_message_ms = 0;
    m_session_state.last_req_message_seq = 0;
    free(message);
    return;
}

static void proc_response_bye(MOVE received_sip_message_ptr message){
    clear_session();
    adapter_clear_traffic_tunnel();
    on_call_terminated_ack();
    free(message);
    return;
}

static void proc_message(void* parse, server_message_notify_ptr notify){

    char ev[16] = {0};
    strncpy(ev, adapter_get_json_string_value(parse, "event"), sizeof(ev)-1);
    char *val = adapter_get_json_string_value(parse, "command");
    if (val){
        strncpy(notify->command, val, sizeof(notify->command)-1);
    }
    val = adapter_get_json_string_value(parse, "text");
    if (val){
        strncpy(notify->message, val, sizeof(notify->message)-1);
    }
    val = adapter_get_json_string_value(parse, "emotion");
    if (val){
        strncpy(notify->emotion, val, sizeof(notify->emotion)-1);
    }
    on_server_message_notify(notify);
}

static void proc_message_alert(void* data){
    server_message_notify_t notify = {
        .event = SERVER_MESSAGE_MSG,
        .level = MESSAGE_ALERT
    };
    proc_message(data, &notify);
}

static void proc_message_activate(void* data){
    server_message_notify_t notify = {
        .event = SERVER_MESSAGE_STATUS,
        .status = SERVER_STATUS_ACTIVATED
    };
    proc_message(data, &notify);
}


static void proc_message_expire(void* data){
    server_message_notify_t notify = {
        .event = SERVER_MESSAGE_STATUS,
        .status = SERVER_STATUS_MEMBERSHIP_INVALID
    };
    proc_message(data, &notify);
}

static void proc_request_message(MOVE received_sip_message_ptr  message){

    if (message->body_length > 0) {
        // 解析 JSON
        void *parse = adapter_parse_json_string(message->message_body);
        if (parse) {
            char ev[16] = {0};
            strncpy(ev, adapter_get_json_string_value(parse, "event"), sizeof(ev)-1);
            if (strcmp(ev, DEVICE_CTRL_EVENT_ALERT) == 0){
                proc_message_alert(parse);
            }else if (strcmp(ev, DEVICE_CTRL_EVENT_ACTIVATE) == 0){
                proc_message_activate(parse);
            }else if (strcmp(ev, DEVICE_CTRL_EVENT_EXPIRE) == 0){
                proc_message_expire(parse);
            }else {
                adapter_delete_json_object(parse);
                free(message);
                return;
            }
            adapter_delete_json_object(parse);

            char *out_msg = NULL;
            size_t out_len = 0;
            int ret = build_200_ok_response(message, &out_msg, &out_len);
            if (ret != 0 || out_msg == NULL || out_len == 0){
                free(message);
                return;
            }
            transmit_sip(out_msg);
            free_sip_message(out_msg);
            out_msg = NULL;
        }
    }
    free(message);
    return;
}

static void proc_request_bye(MOVE received_sip_message_ptr message){
    adapter_lock_sip_mutex();
    if (strncmp(message->call_id, m_session_state.session_id, sizeof(m_session_state.session_id)) != 0){
        LOG_INFO("BYE call_id mismatch, ignore: %s vs %s", message->call_id, m_session_state.session_id);
        free(message);
        return;
    }
    LOG_INFO("BYE call_id matched: %s", message->call_id);
    adapter_lock_sip_mutex();
    // 处理 BYE 请求
    m_session_state.session_status = SESSION_STATUS_IDLE;
    LOG_INFO("Processing BYE request from server");
    on_call_terminated_by_server();
	
    char *out_msg = NULL;
    size_t out_len = 0;
    int ret = build_200_ok_response(message, &out_msg, &out_len);
    if (ret != 0 || out_len == 0){
        LOG_INFO("failed to response to BYE");
    }else{
        transmit_sip(out_msg);
        free_sip_message(out_msg);
        out_msg = NULL;
    }

    clear_session();
    adapter_clear_traffic_tunnel();
    adapter_unlock_sip_mutex();
    free(message);
    return;
}

static void proc_request_info(MOVE received_sip_message_ptr message){
    LOG_INFO("Processing INFO request");
    adapter_lock_sip_mutex();
    if (m_session_state.session_status != SESSION_STATUS_IN_CALL){
        adapter_unlock_sip_mutex();
        free(message);
        return;
    }

    if (strncmp(message->call_id, m_session_state.session_id, sizeof(m_session_state.session_id)) != 0){
        adapter_unlock_sip_mutex();
        free(message);
        return;
    }

    adapter_unlock_sip_mutex();

    if (message->body_length > 0) {
        // 解析 JSON
        void* parse = adapter_parse_json_string(message->message_body);
        if (parse) {
            message_session_event_ptr session_event = malloc(sizeof(message_session_event_t));
            memset(session_event, 0, sizeof(message_session_event_t));
            char ev[16] = {0};
            char* val = adapter_get_json_string_value(parse, "event");
            if (val){
                strncpy(ev, val, sizeof(ev)-1);
            }
            if (strcmp(ev, DEVICE_CTRL_EVENT_STT) == 0){
                session_event->event = CTRL_EVENT_USER_TEXT;
            }else if (strcmp(ev, DEVICE_CTRL_EVENT_SPEAKER) == 0){
                session_event->event = CTRL_EVENT_SPEAKER;
            }else {
                adapter_delete_json_object(parse);
                free(message);
                return;
            }
            char st[33] = {0};
            val = adapter_get_json_string_value(parse, "command");
            if (val){
                strncpy(st, val, sizeof(st)-1);
            }
            if (strcmp(st, WORKING_CMD_START) == 0){
                session_event->status = WORKING_STATUS_START;
            }else if (strcmp(st, WORKING_CMD_STOP) == 0){
                session_event->status = WORKING_STATUS_STOP;
            }else if (strcmp(st, WORKING_CMD_TEXT) == 0 || strcmp(st, WORKING_CMD_SENTENCE_START) == 0){
                session_event->status = WORKING_STATUS_TEXT;
            }else {
                LOG_INFO("Unknown working command: %s", st);
                session_event->status = WORKING_STATUS_INVALID;
            }
            val = adapter_get_json_string_value(parse, "text");
            if (val){
                strncpy(session_event->text, val, sizeof(session_event->text)-1);
            }

            val = adapter_get_json_string_value(parse, "emotion");
            if (val){
                strncpy(session_event->emotion, val, sizeof(session_event->emotion)-1);
            }
            on_server_session_update_notify(session_event);
            adapter_delete_json_object(parse);

            char *out_msg = NULL;
            size_t out_len = 0;
            int ret = build_200_ok_response(message, &out_msg, &out_len);
            if (ret != 0 || out_msg == NULL || out_len == 0){
                free(message);
                return;
            }
            transmit_sip(out_msg);
            free_sip_message(out_msg);
            out_msg = NULL;
            free(session_event);
            session_event = NULL;
        }
    }

    free(message);
    return;
}


void send_register(void *param){

    LOG_INFO("Sending REGISTER request");
    adapter_lock_sip_mutex();

    do{

        if (m_session_state.session_status > SESSION_STATUS_REGISTERING){
            break;
        }

        uint32_t ms = adapter_get_system_ms();
        if (m_session_state.last_keepalive_ms > 0 && (m_session_state.last_keepalive_ms + REGISTER_EXPIRE_SECOND * 1000) > ms){
            break;
        }

        sip_register_param_t register_param = {
            .uid = m_session_state.uid,
            .device_ip = m_session_state.device_ip,
            .expires_sec = REGISTER_EXPIRE_SECOND,
            .cseq_num = (int)(m_session_state.seq)
        };
        char* message = NULL;
        size_t message_len = 0;
        int ret = build_register(&register_param, &message,  &message_len);
        if (ret != 0){
            break;
        }

        m_session_state.session_status = SESSION_STATUS_REGISTERING;
        m_session_state.last_req_message_ms = ms;
        m_session_state.last_req_message_seq = m_session_state.seq;
        transmit_sip(message);

        m_session_state.seq++;
        free_sip_message(message);
    }while (0);

    adapter_unlock_sip_mutex();
    return;
}

int init_call(const char* wake_up_word){
    LOG_INFO("Initiating call with wake-up word: %s", wake_up_word ? wake_up_word : "null");
    adapter_lock_sip_mutex();
    int ret = 0;

    do{
        if (m_session_state.session_status > SESSION_STATUS_INVITING){
            ret = -1;
            break;
        }

        uint32_t ms = adapter_get_system_ms();
        uplink_sdp_parameter_t sdp_param = {
            .uid = m_session_state.uid,
            .device_ip = m_session_state.device_ip,
            .codec = g_audio_enc_media_param.codec,
            .sample_rate = g_audio_enc_media_param.sample_rate,
            .channels = g_audio_enc_media_param.channels,
            .frame_duration_ms = OPUS_FRAME_DURATION_MS,
            .support_mcp = SESSION_SUPORT_MCP,
            .cbr = SESSION_OPUS_CBR,
            .frame_gap = SESSION_AUDIO_FRAME_GAP,
            .wake_up_word = wake_up_word
        };

        sip_invite_param_t invite = {
            .uid = m_session_state.uid,
            .device_ip = m_session_state.device_ip,
            .cseq_num = (int)(m_session_state.seq),
            .sdp = &sdp_param

        };
        char* message = NULL;
        size_t message_len = 0;
        int ret = build_invite(&invite, &message,  &message_len);
        if (ret != 0){
            break;
        }
        m_session_state.session_status = SESSION_STATUS_INVITING;
        m_session_state.last_req_message_ms = ms;
        m_session_state.last_req_message_seq = m_session_state.seq;
        m_session_state.seq++;
        transmit_sip(message);
        free_sip_message(message);

    }while(0);

    adapter_unlock_sip_mutex();
    return ret;
}


int finish_call(){
    LOG_INFO("Finishing call");
    adapter_lock_sip_mutex();
    int ret = 0;
    do{

        if (m_session_state.session_status != SESSION_STATUS_IN_CALL){
            ret = -1;
            break;
        }

        char* message = NULL;
        size_t message_len = 0;

        ret = build_bye(m_session_state.invite_200_ok_resp_message,
                m_session_state.uid,
                m_session_state.device_ip,
                m_session_state.seq,
                &message,
                &message_len
            );
        if (ret == 0){
            m_session_state.seq++;
            transmit_sip(message);
            free_sip_message(message);
        }
        clear_session();
        adapter_clear_traffic_tunnel();

    }while(0);

    adapter_unlock_sip_mutex();
    return ret;
}

static void send_invite_ack(){
    if (!m_session_state.invite_200_ok_resp_message){
        return;
    }

    int ret = -1;
    char *out_msg = NULL;
    size_t out_len = 0;

    ret = build_ack(m_session_state.invite_200_ok_resp_message,
                    m_session_state.uid,
                    m_session_state.device_ip,
                    m_session_state.seq++,
                    &out_msg,
                    &out_len);
    if (ret != 0 || !out_msg) {
        goto cleanup;
    }

    transmit_sip(out_msg);

cleanup:
    if (out_msg) free_sip_message(out_msg);
    return;
}

static int send_listening_status(const char* cmd, const char* mode){

    if(!cmd || (strcmp(cmd, WORKING_CMD_START) != 0 && strcmp(cmd, WORKING_CMD_STOP) != 0)){
        return -1;
    }
    info_param_t info = {0};
    strncpy(info.event, "listen", sizeof(info.event)-1);
    if (cmd && strlen(cmd) > 0){
        strncpy(info.command, cmd, sizeof(info.command)-1);
    }
    if (mode && strlen(mode) > 0){
        strncpy(info.mode, mode, sizeof(info.mode)-1);
    }

    LOG_INFO("Sending listening status: command=%s, mode=%s", info.command, info.mode);
    adapter_lock_sip_mutex();
    int ret = 0;
    do{

        if (m_session_state.session_status != SESSION_STATUS_IN_CALL){
            ret = -1;
            break;
        }

        if (!m_session_state.invite_200_ok_resp_message){
            ret = -1;
            break;
        }

        char* message = NULL;
        size_t message_len = 0;
        int ret = build_listen_info(
            m_session_state.invite_200_ok_resp_message,
            m_session_state.uid,
            m_session_state.device_ip,
            m_session_state.seq,
            &info,
            &message,
            &message_len);
        if (ret != 0){
            break;
        }

        uint32_t ms = adapter_get_system_ms();
        m_session_state.last_req_message_ms = ms;
        m_session_state.last_req_message_seq = m_session_state.seq;
        m_session_state.seq++;
        transmit_sip(message);
        free_sip_message(message);

    }while(0);

    adapter_unlock_sip_mutex();
    return ret;
}


void send_abort_speaking(abort_reason_t reason){

    info_param_t info = {0};
    strncpy(info.event, "listen", sizeof(info.event)-1);
    strncpy(info.command, "interrupt", sizeof(info.command)-1);
    LOG_INFO("Sending abort speaking: reason=%d", reason);
    adapter_lock_sip_mutex();

    do{

        if (m_session_state.session_status != SESSION_STATUS_IN_CALL){
            break;
        }

        if (!m_session_state.invite_200_ok_resp_message){
            break;
        }

        char* message = NULL;
        size_t message_len = 0;
        int ret = build_listen_info(
            m_session_state.invite_200_ok_resp_message,
            m_session_state.uid,
            m_session_state.device_ip,
            m_session_state.seq,
            &info,
            &message,
            &message_len);
        if (ret != 0){
            break;
        }

        uint32_t ms = adapter_get_system_ms();
        m_session_state.last_req_message_ms = ms;
        m_session_state.last_req_message_seq = m_session_state.seq;
        m_session_state.seq++;
        transmit_sip(message);
        free_sip_message(message);

    }while(0);

    adapter_unlock_sip_mutex();
    return;
}


void send_start_listening(listening_mode_t mode){
    char*mode_str = NULL;
    switch(mode){
        case LISTENING_MODE_AUTO_STOP:
            mode_str = "auto";
            break;
        case LISTENING_MODE_REALTIME:
            mode_str = "realtime";
            break;
        default:
            mode_str = "manual";
            break;
    }
    send_listening_status(WORKING_CMD_START, mode_str);
}

void send_stop_listening(){
    send_listening_status(WORKING_CMD_STOP, NULL);
}



void handle_received_sip(const char *data, size_t len)
{
    received_sip_message_ptr msg_info = NULL;
    int result = sip_parse_incoming_message(data, len, &msg_info);

    if(result == 0){
        if (!is_response_message(msg_info)) {
            // 设备只会收到如下几种服务器下发的
            if (strcmp(msg_info->method, "MESSAGE") == 0) {
                // 服务器下发事件
                proc_request_message(msg_info);

            } else if (strcmp(msg_info->method, "BYE") == 0) {
                // 服务器释放会话
                proc_request_bye(msg_info);
            } else if (strcmp(msg_info->method, "INFO") == 0) {
                // 会话中服务器下发更新消息
                proc_request_info(msg_info);
            }else{
                free(msg_info);
            }
        } else{
            // 处理响应消息
            if (strcmp(msg_info->method, "REGISTER") == 0) {
                proc_response_register(msg_info);
            } else if (strcmp(msg_info->method, "INVITE") == 0) {
                proc_response_invite(msg_info);
            } else if (strcmp(msg_info->method, "INFO") == 0) {
                proc_response_info(msg_info);
            }  else if (strcmp(msg_info->method, "BYE") == 0) {
                proc_response_bye(msg_info);
            } else {
                free(msg_info);
            }
        }
    }
}


void handle_received_mqtt_message(const char *data, size_t len){
    if (!data || len == 0){
        return;
    }

#ifdef SIP_MESSAGE_CACHED_IN_LIST
    char* buf = malloc(len + 1);
    if (!buf){
        return;
    }

    strncpy(((char*)buf), data, len);
    buf[len] = 0;

    adapter_lock_sip_list_mutex();

    if (osip_list_add(&m_received_sip_list, buf, -1) < 0){
        free(buf);
        adapter_unlock_sip_list_mutex();
        return;
    }

    adapter_unlock_sip_list_mutex();
#else
    void *root = adapter_parse_json_string((char *)data);
    if(root != NULL){
        char protocol[10] = {0};
        strcpy(protocol, adapter_get_json_string_value(root, "protocol"));
        char* payload = adapter_get_json_string_value(root, "payload");
        if (strcmp("SIP", protocol) == 0){
            handle_received_sip(payload, strlen(payload));
        }else
        if (strcmp("MCP", protocol) == 0){
            on_server_mcp_call(payload);
        }
        adapter_delete_json_object(root);
        root = NULL;

    }
#endif
    return;

}



void session_checking(void *param){
    if (!m_session_state.protocol_inited){
        return;
    }

    LOG_INFO("Session checking...");
    adapter_lock_sip_mutex();
    uint32_t ms = adapter_get_system_ms();

    // 检查注册状态
    if (m_session_state.session_status == SESSION_STATUS_REGISTERING){
        if ((m_session_state.last_req_message_ms + COMMAND_TIMEOUT_MS) <= ms){
            m_session_state.session_status = SESSION_STATUS_IDLE;
            m_session_state.last_req_message_ms = 0;
            m_session_state.last_req_message_seq = 0;
        }
    }else
    if (m_session_state.session_status == SESSION_STATUS_INVITING){
        if ((m_session_state.last_req_message_ms + COMMAND_TIMEOUT_MS) <= ms){
            m_session_state.session_status = SESSION_STATUS_IDLE;
            m_session_state.last_req_message_ms = 0;
            m_session_state.last_req_message_seq = 0;

            on_call_ack_error(CALL_ERROR_SERVER_NO_ANSWER);
        }
    }else
    if (m_session_state.session_status == SESSION_STATUS_TERMINATING){

        if ((m_session_state.last_req_message_ms + COMMAND_TIMEOUT_MS) <= ms){
            m_session_state.session_status = SESSION_STATUS_IDLE;
            m_session_state.last_req_message_ms = 0;
            m_session_state.last_req_message_seq = 0;
        }
    }

    adapter_unlock_sip_mutex();
    return;
}


void mqtt_proc_task(void *param){
    while (1) {
        adapter_lock_sip_list_mutex();
        int list_size = osip_list_size(&m_received_sip_list);
        if (list_size == 0) {
            adapter_unlock_sip_list_mutex();
            adapter_task_delay(50);
            continue;
        }
        uint8_t* msg = (uint8_t*)osip_list_get(&m_received_sip_list, 0);
        if (msg) {
            osip_list_remove(&m_received_sip_list, 0);
        }
        adapter_unlock_sip_list_mutex();

        if (msg) {
            // 处理接收到的消息
            void *root = adapter_parse_json_string((char *)msg);
            if(root != NULL){
                char protocol[10] = {0};
                strcpy(protocol, adapter_get_json_string_value(root, "protocol"));
                char* payload = adapter_get_json_string_value(root, "payload");
                if (strcmp("SIP", protocol) == 0){
                    handle_received_sip(payload, strlen(payload));
                }else
                if (strcmp("MCP", protocol) == 0){
                    on_server_mcp_call(payload);
                }
                adapter_delete_json_object(root);
                root = NULL;

            }
            free(msg);
        }
    }
}

void init_session_module(const char* uid, const char* device_ip){

    init_sip();
    LOG_INFO("Initializing session module with UID: %s, Device IP: %s", uid, device_ip);
    adapter_lock_sip_mutex();
    m_session_state.protocol_inited = 1;
    size_t len = sizeof(m_session_state.uid);
    strncpy(m_session_state.uid, uid, len);
    m_session_state.uid[len - 1] = '\0';
    strncpy(m_session_state.device_ip, device_ip, sizeof(m_session_state.device_ip)-1);
    m_session_state.seq = adapter_get_system_ms()%1000;
    adapter_unlock_sip_mutex();

#ifdef SIP_MESSAGE_CACHED_IN_LIST
    osip_list_init(&m_received_sip_list);
    adapter_start_thread(mqtt_proc_task, "mqtt_proc_task", 1024*8, 16);
#else
    send_register(NULL);
#endif

    adapter_start_periodic_task(session_checking, 50 * 1000, 1024*8, NULL);
    adapter_start_periodic_task(send_register, 60 * 1000, 1024*8, NULL);
}
