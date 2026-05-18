#include "../protocol.h"
#include "session.h"
#include "osip_adapter.h"
#include "osipparser2/osip_list.h"
#include "string.h"


#define ADAPTER_LOG_TAG        "[SESSION]"
#define LOG_LEVEL_ENABLED      LOG_INFO_LEVEL
#include "../adapter.h"

static void send_invite_ack();

static void reject_invite_request(received_sip_message_ptr message, int status_code, const char *reason_phrase);
static void reject_info_request(received_sip_message_ptr message, int status_code, const char *reason_phrase);
static register_param_t m_register_param = {
    .scenario = 1,
    .battery = 0,
    .network = {
        .type = WIFI,
        .rssi = 0
    }
};

static session_state_machine_t m_session_state = {
    .protocol_inited = 0,
    .uid = {0},
    .session_status = SESSION_STATUS_IDLE,
    .seq = 0,
    .last_keepalive_ms = 0,
    .last_traffic_ms = 0,
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
    .aes_key = {0},
    .idle_timeout = 60 // 默认空闲超时时间60秒
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

void report_traffic_active(){
    if (check_if_session_in_call()){
        m_session_state.last_traffic_ms = adapter_get_system_ms();
    }
}

void report_register_status(register_param_ptr  param){
    if (!param) return;
    m_register_param.battery = param->battery;
    m_register_param.network = param->network;
    m_register_param.scenario = param->scenario;
    m_register_param.network.rssi = param->network.rssi;
    m_register_param.network.type = param->network.type;
}

static sip_ret_t transmit_sip(char *message){
    if (!message){
        return RET_ERROR;
    }

    void *root = adapter_create_json_object();
    adapter_put_json_string_value(root, "protocol", "SIP");
    adapter_put_json_string_value(root, "payload", message);

    char* json_string = adapter_serialize_json_to_string(root);
    if (json_string) {
        adapter_transmit_mqtt_message(json_string);
    }

    adapter_delete_json_object(root);
    return RET_OK;
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
    test_print_session_state();
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



static sip_ret_t proc_response_invite(MOVE received_sip_message_ptr message){
    LOG_INFO("Processing INVITE response");
    adapter_lock_sip_mutex();
    int ret = RET_OK;
    do
    {
        if (m_session_state.session_status != SESSION_STATUS_INVITING){
            ret = RET_ERROR;
            break;
        }

        if (is_response_ok(message) && message->cseq_num == m_session_state.last_req_message_seq) {

            // 进入通话状态
            m_session_state.invite_200_ok_resp_message = message;

            m_session_state.session_status = SESSION_STATUS_IN_CALL;
            m_session_state.last_keepalive_ms = adapter_get_system_ms();
            m_session_state.last_traffic_ms = m_session_state.last_keepalive_ms;
            m_session_state.last_req_message_ms = 0;
            m_session_state.last_req_message_seq = 0;
            strncpy(m_session_state.session_id, message->call_id, sizeof(m_session_state.session_id)-1);

            downlink_sdp_parameter_t sdp = {0};
            if (message->body_length > 0) {
                // 解析 SDP
                if (parse_sdp(message->message_body, &sdp) == RET_OK) {
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
        ret = RET_ERROR;
    } while (0);

    if (ret != RET_OK){
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


static void proc_request_invite(MOVE received_sip_message_ptr  message){
    LOG_INFO("Processing INVITE request");

    adapter_lock_sip_mutex();
    int ret = RET_ERROR;
    char *out_msg = NULL;
    size_t out_len = 0;
    char to_tag[33] = {0};
    received_sip_message_ptr dialog_anchor = NULL;
    downlink_sdp_parameter_t sdp = {0};

    do {
        if (m_session_state.session_status != SESSION_STATUS_IDLE) {
            LOG_INFO("Reject INVITE while session is busy: %d", m_session_state.session_status);
            reject_invite_request(message, 486, "Busy Here");
            clear_session();
            on_call_terminated_by_server();
            adapter_clear_traffic_tunnel();
            break;
        }

        if (message->body_length == 0 || parse_sdp(message->message_body, &sdp) != RET_OK) {
            LOG_INFO("Invalid SDP in INVITE request");
            reject_invite_request(message, 488, "Not Acceptable Here");
            break;
        }
        LOG_INFO("Received INVITE with SDP: ip=%s, port=%d, codec=%s, transport=%s, sample_rate=%d, channels=%d, frame_duration=%d, encryption=%s",
                 sdp.ip, sdp.port, sdp.codec, sdp.transport, sdp.sample_rate, sdp.channels, sdp.frame_duration, sdp.encryption);
        uplink_sdp_parameter_t sdp_param = {
            .session_id = {0},
            .uid = m_session_state.uid,
            .device_ip = m_session_state.device_ip,
            .codec = g_audio_enc_media_param.codec,
            .sample_rate = g_audio_enc_media_param.sample_rate,
            .channels = g_audio_enc_media_param.channels,
            .frame_duration_ms = OPUS_FRAME_DURATION_MS,
            .support_mcp = SESSION_SUPORT_MCP,
            .cbr = SESSION_OPUS_CBR,
            .frame_gap = SESSION_AUDIO_FRAME_GAP,
            .wake_up_word = NULL,
            .support_frame_aggregation = SESSION_SUPPORT_FRAME_AGGREGATION,
            .support_redundant = 0
        };
        strncpy(sdp_param.session_id, sdp.session_id, sizeof(sdp_param.session_id) - 1);
        if (build_invite_200_ok_response(message,
                                         m_session_state.uid,
                                         m_session_state.device_ip,
                                         &sdp_param,
                                         to_tag,
                                         sizeof(to_tag),
                                         &out_msg,
                                         &out_len) != RET_OK ||
            !out_msg || out_len == 0) {
            LOG_INFO("Failed to build 200 OK for INVITE request");
            break;
        }

        dialog_anchor = build_dialog_anchor_from_invite_request(message, to_tag, m_session_state.uid, m_session_state.device_ip);
        if (!dialog_anchor) {
            LOG_INFO("Failed to cache dialog state for INVITE request");
            break;
        }

        strncpy(g_audio_dec_media_param.ip, sdp.ip, sizeof(g_audio_dec_media_param.ip) - 1);
        g_audio_dec_media_param.port = sdp.port;
        strncpy(g_audio_dec_media_param.codec, sdp.codec, sizeof(g_audio_dec_media_param.codec) - 1);
        strncpy(g_audio_dec_media_param.transport, sdp.transport, sizeof(g_audio_dec_media_param.transport) - 1);
        g_audio_dec_media_param.sample_rate = sdp.sample_rate;
        g_audio_dec_media_param.channels = sdp.channels;
        g_audio_dec_media_param.frame_duration = sdp.frame_duration;
        strncpy(g_audio_dec_media_param.encryption, sdp.encryption, sizeof(g_audio_dec_media_param.encryption) - 1);
        memcpy(g_audio_dec_media_param.nonce, sdp.nonce, sizeof(g_audio_dec_media_param.nonce));
        memcpy(g_audio_dec_media_param.aes_key, sdp.aes_key, sizeof(g_audio_dec_media_param.aes_key));

        if (adapter_start_traffic_tunnel(&g_audio_dec_media_param) != 0) {
            LOG_INFO("Failed to start traffic tunnel for INVITE request");
            break;
        }

        transmit_sip(out_msg);

        m_session_state.session_status = SESSION_STATUS_IN_CALL;
        m_session_state.last_keepalive_ms = adapter_get_system_ms();
        m_session_state.last_traffic_ms = m_session_state.last_keepalive_ms;
        m_session_state.last_req_message_ms = 0;
        m_session_state.last_req_message_seq = 0;
        strncpy(m_session_state.session_id, message->call_id, sizeof(m_session_state.session_id) - 1);
        m_session_state.session_id[sizeof(m_session_state.session_id) - 1] = '\0';
        m_session_state.invite_200_ok_resp_message = dialog_anchor;
        dialog_anchor = NULL;

        on_call_established(m_session_state.session_id, &g_audio_dec_media_param);
        ret = RET_OK;
    } while (0);

    if (dialog_anchor) {
        free(dialog_anchor);
    }
    if (out_msg) {
        free_sip_message(out_msg);
    }
    if (ret != RET_OK) {
        adapter_clear_traffic_tunnel();
    }

    adapter_unlock_sip_mutex();
    free(message);
}

static void reject_invite_request(received_sip_message_ptr message, int status_code, const char *reason_phrase)
{
    char *out_msg = NULL;
    size_t out_len = 0;

    if (!message || !reason_phrase) {
        return;
    }

    if (build_invite_error_response(message,
                                    m_session_state.uid,
                                    m_session_state.device_ip,
                                    status_code,
                                    reason_phrase,
                                    &out_msg,
                                    &out_len) != RET_OK ||
        !out_msg || out_len == 0) {
        LOG_INFO("Failed to build INVITE error response: %d %s", status_code, reason_phrase);
        return;
    }

    transmit_sip(out_msg);
    free_sip_message(out_msg);
}

static void reject_info_request(received_sip_message_ptr message, int status_code, const char *reason_phrase)
{

    char *out_msg = NULL;
    size_t out_len = 0;

    if (!message || !reason_phrase) {
        return;
    }
    LOG_INFO("Rejecting INFO request: %d %s", status_code, reason_phrase);

    if (build_error_response(message,
                             status_code,
                             reason_phrase,
                             &out_msg,
                             &out_len) != RET_OK ||
        !out_msg || out_len == 0) {
        LOG_INFO("Failed to build INFO error response: %d %s", status_code, reason_phrase);
        return;
    }

    transmit_sip(out_msg);
    free_sip_message(out_msg);
}



static void proc_request_message(MOVE received_sip_message_ptr  message){

    if (message->body_length > 0) {
        // 解析 JSON
        void *received_msg = NULL;
        dcp_cmd_type_t cmd_type = parse_dcp_message(message->message_body, &received_msg);

        if (cmd_type == INVALID_CMD || received_msg == NULL) {
            LOG_INFO("Unknown command received in INFO message");
            free(message);
            return;
        }

        switch (cmd_type)
        {
        case EVENT_SYSTEM_NOTIFICATION:
            event_system_notification_ptr notification = (event_system_notification_ptr)received_msg;
            on_server_notify(notification);
            break;    
        case EVENT_DEVICE_LIFECYCLE:
            event_device_lifecycle_ptr lifecycle = (event_device_lifecycle_ptr)received_msg;
            on_server_lifecycle_event(lifecycle);
            break;
        case CONTROL_DEVICE_MODE_SET:
            control_device_mode_set_ptr mode_set = (control_device_mode_set_ptr)received_msg;   
            on_set_device_mode(mode_set);
            break;
        case CONTROL_DEVICE_MOTION_EXECUTE:
            control_device_motion_execute_ptr motion_execute = (control_device_motion_execute_ptr)received_msg;   
            on_execute_motion(motion_execute);
            break;
        case CONTROL_DEVICE_MOTION_STOP:
            control_device_motion_stop_ptr motion_stop = (control_device_motion_stop_ptr)received_msg;   
            on_stop_motion(motion_stop);
            break;
        default:
            break;
        }
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
    // 处理 BYE 请求
    m_session_state.session_status = SESSION_STATUS_IDLE;
    LOG_INFO("Processing BYE request from server");
    on_call_terminated_by_server();
	
    char *out_msg = NULL;
    size_t out_len = 0;
    int ret = build_200_ok_response(message, &out_msg, &out_len);
    if (ret != 0 || out_msg == NULL || out_len == 0){
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
        reject_info_request(message, 481, "Call/Transaction Does Not Exist");
        free(message);
        return;
    }

    if (strncmp(message->call_id, m_session_state.session_id, sizeof(m_session_state.session_id)) != 0){
        adapter_unlock_sip_mutex();
        reject_info_request(message, 481, "Call/Transaction Does Not Exist");
        free(message);
        return;
    }

    adapter_unlock_sip_mutex();

    if (message->body_length > 0) {

        // 解析 JSON
        void *received_msg = NULL;
        dcp_cmd_type_t cmd_type = parse_dcp_message(message->message_body, &received_msg);

        if (cmd_type == INVALID_CMD || received_msg == NULL) {
            LOG_INFO("Unknown command received in INFO message");
            free(message);
            return;
        }

        switch (cmd_type)
        {
        case EVENT_SYSTEM_NOTIFICATION:
            event_system_notification_ptr notification = (event_system_notification_ptr)received_msg;
            on_server_notify(notification);
            break;    
        case EVENT_DEVICE_LIFECYCLE:
            event_device_lifecycle_ptr lifecycle = (event_device_lifecycle_ptr)received_msg;
            on_server_lifecycle_event(lifecycle);
            break;

        case DATA_AUDIO_INPUT_TEXT:
            data_audio_input_text_ptr audio_input_text = (data_audio_input_text_ptr)received_msg;
            on_server_session_input_text_notify(audio_input_text);
            break;
        case DATA_AUDIO_OUTPUT_TEXT:
            data_audio_output_text_ptr audio_output_text = (data_audio_output_text_ptr)received_msg;
            on_server_session_output_text_notify(audio_output_text);
            break;
         case CONTROL_AUDIO_OUTPUT_STATE:
            control_audio_output_state_ptr audio_output_state = (control_audio_output_state_ptr)received_msg; 
            on_server_session_update_notify(audio_output_state);  
            break;
        case CONTROL_DEVICE_MODE_SET:
            control_device_mode_set_ptr mode_set = (control_device_mode_set_ptr)received_msg;   
            on_set_device_mode(mode_set);
            break;
        case CONTROL_DEVICE_MOTION_EXECUTE:
            control_device_motion_execute_ptr motion_execute = (control_device_motion_execute_ptr)received_msg;   
            on_execute_motion(motion_execute);
            break;
        case CONTROL_DEVICE_MOTION_STOP:
            control_device_motion_stop_ptr motion_stop = (control_device_motion_stop_ptr)received_msg;   
            on_stop_motion(motion_stop);
             break;
        default:
            break;
        }
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

    free(message);
    return;
}

static int power_on_register = 0;
void send_register(register_param_ptr param){

    if(!param){
        LOG_INFO("Invalid register parameters");
        return;
    }
    LOG_INFO("Sending REGISTER request");
    adapter_lock_sip_mutex();
    param->scenario = power_on_register == 0 ? 0 : 1;
    power_on_register = 1; // 只在第一次注册时携带开机注册标志，之后的注册都不携带  
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
            .cseq_num = (int)(m_session_state.seq),
            .register_param = *param
        };
        char* message = NULL;
        size_t message_len = 0;
        sip_ret_t ret = build_register(&register_param, &message,  &message_len);
        if (ret != RET_OK){
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

sip_ret_t init_call(const char* wake_up_word){
    LOG_INFO("Initiating call with wake-up word: %s", wake_up_word ? wake_up_word : "null");
    adapter_lock_sip_mutex();
    int ret = RET_OK;

    do{
        if (m_session_state.session_status > SESSION_STATUS_INVITING){
            ret = RET_ERROR;
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
            .wake_up_word = wake_up_word,
            .support_frame_aggregation = SESSION_SUPPORT_FRAME_AGGREGATION
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
        if (ret != RET_OK){
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


sip_ret_t finish_call(){
    LOG_INFO("Finishing call");
    adapter_lock_sip_mutex();
    sip_ret_t ret = RET_OK;
    do{

        if (m_session_state.session_status != SESSION_STATUS_IN_CALL){
            ret = RET_ERROR;
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
        if (ret == RET_OK){
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

    sip_ret_t ret = RET_ERROR;
    char *out_msg = NULL;
    size_t out_len = 0;

    ret = build_ack(m_session_state.invite_200_ok_resp_message,
                    m_session_state.uid,
                    m_session_state.device_ip,
                    &out_msg,
                    &out_len);
    if (ret != RET_OK || !out_msg) {
        goto cleanup;
    }

    transmit_sip(out_msg);

cleanup:
    if (out_msg) free_sip_message(out_msg);
    return;
}

sip_ret_t send_abort_speaking(event_session_barge_in_ptr barge_in){

    if(!barge_in){
        return RET_ERROR;
    }
    LOG_INFO("Sending abort speaking: reason=%d", barge_in->reason);
    adapter_lock_sip_mutex();
    sip_ret_t ret = RET_OK;
    do{

        if (m_session_state.session_status != SESSION_STATUS_IN_CALL){
            ret = RET_ERROR;
            break;
        }

        if (!m_session_state.invite_200_ok_resp_message){
            ret = RET_ERROR;
            break;
        }

        char* message = NULL;
        size_t message_len = 0;
        ret = build_session_barge_in(
            m_session_state.invite_200_ok_resp_message,
            m_session_state.uid,
            m_session_state.device_ip,
            m_session_state.seq,
            barge_in,
            &message,
            &message_len);
        if (ret != RET_OK){
            ret = RET_ERROR;
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


sip_ret_t send_start_listening(listening_mode_t mode){

    event_audio_input_state_start_t param = {
        .mode = mode
    };

    LOG_INFO("Sending listening start, mode=%d", mode);
    sip_ret_t ret = RET_OK;
    do{
        if (m_session_state.session_status != SESSION_STATUS_IN_CALL){
            ret = RET_ERROR;
            break;
        }

        if (!m_session_state.invite_200_ok_resp_message){
            ret = RET_ERROR;
            break;
        }

        char* message = NULL;
        size_t message_len = 0;
        ret = build_audio_input_state_start(
            m_session_state.invite_200_ok_resp_message,
            m_session_state.uid,
            m_session_state.device_ip,
            m_session_state.seq,
            &param,
            &message,
            &message_len);
        if (ret != RET_OK){
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

sip_ret_t send_stop_listening(audio_input_stop_reason_t reason){

    event_audio_input_state_stop_t param = {reason};
    LOG_INFO("Sending listening stop");
    sip_ret_t ret = RET_OK;
    do{

        if (m_session_state.session_status != SESSION_STATUS_IN_CALL){
            ret = RET_ERROR;
            break;
        }

        if (!m_session_state.invite_200_ok_resp_message){
            ret = RET_ERROR;
            break;
        }

        char* message = NULL;
        size_t message_len = 0;
        ret = build_audio_input_state_stop(
            m_session_state.invite_200_ok_resp_message,
            m_session_state.uid,
            m_session_state.device_ip,
            m_session_state.seq,
            &param,
            &message,
            &message_len);
        if (ret != RET_OK){
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


void handle_received_sip(const char *data, size_t len)
{
    received_sip_message_ptr msg_info = NULL;
    sip_ret_t result = sip_parse_incoming_message(data, len, &msg_info);

    if(result == RET_OK && msg_info != NULL){
        if (!is_response_message(msg_info)) {
            // 设备只会收到如下几种服务器下发的
            if (strcmp(msg_info->method, "INVITE") == 0) {
                // 服务器发起会话
                proc_request_invite(msg_info);
                

            } else if (strcmp(msg_info->method, "MESSAGE") == 0) {
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
    if (m_session_state.session_status == SESSION_STATUS_IN_CALL){
        if ((m_session_state.last_traffic_ms + g_audio_enc_media_param.idle_timeout * 1000) <= ms){
            LOG_INFO("No traffic for %d seconds, assuming call dropped", g_audio_enc_media_param.idle_timeout);
            finish_call();
            on_call_terminated_by_server();
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

void proc_register_task(void *param){
    send_register(&m_register_param);
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
    adapter_start_thread(mqtt_proc_task, "mqtt_proc_task", 1024*8, 16, NULL);
#else
    send_register(&m_register_param);
#endif

    adapter_start_periodic_task(session_checking, 1000, 1024*8, NULL);
    adapter_start_periodic_task(proc_register_task, 60 * 1000, 1024*8, NULL);
}
