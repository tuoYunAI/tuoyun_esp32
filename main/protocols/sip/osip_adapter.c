#include "osipparser2/osip_message.h"
#include "osipparser2/osip_parser.h"
#include "osipparser2/sdp_message.h"
#include "osip_adapter.h"
#include "string.h"

#define ADAPTER_LOG_TAG    "[SIP-ADAPTER]"
#define LOG_LEVEL_ENABLED  LOG_INFO_LEVEL
#include "adapter.h"


#define SIP_VERSION "SIP/2.0"
#define SERVER_HOST "server.lovaiot.com"
#define USER_AGENT  "TUOYUN-AI-TOY/1.0"

// 简单的错误处理宏
#define CHECK_RET(expr) do { int __rc = (expr); if (__rc != 0) { goto fail; } } while (0)




void free_sip_message(char* sip){
    if (sip){
        osip_free(sip);
    }
}
/**
 * 安全地将字符串转换为整数
 */
int safe_str_to_int(const char *s, int *out) {
    if (!s || !out) return 0;
    int errno = 0;
    char *end = NULL;
    long val = strtol(s, &end, 10); // base 10

    // 没有任何字符被转换
    if (end == s) return 0;

    // 额外的非空白字符存在（例如 "123abc"）
    while (*end != '\0') {
        if (*end != ' ' && *end != '\t' && *end != '\n' && *end != '\r')
            return 0;
        end++;
    }

    // 溢出或范围超出 int
    //if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
    //    val > INT_MAX || val < INT_MIN) {
    //    return 0;
    //}

    *out = (int)val;
    return 1;
}

/**
 * 安全地将字符串转换为无符号整数
 */
int safe_str_to_uint(const char *s, unsigned long *out, int base) {
    int errno = 0;
    char *end = NULL;
    unsigned long val = strtoul(s, &end, base); // base 可传 0 自动识别 0x 前缀

    if (end == s) return 0;
    // 检查尾部非空白
    while (*end != '\0') {
        if (*end != ' ' && *end != '\t' && *end != '\n') return 0;
        end++;
    }
    //if (errno == ERANGE || val > UINT_MAX) return false;
    *out = val;
    return 1;
}



// 将十六进制字符串转换为字节数组
// 参数：
//   hex_str:     十六进制字符串（例如 "48656C6C6F" 或 "48:65:6C:6C:6F"）
//   hex_array:   输出的字节数组
//   array_size:  数组的最大大小
// 返回：成功转换的字节数，失败返回 -1
int hex_string_to_array(const char *hex_str, uint8_t *hex_array, size_t array_size)
{
    if (!hex_str || !hex_array || array_size == 0) {
        return -1;
    }

    size_t str_len = strlen(hex_str);
    size_t byte_count = 0;
    size_t i = 0;
    memset(hex_array, 0, array_size); 
    while (i < str_len && byte_count < array_size) {
        // 跳过分隔符（空格、冒号、短横线等）
        while (i < str_len && (hex_str[i] == ' ' || hex_str[i] == ':' || 
                               hex_str[i] == '-' || hex_str[i] == '\t')) {
            i++;
        }

        // 检查是否还有足够的字符
        if (i + 1 >= str_len) {
            break;
        }

        // 获取两个十六进制字符
        char high_char = hex_str[i];
        char low_char = hex_str[i + 1];

        // 验证字符是否为有效的十六进制字符
        if (!isxdigit(high_char) || !isxdigit(low_char)) {
            return -1;
        }

        // 转换十六进制字符为数值
        uint8_t high_nibble = (high_char >= '0' && high_char <= '9') ? 
                              (high_char - '0') : 
                              (toupper(high_char) - 'A' + 10);
        
        uint8_t low_nibble = (low_char >= '0' && low_char <= '9') ? 
                             (low_char - '0') : 
                             (toupper(low_char) - 'A' + 10);

        // 组合成一个字节
        hex_array[byte_count] = (high_nibble << 4) | low_nibble;
        byte_count++;
        i += 2;
    }

    return (int)byte_count;
}

// 反向函数：将字节数组转换为十六进制字符串
int array_to_hex_string(const uint8_t *hex_array, size_t array_len, 
                        char *hex_str, size_t str_size)
{
    if (!hex_array || !hex_str || str_size == 0 || array_len == 0) {
        return -1;
    }

    size_t required_size = array_len * 2 + 1; // +1 for '\0'

    if (str_size < required_size) {
        return -1;
    }

    char *ptr = hex_str;
    for (size_t i = 0; i < array_len; i++) {
        sprintf(ptr, "%02X", hex_array[i]);
        ptr += 2;
    }
    *ptr = '\0';

    return (int)(ptr - hex_str);
}


static inline uint32_t sip_rotl32(uint32_t v, unsigned r) {
    return (v << r) | (v >> (32u - r));
}

static inline uint32_t sip_fnv1a32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static inline uint32_t sip_branch_hash(const char *uid,
                                       const char *device_ip,
                                       uint32_t cseq,
                                       uint32_t ticks)
{
    uint32_t h = 0x9E3779B9u; // golden ratio
    if (uid)       h ^= sip_rotl32(sip_fnv1a32(uid,       (uint32_t)strlen(uid)),  5);
    if (device_ip) h ^= sip_rotl32(sip_fnv1a32(device_ip, (uint32_t)strlen(device_ip)), 11);
    h ^= sip_rotl32(cseq, 17);
    h ^= sip_rotl32(ticks, 3);
    // avalanche
    h ^= h >> 16; h *= 0x7feb352dU;
    h ^= h >> 15; h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

// 生成形如 "z9hG4bK-<8位十六进制>" 的 branch
static inline void sip_generate_branch(char *out,
                                       size_t out_sz,
                                       const char *uid,
                                       const char *device_ip,
                                       uint32_t cseq)
{
    if (!out || out_sz == 0) return;

    uint32_t ticks = (uint32_t)adapter_get_system_ms();

    uint32_t h = sip_branch_hash(uid, device_ip, cseq, ticks);
    // 确保以 RFC3261 魔法串开头
    // 分支长度控制在 64 字节以内（本实现远小于该值）
    (void)snprintf(out, out_sz, "z9hG4bK-%08X", (unsigned)h);
}


// 生成 From 的 tag（仅 token 字符，16位十六进制）
static inline void sip_generate_tag(char *out,
                                    size_t out_sz,
                                    const char *uid,
                                    const char *device_ip,
                                    const char *call_id,
                                    uint32_t cseq)
{
    if (!out || out_sz == 0) return;

    uint32_t ticks = (uint32_t)adapter_get_system_ms();

    uint32_t h1 = sip_branch_hash(uid, device_ip, cseq, ticks);
    uint32_t h2 = h1 ^ (call_id ? sip_rotl32(sip_fnv1a32(call_id, (uint32_t)strlen(call_id)), 7) : 0xA5A5A5A5u);
    (void)snprintf(out, out_sz, "%08X%08X", (unsigned)h1, (unsigned)h2);
}

/**
 * 组装DCP协议的主消息体
 * {
    "version": "2.1",
    "type": "event",
    "name": "audio.input.state",
    "id": "evt-1002",
    "timestamp": 1712141232000,
    "params": {
       ...
    }
}
 */

static long evt_id_counter = 0;
static long gen_evt_id(){
    return ++evt_id_counter;
}  
static long ctrl_id_counter = 0;
static long gen_ctrl_id(){  
    return ++ctrl_id_counter;
}
static long data_id_counter = 0;
static long gen_data_id(){
    return ++data_id_counter;
}

/**
 * DCP基本消息结构生成函数，生成包含 version、type、name、id、timestamp 等基础字段的 JSON 对象
 */
void* build_dcp_base_msg(dcp_cmd_type_t type){
    
    char* name = NULL;
    char* type_str = NULL;
    char id[128] = {0};
    switch (type)
    {
    case EVENT_REGISTER:
        name = DCP_REGISTER;
        type_str = "event";
        sprintf(id, "%s%ld", EVENT_MSG_NAME_TAG,  gen_evt_id());
        break;
    case EVENT_AUDIO_INPUT_STATE:
        type_str = "event";
        name = DCP_AUDIO_INPUT_STATE;
        sprintf(id, "%s%ld", EVENT_MSG_NAME_TAG,  gen_evt_id());
        break;
    case EVENT_SESSION_BARGE_IN:
        name = DCP_SESSION_BARGE_IN;
        type_str = "event";
        sprintf(id, "%s%ld", EVENT_MSG_NAME_TAG,  gen_evt_id());
        break;
    default:
        break;
    }
    if (!name || !type_str){
        LOG_INFO("Unsupported message type: %d", type);
        return NULL;
    }

    void *root = adapter_create_json_object();
    adapter_put_json_string_value(root, "version", DCP_VERSION);
    adapter_put_json_string_value(root, "type", type_str);
    adapter_put_json_string_value(root, "name", name);
    adapter_put_json_string_value(root, "id", id);
    adapter_put_json_object_value(root, "timestamp", adapter_json_object_new_int(adapter_get_system_ms()));
    return root;
}
/**
 * 组装一个 SIP REGISTER 报文并输出为字符串。
 * 参数：
 *    param:    注册参数结构体指针
 * 
 * 输出：
 *    out_msg:  指向已分配的完整报文字符串（用 osip_free 释放）
 *    out_len:  报文长度
 * 
 * 返回：0 成功，非0失败
 * 
 */
sip_ret_t build_register(sip_register_param_ptr param, char **out_msg, size_t *out_len)
{
    if (!param || !param->uid || !param->device_ip || !out_msg || !out_len) return RET_ERROR;

    osip_message_t *msg = NULL;
    char buf[256];

    CHECK_RET(osip_message_init(&msg));

    // 请求行：REGISTER sip:server.lovaiot.com SIP/2.0
    osip_message_set_method(msg, osip_strdup("REGISTER"));
    osip_message_set_version(msg, osip_strdup(SIP_VERSION));
    CHECK_RET(osip_uri_init(&msg->req_uri));
    snprintf(buf, sizeof(buf), "sip:%s", SERVER_HOST);
    CHECK_RET(osip_uri_parse(msg->req_uri, buf));

    // Via: SIP/2.0/MQTT {device-ip};branch=z9hG4bK-123456
    // 说明：oSIP 对协议标记（MQTT）不做强校验，作为 token 接受
    osip_via_t *via = NULL;
    osip_via_init(&via);

    char branch[32];
    sip_generate_branch(branch, sizeof(branch), param->uid, param->device_ip, (uint32_t)param->cseq_num);
    snprintf(buf, sizeof(buf), "SIP/2.0/MQTT %s;branch=%s", param->device_ip, branch);
    CHECK_RET(osip_via_parse(via, buf));
    osip_list_add(&msg->vias, via, -1);

    // Max-Forwards: 70
    CHECK_RET(osip_message_set_header(msg, "Max-Forwards", "70"));

    // From: <sip:{uid}@server.lovaiot.com>;tag=<动态生成>
    CHECK_RET(osip_from_init(&msg->from));
    char tag[33]; // 16 hex + 16 hex + '\0'，我们只用 16（%08X%08X）
    char call_id[36] = {0};
    uint32_t ms = adapter_get_system_ms();
    sprintf(call_id, "%ld@%s", ms, param->device_ip);  
    sip_generate_tag(tag, sizeof(tag), param->uid, param->device_ip, call_id, (uint32_t)param->cseq_num);
    snprintf(buf, sizeof(buf), "<sip:%s@%s>;tag=%s", param->uid, SERVER_HOST, tag);
    CHECK_RET(osip_from_parse(msg->from, buf));

    // To: <sip:{uid}@server.lovaiot.com>
    CHECK_RET(osip_to_init(&msg->to));
    snprintf(buf, sizeof(buf), "<sip:%s@%s>", param->uid, SERVER_HOST);
    CHECK_RET(osip_to_parse(msg->to, buf));

    // Call-ID: reg-1234567890@lovaiot.com
    CHECK_RET(osip_call_id_init(&msg->call_id));
    CHECK_RET(osip_call_id_parse(msg->call_id, call_id));

    // CSeq: 1 REGISTER
    CHECK_RET(osip_cseq_init(&msg->cseq));
    snprintf(buf, sizeof(buf), "%d REGISTER", param->cseq_num);
    CHECK_RET(osip_cseq_parse(msg->cseq, buf));

    // Contact: <sip:{uid}@mqtt:{device-ip}>;expires=3600
    osip_contact_t *ct = NULL;
    osip_contact_init(&ct);
    snprintf(buf, sizeof(buf), "<sip:%s@%s>;expires=%d", param->uid, param->device_ip, param->expires_sec);
    CHECK_RET(osip_contact_parse(ct, buf));
    osip_list_add(&msg->contacts, ct, -1);

    // Expires: 3600
    snprintf(buf, sizeof(buf), "%d", param->expires_sec);
    CHECK_RET(osip_message_set_header(msg, "Expires", buf));

    // User-Agent: AI-Toy/1.0
    CHECK_RET(osip_message_set_header(msg, "User-Agent", USER_AGENT));

// Content-Type: application/sdp
    CHECK_RET(osip_message_set_header(msg, "Content-Type", "application/json"));

    // Content-Length：与实际 body 长度保持一致（无 body 时为 0）
    {
        void *root = build_dcp_base_msg(EVENT_REGISTER);
        void *params = adapter_create_json_object();
        adapter_put_json_object_value(params, "scenario", adapter_json_object_new_boolean(param->register_param.scenario));
        adapter_put_json_object_value(params, "battery", adapter_json_object_new_int(param->register_param.battery));
        void *network = adapter_create_json_object();
        char* type = NULL;
        switch (param->register_param.network.type){
        case WIFI:
            type = "wifi";
            break;
        case MOBILE_4G:
            type = "4g";
            break;
        case ETHERNET:
            type = "ethernet";
            break;
        default:
            type = "unknown";
            break;  
        }
        adapter_put_json_string_value(network, "type", type);
        adapter_put_json_object_value(network, "rssi", adapter_json_object_new_int(param->register_param.network.rssi));
        adapter_put_json_object_value(params, "network", network);
        adapter_put_json_object_value(root, "params", params);
        const char* json_string = adapter_serialize_json_to_string(root);
        CHECK_RET(osip_message_set_body(msg, json_string, strlen(json_string)));
        adapter_delete_json_object(root);


        int body_len = 0;
        if (!osip_list_eol(&msg->bodies, 0)) {
            osip_body_t *body = (osip_body_t *)osip_list_get(&msg->bodies, 0);
            if (body && body->length > 0) {
                body_len = (int)body->length;
            }
        }
        char clen[32]={0};
        snprintf(clen, sizeof(clen) - 1, "%d", body_len);
        CHECK_RET(osip_message_set_content_length(msg, osip_strdup(clen)));
    }
    
    // 序列化为最终字符串
    CHECK_RET(osip_message_to_str(msg, out_msg, out_len));
    osip_message_free(msg);
    return RET_OK;

fail:
    if (msg) osip_message_free(msg);
    return RET_ERROR;
}


static sip_ret_t make_sdp(uplink_sdp_parameter_ptr param, char *dst, size_t dst_sz)
{
    if (!dst || dst_sz == 0 || !param || !param->uid || !param->device_ip || !param->codec) return RET_ERROR;

    uint32_t version = (uint32_t)adapter_get_system_ms();
    char call_id[36] = {0};
    sprintf(call_id, "%ld@%s", version, param->device_ip);  
    char wake_word_part[64] = {0};
    if (param->wake_up_word && param->wake_up_word[0] != '\0') {
        snprintf(wake_word_part, sizeof(wake_word_part), ",wake_up_word=%s", param->wake_up_word);
    }
    int n = snprintf(dst, dst_sz,
        "v=0\r\n"
        "o=%s %s %ld IN IP4 0.0.0.0\r\n"
        "s=AI-Session\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "t=0 0\r\n"
        "m=audio 0 UDP/AI-AUDIO\r\n"
        "a=lovaiot-uplink:codec=%s,frame=%d,sample_rate=%d,channels=%d,mcp=%d%s\r\n"
        "a=lovaiot-downlink:cbr=%d,frame_gap=%d,aggregation=%d,redundant=%d\r\n",
        param->uid, param->session_id, version, 
        param->codec, param->frame_duration_ms, param->sample_rate, param->channels, param->support_mcp ? 1 : 0, wake_word_part,
        param->cbr ? 1 : 0, param->frame_gap, param->support_frame_aggregation ? 1 : 0, param->support_redundant ? 1 : 0    
    );
    return (n > 0 && (size_t)n < dst_sz) ? RET_OK : RET_ERROR;
}

static void parse_downlink_attribute_value(const char *value, downlink_sdp_parameter_ptr param)
{
    if (!value || !param) {
        return;
    }

    char attr_buf[512] = {0};
    strncpy(attr_buf, value, sizeof(attr_buf) - 1);

    char *cursor = attr_buf;
    while (cursor && *cursor != '\0') {
        char *segment = cursor;
        char *comma = strchr(cursor, ',');
        if (comma) {
            *comma = '\0';
            cursor = comma + 1;
        } else {
            cursor = NULL;
        }

        char *eq = strchr(segment, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';

        char *key = segment;
        char *val = eq + 1;
        if (osip_strcasecmp(key, "codec") == 0) {
            strncpy(param->codec, val, sizeof(param->codec) - 1);
            param->codec[sizeof(param->codec) - 1] = '\0';
        } else if (osip_strcasecmp(key, "frame") == 0) {
            param->frame_duration = atoi(val);
        } else if (osip_strcasecmp(key, "sample_rate") == 0) {
            param->sample_rate = atoi(val);
        } else if (osip_strcasecmp(key, "channels") == 0) {
            param->channels = atoi(val);
        } else if (osip_strcasecmp(key, "encryption") == 0) {
            strncpy(param->encryption, val, sizeof(param->encryption) - 1);
            param->encryption[sizeof(param->encryption) - 1] = '\0';
        } else if (osip_strcasecmp(key, "key") == 0) {
            hex_string_to_array(val, param->aes_key, sizeof(param->aes_key));
        } else if (osip_strcasecmp(key, "nonce") == 0) {
            hex_string_to_array(val, param->nonce, sizeof(param->nonce));
        }
    }
}

static void parse_origin_session_id(const char *sdp_buf, downlink_sdp_parameter_ptr param)
{
    if (!sdp_buf || !param) {
        return;
    }

    const char *origin = strstr(sdp_buf, "\no=");
    if (origin) {
        origin += 3;
    } else if (strncmp(sdp_buf, "o=", 2) == 0) {
        origin = sdp_buf + 2;
    } else {
        return;
    }

    while (*origin == ' ') {
        origin++;
    }

    const char *session_id = strchr(origin, ' ');
    if (!session_id) {
        return;
    }
    while (*session_id == ' ') {
        session_id++;
    }
    if (*session_id == '\0' || *session_id == '\r' || *session_id == '\n') {
        return;
    }

    size_t session_id_len = strcspn(session_id, " \r\n");
    if (session_id_len == 0) {
        return;
    }
    if (session_id_len >= sizeof(param->session_id)) {
        session_id_len = sizeof(param->session_id) - 1;
    }

    memcpy(param->session_id, session_id, session_id_len);
    param->session_id[session_id_len] = '\0';
}

sip_ret_t parse_sdp(const char *sdp_buf, downlink_sdp_parameter_ptr param)
{
    if (!sdp_buf || !param) return RET_ERROR;

    sdp_message_t *sdp = NULL;
    CHECK_RET(sdp_message_init(&sdp));

    CHECK_RET(sdp_message_parse(sdp, sdp_buf));
    parse_origin_session_id(sdp_buf, param);
    
    // 连接信息（会话级）
    const char *ip = sdp_message_c_addr_get(sdp, -1, 0);
    if (ip && ip[0] != '\0'){
        strncpy(param->ip, ip, sizeof(param->ip) - 1);
        param->ip[sizeof(param->ip) - 1] = '\0';
    }

    // 媒体部分
    for (int m = 0; ; ++m) {
        char *media = sdp_message_m_media_get(sdp, m);
        if (!media) break;
        const char *port = sdp_message_m_port_get(sdp, m);
        const char *proto = sdp_message_m_proto_get(sdp, m);
        if (port){
            param->port = atoi(port);
        }
        if (proto && proto[0] != '\0') {
            if (osip_strncasecmp(proto, "UDP", 3) == 0) {
                strncpy(param->transport, "udp", sizeof(param->transport) - 1);
            } else if (osip_strncasecmp(proto, "TCP", 3) == 0) {
                strncpy(param->transport, "tcp", sizeof(param->transport) - 1);
            } else {
                strncpy(param->transport, proto, sizeof(param->transport) - 1);
            }
            param->transport[sizeof(param->transport) - 1] = '\0';
        }

        for (int ai = 0; ; ++ai) {
            char *af = sdp_message_a_att_field_get(sdp, m, ai);
            if (!af) break;

            char *av = sdp_message_a_att_value_get(sdp, m, ai);
            if (av && osip_strcasecmp(af, "lovaiot-downlink") == 0) {
                parse_downlink_attribute_value(av, param);
                break;
            }
        }
        
        break; // 仅处理第一个媒体
    }

    sdp_message_free(sdp);
    return RET_OK;

fail:
    if (sdp) sdp_message_free(sdp);
    return RET_ERROR;    
}


sip_ret_t build_invite(sip_invite_param_ptr param, char **out_msg, size_t *out_len)
{
    if (!param || !param->uid || !param->device_ip || !out_msg || !out_len) return RET_ERROR;

    osip_message_t *msg = NULL;
    char buf[350];

    CHECK_RET(osip_message_init(&msg));

    // INVITE sip:server.lovaiot.com SIP/2.0
    osip_message_set_method(msg, osip_strdup("INVITE"));
    osip_message_set_version(msg, osip_strdup(SIP_VERSION));
    CHECK_RET(osip_uri_init(&msg->req_uri));
    snprintf(buf, sizeof(buf), "sip:%s", SERVER_HOST);
    CHECK_RET(osip_uri_parse(msg->req_uri, buf));

    // Via: SIP/2.0/MQTT {device-ip};branch=z9hG4bK-123456
    // 说明：oSIP 对协议标记（MQTT）不做强校验，作为 token 接受
    osip_via_t *via = NULL;
    osip_via_init(&via);

    char branch[32];
    sip_generate_branch(branch, sizeof(branch), param->uid, param->device_ip, (uint32_t)param->cseq_num);
    snprintf(buf, sizeof(buf), "SIP/2.0/MQTT %s;branch=%s", param->device_ip, branch);
    CHECK_RET(osip_via_parse(via, buf));
    osip_list_add(&msg->vias, via, -1);

    // Max-Forwards: 70
    CHECK_RET(osip_message_set_header(msg, "Max-Forwards", "70"));

    uint32_t ms = adapter_get_system_ms();
    char call_id[36] = {0};
    sprintf(call_id, "%ld@%s", ms, param->device_ip);    

    // From: <sip:{uid}@server.lovaiot.com>;tag=<动态生成>
    CHECK_RET(osip_from_init(&msg->from));
    char tag[33]; // 16 hex + 16 hex + '\0'，我们只用 16（%08X%08X）
    sip_generate_tag(tag, sizeof(tag), param->uid, param->device_ip, call_id, (uint32_t)param->cseq_num);
    snprintf(buf, sizeof(buf), "<sip:%s@%s>;tag=%s", param->uid, SERVER_HOST, tag);
    CHECK_RET(osip_from_parse(msg->from, buf));

    // To: <sip:server@server.lovaiot.com>
    CHECK_RET(osip_to_init(&msg->to));
    snprintf(buf, sizeof(buf), "<sip:server@%s>", SERVER_HOST);
    CHECK_RET(osip_to_parse(msg->to, buf));

    // Call-ID: reg-1234567890@lovaiot.com   
    CHECK_RET(osip_call_id_init(&msg->call_id));
    CHECK_RET(osip_call_id_parse(msg->call_id, call_id));

    // CSeq: 1 INVITE
    CHECK_RET(osip_cseq_init(&msg->cseq));
    snprintf(buf, sizeof(buf), "%u INVITE", param->cseq_num);
    CHECK_RET(osip_cseq_parse(msg->cseq, buf));

    // Contact: <sip:{uid}@{device-ip}>
    osip_contact_t *ct = NULL;
    osip_contact_init(&ct);
    snprintf(buf, sizeof(buf), "<sip:%s@%s>", param->uid, param->device_ip);
    CHECK_RET(osip_contact_parse(ct, buf));
    osip_list_add(&msg->contacts, ct, -1);

    // Content-Type: application/sdp
    CHECK_RET(osip_message_set_header(msg, "Content-Type", "application/sdp"));

    // Body：SDP（如果提供则设置）
    if (param->sdp != NULL){
        make_sdp(param->sdp, buf, sizeof(buf));
        size_t sdp_len = strlen(buf);
        CHECK_RET(osip_message_set_body(msg, buf, sdp_len));
    }

    // Content-Length：与实际 body 长度保持一致（无 body 时为 0）
    {
        int body_len = 0;
        if (!osip_list_eol(&msg->bodies, 0)) {
            osip_body_t *body = (osip_body_t *)osip_list_get(&msg->bodies, 0);
            if (body && body->length > 0) {
                body_len = (int)body->length;
            }
        }
        char clen[32];
        snprintf(clen, sizeof(clen), "%d", body_len);
        CHECK_RET(osip_message_set_content_length(msg, osip_strdup(clen)));
    }

    // 序列化为最终字符串
    CHECK_RET(osip_message_to_str(msg, out_msg, out_len));
    osip_message_free(msg);
    return RET_OK;

fail:
    if (msg) osip_message_free(msg);
    return RET_ERROR;
}

/** 
* 组装一个 SIP 200 OK 响应报文并输出为字符串。
* 参数：
*   status_code:     响应码（例如 200）
*   reason_phrase:   响应短语（例如 "OK"）
*   via_header:      Via 头部字符串（例如 "SIP/2.0/UDP 192.0.2.4;branch=z9hG4bK776asdhds"）
*   from_header:     From 头部字符串（例如 "Alice <sip:alice@example.com>;tag=1928301774"）
*   to_header:       To 头部字符串（例如 "Bob <sip:bob@example.com>"）
*   to_tag:          To 标签（例如 "xyz123"，如果为 NULL 则不添加）
*   call_id:         Call-ID（例如 "a84b4c76e66710"）
*   cseq_header:     CSeq 头部字符串（例如 "314159 INVITE"）
*   contact_header:  Contact 头部字符串（例如 "<sip:alice@pc33.example.com>"，可为 NULL）
*   content_type:    Content-Type（例如 "application/sdp"，可为 NULL）
*   message_body:    消息体内容（可为 NULL）
*   body_length:     消息体长度
* 输出：
*   out_msg:         指向已分配的完整响应报文字符串（用 osip_free 释放）
*   out_len:         报文长度
* 返回：0 成功，非0失败
**/
static sip_ret_t build_response(int status_code,
                   const char *reason_phrase,
                   const char *via_header,
                   const char *from_header,
                   const char *to_header,
                   const char *to_tag,
                   const char *call_id,
                   const char *cseq_header,
                   const char *contact_header,
                   const char *content_type,
                   const char *message_body,
                   size_t body_length,
                   char **out_msg,
                   size_t *out_len)
{
    if (!reason_phrase || !via_header || !from_header || !to_header || 
        !call_id || !cseq_header || !out_msg || !out_len) {
        return RET_ERROR;
    }

    osip_message_t *resp = NULL;
    char buf[512];

    CHECK_RET(osip_message_init(&resp));

    // 状态行：SIP/2.0 200 OK
    osip_message_set_version(resp, osip_strdup(SIP_VERSION));
    osip_message_set_status_code(resp, status_code);
    osip_message_set_reason_phrase(resp, osip_strdup(reason_phrase));

    // Via 头部
    osip_via_t *via = NULL;
    osip_via_init(&via);
    CHECK_RET(osip_via_parse(via, via_header));
    osip_list_add(&resp->vias, via, -1);

    // From 头部
    CHECK_RET(osip_from_init(&resp->from));
    CHECK_RET(osip_from_parse(resp->from, from_header));

    // To 头部（可能需要添加 tag）
    CHECK_RET(osip_to_init(&resp->to));
    if (to_tag) {
        // 检查 to_header 是否已经包含 tag
        if (strstr(to_header, ";tag=")) {
            // 已经有 tag，直接使用
            CHECK_RET(osip_to_parse(resp->to, to_header));
        } else {
            // 没有 tag，添加 tag
            snprintf(buf, sizeof(buf), "%s;tag=%s", to_header, to_tag);
            CHECK_RET(osip_to_parse(resp->to, buf));
        }
    } else {
        CHECK_RET(osip_to_parse(resp->to, to_header));
    }

    // Call-ID
    CHECK_RET(osip_call_id_init(&resp->call_id));
    CHECK_RET(osip_call_id_parse(resp->call_id, call_id));

    // CSeq
    CHECK_RET(osip_cseq_init(&resp->cseq));
    CHECK_RET(osip_cseq_parse(resp->cseq, cseq_header));

    // Contact（可选）
    if (contact_header) {
        osip_contact_t *ct = NULL;
        osip_contact_init(&ct);
        CHECK_RET(osip_contact_parse(ct, contact_header));
        osip_list_add(&resp->contacts, ct, -1);
    }

    // Content-Type（可选）
    if (content_type) {
        CHECK_RET(osip_message_set_header(resp, "Content-Type", content_type));
    }

    // 消息体和 Content-Length
    if (message_body && body_length > 0) {
        CHECK_RET(osip_message_set_body(resp, message_body, body_length));
        snprintf(buf, sizeof(buf), "%u", (unsigned)body_length);
        CHECK_RET(osip_message_set_content_length(resp, osip_strdup(buf)));
    } else {
        CHECK_RET(osip_message_set_content_length(resp, osip_strdup("0")));
    }

    // 序列化为最终字符串
    CHECK_RET(osip_message_to_str(resp, out_msg, out_len));
    osip_message_free(resp);
    return RET_OK;

fail:
    if (resp) osip_message_free(resp);
    return RET_ERROR;
}

// 便捷函数：构建简单的 200 OK 响应（无消息体）
sip_ret_t build_200_ok_response(received_sip_message_ptr request, char **out_msg, size_t *out_len)
{
    if (!request || !out_msg || !out_len){
        return RET_ERROR;
    }

    return build_response(200, "OK", request->via_header, request->from_header, request->to_header, NULL,
                         request->call_id_header, request->cseq_header, request->contact_header, NULL, NULL, 0,
                         out_msg, out_len); 
    
}

sip_ret_t build_invite_200_ok_response(received_sip_message_ptr request,
                                       const char *uid,
                                       const char *device_ip,
                                       uplink_sdp_parameter_ptr sdp,
                                       char *out_to_tag,
                                       size_t out_to_tag_size,
                                       char **out_msg,
                                       size_t *out_len)
{
    if (!request || !uid || !device_ip || !out_msg || !out_len || !out_to_tag || out_to_tag_size == 0) {
        return RET_ERROR;
    }

    char to_tag[33] = {0};
    char contact_header[128] = {0};
    char sdp_body[350] = {0};
    const char *content_type = NULL;
    const char *message_body = NULL;
    size_t body_length = 0;

    sip_generate_tag(to_tag, sizeof(to_tag), uid, device_ip, request->call_id, (uint32_t)request->cseq_num);
    snprintf(contact_header, sizeof(contact_header), "<sip:%s@%s>", uid, device_ip);

    if (sdp) {
        if (make_sdp(sdp, sdp_body, sizeof(sdp_body)) != RET_OK) {
            return RET_ERROR;
        }
        content_type = "application/sdp";
        message_body = sdp_body;
        body_length = strlen(sdp_body);
    }

    if (snprintf(out_to_tag, out_to_tag_size, "%s", to_tag) >= (int)out_to_tag_size) {
        return RET_ERROR;
    }

    return build_response(200,
                          "OK",
                          request->via_header,
                          request->from_header,
                          request->to_header,
                          to_tag,
                          request->call_id_header,
                          request->cseq_header,
                          contact_header,
                          content_type,
                          message_body,
                          body_length,
                          out_msg,
                          out_len);
}

sip_ret_t build_invite_error_response(received_sip_message_ptr request,
                                      const char *uid,
                                      const char *device_ip,
                                      int status_code,
                                      const char *reason_phrase,
                                      char **out_msg,
                                      size_t *out_len)
{
    char to_tag[33] = {0};

    if (!request || !uid || !device_ip || !reason_phrase || !out_msg || !out_len) {
        return RET_ERROR;
    }

    sip_generate_tag(to_tag, sizeof(to_tag), uid, device_ip, request->call_id, (uint32_t)request->cseq_num);

    return build_response(status_code,
                          reason_phrase,
                          request->via_header,
                          request->from_header,
                          request->to_header,
                          to_tag,
                          request->call_id_header,
                          request->cseq_header,
                          NULL,
                          NULL,
                          NULL,
                          0,
                          out_msg,
                          out_len);
}



sip_ret_t build_ack(received_sip_message_ptr response, const char *uid, const char *device_ip, char **out_msg, size_t *out_len)
{
    if (!response || !uid || !device_ip || !out_msg || !out_len) {
        return RET_ERROR;
    }

    osip_message_t *ack = NULL;
    char buf[256];

    CHECK_RET(osip_message_init(&ack));

    // 请求行：ACK {request_uri} SIP/2.0
    osip_message_set_method(ack, osip_strdup("ACK"));
    osip_message_set_version(ack, osip_strdup(SIP_VERSION));
    
    char req_uri_str[128] = {0};
    snprintf(req_uri_str, sizeof(req_uri_str), "sip:server@%s", SERVER_HOST);

    CHECK_RET(osip_uri_init(&ack->req_uri));
    CHECK_RET(osip_uri_parse(ack->req_uri, req_uri_str));

    // 2) Via 头（为新的 ACK 事务生成 branch）
    //    协议承载在 MQTT 上，沿用栈中示例格式
    char via_hdr[128] = {0};
    snprintf(via_hdr, sizeof(via_hdr), "SIP/2.0/MQTT %s;branch=z9hG4bK-ack%u",
             device_ip, (unsigned)(adapter_get_system_ms() & 0xFFFF));

    osip_via_t *via = NULL;
    osip_via_init(&via);
    CHECK_RET(osip_via_parse(via, via_hdr));
    osip_list_add(&ack->vias, via, -1);

    // Max-Forwards: 70
    CHECK_RET(osip_message_set_header(ack, "Max-Forwards", "70"));

    // From: 复制原来的 From 头部
    CHECK_RET(osip_from_init(&ack->from));
    CHECK_RET(osip_from_parse(ack->from, response->from_header));

    // To: 复制原来的 To 头部
    CHECK_RET(osip_to_init(&ack->to));
    CHECK_RET(osip_to_parse(ack->to, response->to_header));

    // Call-ID: 复制原来的 Call-ID
    CHECK_RET(osip_call_id_init(&ack->call_id));
    CHECK_RET(osip_call_id_parse(ack->call_id, response->call_id_header));

    // CSeq: 使用原来的序列号，但方法改为 ACK
    CHECK_RET(osip_cseq_init(&ack->cseq));
    snprintf(buf, sizeof(buf), "%d ACK", response->cseq_num);
    CHECK_RET(osip_cseq_parse(ack->cseq, buf));

    // Contact: <sip:{uid}@{device-ip}>
    osip_contact_t *ct = NULL;
    osip_contact_init(&ct);
    snprintf(buf, sizeof(buf), "<sip:%s@%s>", uid, device_ip);
    CHECK_RET(osip_contact_parse(ct, buf));
    osip_list_add(&ack->contacts, ct, -1);

    // User-Agent: AI-Toy/1.0
    CHECK_RET(osip_message_set_header(ack, "User-Agent", USER_AGENT));

    // Content-Length: 0
    CHECK_RET(osip_message_set_content_length(ack, osip_strdup("0")));

    // 序列化为最终字符串
    CHECK_RET(osip_message_to_str(ack, out_msg, out_len));
    osip_message_free(ack);
    return RET_OK;

fail:
    if (ack) osip_message_free(ack);
    return RET_ERROR;
}


static osip_message_t* build_info(received_sip_message_ptr response,
               const char *uid,
               const char *device_ip,
               int cseq_num){
    osip_message_t *msg = NULL;
    char buf[512];
    CHECK_RET(osip_message_init(&msg));

    // 请求行：INFO sip:server@server.lovaiot.com SIP/2.0
    osip_message_set_method(msg, osip_strdup("INFO"));
    osip_message_set_version(msg, osip_strdup(SIP_VERSION));
    CHECK_RET(osip_uri_init(&msg->req_uri));
    snprintf(buf, sizeof(buf), "sip:server@%s", SERVER_HOST);
    CHECK_RET(osip_uri_parse(msg->req_uri, buf));

    // Via: SIP/2.0/UDP {device-ip};branch=z9hG4bK-info001
    osip_via_t *via = NULL;
    osip_via_init(&via);
    char branch[32];
    sip_generate_branch(branch, sizeof(branch), uid, device_ip, (uint32_t)cseq_num);
    snprintf(buf, sizeof(buf), "SIP/2.0/UDP %s;branch=%s", device_ip, branch);
    CHECK_RET(osip_via_parse(via, buf));
    osip_list_add(&msg->vias, via, -1);

    // Max-Forwards: 70
    CHECK_RET(osip_message_set_header(msg, "Max-Forwards", "70"));

    // From: <sip:{uid}@server.lovaiot.com>;tag=abc123
    CHECK_RET(osip_from_init(&msg->from));
    CHECK_RET(osip_from_parse(msg->from, response->from_header));

    // To: <sip:server@server.lovaiot.com>;tag=server999
    CHECK_RET(osip_to_init(&msg->to));
    CHECK_RET(osip_to_parse(msg->to, response->to_header));

    // Call-ID: call-987654323@{device-ip}
    CHECK_RET(osip_call_id_init(&msg->call_id));
    CHECK_RET(osip_call_id_parse(msg->call_id, response->call_id_header));

    // CSeq: 6 INFO
    CHECK_RET(osip_cseq_init(&msg->cseq));
    snprintf(buf, sizeof(buf), "%d INFO", cseq_num);
    CHECK_RET(osip_cseq_parse(msg->cseq, buf));

    // User-Agent: TUOYUN-AI-TOY/1.0
    CHECK_RET(osip_message_set_header(msg, "User-Agent", USER_AGENT));

    // Content-Type: application/json
    CHECK_RET(osip_message_set_header(msg, "Content-Type", "application/json"));

    return msg;
fail:
    if (msg) osip_message_free(msg);
    return NULL;
}


sip_ret_t build_audio_input_state_start(
               received_sip_message_ptr response,
               const char *uid,
               const char *device_ip,
               int cseq_num,
               event_audio_input_state_start_ptr param,
               char **out_msg,
               size_t *out_len)
{
    if (!response || !uid || !device_ip || !param || !out_msg || !out_len) {
        return RET_ERROR;
    }

    osip_message_t *msg = build_info(response, uid, device_ip, cseq_num);

    // Body: JSON 内容
    void *root = build_dcp_base_msg(EVENT_AUDIO_INPUT_STATE);
    void* params = adapter_create_json_object();
    adapter_put_json_object_value(root, "params", params);
    adapter_put_json_string_value(params, "state", "on");

    char mode_str[16] = {0};
    switch(param->mode){
        case LISTENING_MODE_AUTO_STOP:
            strncpy(mode_str, "auto", sizeof(mode_str)-1);
            break;
        case LISTENING_MODE_REALTIME:
            strncpy(mode_str, "realtime", sizeof(mode_str)-1);
            break;
        default:
            strncpy(mode_str, "manual", sizeof(mode_str)-1);
            break;
    }
    adapter_put_json_string_value(params, "mode", mode_str);
    
    
    char* json_string = adapter_serialize_json_to_string(root);
    if (!json_string) {
        adapter_delete_json_object(root);
        goto fail;
    }

    int json_len = (int)strlen(json_string);
    CHECK_RET(osip_message_set_body(msg, json_string, (size_t)json_len));

    // Content-Length: 自动计算设置
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "%d", json_len);
    CHECK_RET(osip_message_set_content_length(msg, buf));

    // 序列化为最终字符串
    CHECK_RET(osip_message_to_str(msg, out_msg, out_len));
    osip_message_free(msg);
    adapter_delete_json_object(root);
    return RET_OK;

fail:
    if (msg) osip_message_free(msg);
    return RET_ERROR;
}


sip_ret_t build_audio_input_state_stop(
               received_sip_message_ptr response,
               const char *uid,
               const char *device_ip,
               int cseq_num,
               event_audio_input_state_stop_ptr param,
               char **out_msg,
               size_t *out_len)
{
    if (!response || !uid || !device_ip || !param || !out_msg || !out_len) {
        return RET_ERROR;
    }

    osip_message_t *msg = build_info(response, uid, device_ip, cseq_num);
    // Body: JSON 内容
    void *root = build_dcp_base_msg(EVENT_AUDIO_INPUT_STATE);
    void* params = adapter_create_json_object();
    adapter_put_json_object_value(root, "params", params);
    adapter_put_json_string_value(params, "state", "off");
    if (param->reason == AUDIO_INPUT_STOP_REASON_VAD) {
        adapter_put_json_string_value(params, "reason", "vad");
    } else if (param->reason == AUDIO_INPUT_STOP_REASON_MANUAL) {
        adapter_put_json_string_value(params, "reason", "manual");
    } else if (param->reason == AUDIO_INPUT_STOP_REASON_ERROR) {
        adapter_put_json_string_value(params, "reason", "error");
    } else {
        adapter_put_json_string_value(params, "reason", "unknown");
    }
    
    
    char* json_string = adapter_serialize_json_to_string(root);
    if (!json_string) {
        adapter_delete_json_object(root);
        goto fail;
    }

    int json_len = (int)strlen(json_string);
    CHECK_RET(osip_message_set_body(msg, json_string, (size_t)json_len));

    // Content-Length: 自动计算设置
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "%d", json_len);
    CHECK_RET(osip_message_set_content_length(msg, buf));

    // 序列化为最终字符串
    CHECK_RET(osip_message_to_str(msg, out_msg, out_len));
    osip_message_free(msg);
    adapter_delete_json_object(root);
    return RET_OK;

fail:
    if (msg) osip_message_free(msg);
    return RET_ERROR;
}


sip_ret_t build_session_barge_in(
               received_sip_message_ptr response,
               const char *uid,
               const char *device_ip,
               int cseq_num,
               event_session_barge_in_ptr param,
               char **out_msg,
               size_t *out_len){

    if (!response || !uid || !device_ip || !param || !out_msg || !out_len) {
        return RET_ERROR;
    }

    osip_message_t *msg = build_info(response, uid, device_ip, cseq_num);
    // Body: JSON 内容
    void *root = build_dcp_base_msg(EVENT_SESSION_BARGE_IN);
    void* params = adapter_create_json_object();
    adapter_put_json_object_value(root, "params", params);
    adapter_put_json_string_value(params, "state", "off");
    if (param->text[0] != '\0') {
        adapter_put_json_string_value(params, "text", param->text);
    } 

    if (param->reason == BARGE_IN_REASON_WAKE_WORD_DETECTED) {
        adapter_put_json_string_value(params, "reason", "user_speech");
    } else  {
        adapter_put_json_string_value(params, "reason", "none");
    } 
    
    
    char* json_string = adapter_serialize_json_to_string(root);
    if (!json_string) {
        adapter_delete_json_object(root);
        goto fail;
    }

    int json_len = (int)strlen(json_string);
    CHECK_RET(osip_message_set_body(msg, json_string, (size_t)json_len));

    // Content-Length: 自动计算设置
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "%d", json_len);
    CHECK_RET(osip_message_set_content_length(msg, buf));

    // 序列化为最终字符串
    CHECK_RET(osip_message_to_str(msg, out_msg, out_len));
    osip_message_free(msg);
    adapter_delete_json_object(root);
    return RET_OK;

fail:
    if (msg) osip_message_free(msg);
    return RET_ERROR;   

}

sip_ret_t build_bye(received_sip_message_ptr invite, char* uid, char* device_ip, int cseq_num, char** out_msg, size_t* out_len){

    if (!invite || !uid || !device_ip || !out_msg || !out_len) return -1;

    int ret = RET_ERROR;
    osip_message_t *bye = NULL;

    char req_uri_str[128] = {0};
    snprintf(req_uri_str, sizeof(req_uri_str), "sip:server@%s", SERVER_HOST);

    // 开始组装 BYE
    CHECK_RET(osip_message_init(&bye));

    // 请求行：BYE {request-uri} SIP/2.0
    osip_message_set_method(bye, osip_strdup("BYE"));
    osip_message_set_version(bye, osip_strdup(SIP_VERSION));
    CHECK_RET(osip_uri_init(&bye->req_uri));
    CHECK_RET(osip_uri_parse(bye->req_uri, req_uri_str));

    // Via：生成新的 branch；承载保持 MQTT（若有 device_ip 则带上）
    {
        char via_buf[192];
        char branch[32];
        // 分支基于 uid/device_ip/cseq 生成；缺省时传 NULL/0 也可
        sip_generate_branch(branch, sizeof(branch),
                            uid ? uid : "uid",
                            device_ip ? device_ip : "0.0.0.0",
                            (uint32_t)invite->cseq_num);

        if (device_ip && device_ip[0] != '\0') {
            snprintf(via_buf, sizeof(via_buf), "SIP/2.0/MQTT %s;branch=%s", device_ip, branch);
        } else {
            // 没设备 IP 也构造最小 Via
            snprintf(via_buf, sizeof(via_buf), "SIP/2.0/MQTT;branch=%s", branch);
        }

        osip_via_t *via = NULL;
        osip_via_init(&via);
        CHECK_RET(osip_via_parse(via, via_buf));
        osip_list_add(&bye->vias, via, -1);
    }

    // Max-Forwards
    CHECK_RET(osip_message_set_header(bye, "Max-Forwards", "70"));

    // From / To：直接沿用 INVITE（保留已有 tag；若 To 无 tag，说明对端未应答 2xx，依旧发送 BYE 也可）
    CHECK_RET(osip_from_init(&bye->from));
    CHECK_RET(osip_from_parse(bye->from, invite->from_header));
    CHECK_RET(osip_to_init(&bye->to));
    CHECK_RET(osip_to_parse(bye->to, invite->to_header));

    // Call-ID
    CHECK_RET(osip_call_id_init(&bye->call_id));
    CHECK_RET(osip_call_id_parse(bye->call_id, invite->call_id_header));

    // CSeq: <num> BYE
    {
        char cseq_buf[32];
        CHECK_RET(osip_cseq_init(&bye->cseq));
        snprintf(cseq_buf, sizeof(cseq_buf), "%u BYE", cseq_num);
        CHECK_RET(osip_cseq_parse(bye->cseq, cseq_buf));
    }

    // Contact（可选）：沿用本端 INVITE 的 Contact
    osip_contact_t *ct = NULL;
    osip_contact_init(&ct);
    int pr = osip_contact_parse(ct, invite->contact_header);
    if (pr == OSIP_SUCCESS) {
        osip_list_add(&bye->contacts, ct, -1);
    } else {
        osip_contact_free(ct);
    }

    // User-Agent
    CHECK_RET(osip_message_set_header(bye, "User-Agent", USER_AGENT));

    // Content-Length: 0（BYE 无消息体）
    CHECK_RET(osip_message_set_content_length(bye, osip_strdup("0")));

    // 序列化
    CHECK_RET(osip_message_to_str(bye, out_msg, out_len));
    osip_message_free(bye);
    bye = NULL;
    ret = RET_OK;

cleanup:
    if (bye) osip_message_free(bye);
    return ret;

fail:
    ret = RET_ERROR;
    goto cleanup;
}


static void *alloc_zeroed(size_t size)
{
    void *ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

static system_notification_level_t parse_system_notification_level_value(const char *level)
{
    if (!level || level[0] == '\0') {
        return _INFO_;
    }
    if (strcmp(level, "warning") == 0) {
        return _WARNING_;
    }
    if (strcmp(level, "critical") == 0) {
        return _CRITICAL_;
    }
    return _INFO_;
}

static subscription_status_t parse_subscription_status_value(const char *status)
{
    if (!status || status[0] == '\0') {
        return ACTIVE;
    }
    if (strcmp(status, "expired") == 0) {
        return EXIPIRED;
    }
    if (strcmp(status, "trial") == 0) {
        return TRIAL;
    }
    if (strcmp(status, "blocked") == 0) {
        return BLOCKED;
    }
    return ACTIVE;
}

static device_working_mode_t parse_device_mode_value(const char *mode)
{
    if (!mode || mode[0] == '\0') {
        return EXPLANATION;
    }
    if (strcmp(mode, "interaction") == 0) {
        return INTERACTION;
    }
    if (strcmp(mode, "duo") == 0) {
        return DUO;
    }
    if (strcmp(mode, "human_agent") == 0) {
        return HUMAN_AGENT;
    }
    return EXPLANATION;
}

static device_motion_t parse_device_motion_value(const char *action)
{
    if (!action || action[0] == '\0') {
        return IDLE;
    }
    if (strcmp(action, "nod") == 0) {
        return NOD;
    }
    if (strcmp(action, "shake_head") == 0) {
        return SHAKE_HEAD;
    }
    if (strcmp(action, "dance") == 0) {
        return DANCE;
    }
    if (strcmp(action, "wave") == 0) {
        return WAVE;
    }
    if (strcmp(action, "emotion") == 0) {
        return EMOTION;
    }
    if (strcmp(action, "custom") == 0) {
        return CUSTOM;
    }
    if (strcmp(action, "idle") == 0) {
        return IDLE;
    }
    return IDLE;
}

static device_motion_priority_t parse_device_motion_priority_value(const char *priority)
{
    if (!priority || priority[0] == '\0') {
        return _NORMAL_;
    }
    if (strcmp(priority, "low") == 0) {
        return _LOW_;
    }
    if (strcmp(priority, "high") == 0) {
        return _HIGH_;
    }
    if (strcmp(priority, "interrupt") == 0) {
        return _INTERRUPT_;
    }
    return _NORMAL_;
}

static device_motion_stop_scope_t parse_device_motion_stop_scope_value(const char *scope)
{
    if (!scope || scope[0] == '\0') {
        return ALL;
    }
    if (strcmp(scope, "current") == 0) {
        return CURRENT;
    }
    if (strcmp(scope, "type") == 0) {
        return TYPE;
    }
    return ALL;
}

static float parse_float_value(void *obj, const char *key, float default_value)
{
    const char *value = adapter_get_json_string_value(obj, key);
    if (!value || value[0] == '\0') {
        return default_value;
    }

    char *end = NULL;
    float parsed = strtof(value, &end);
    if (end == value) {
        return default_value;
    }
    return parsed;
}


dcp_cmd_type_t parse_dcp_message(char* data, void** out_param){
    dcp_cmd_type_t cmd_type = INVALID_CMD;
    void *parse = NULL;
    void *params = NULL;
    const char *name = NULL;

    if (!data || data[0] == '\0' || !out_param) {
        return INVALID_CMD;
    }

    *out_param = NULL;
    parse = adapter_parse_json_string(data);
    if (!parse) {
        return INVALID_CMD;
    }

    name = adapter_get_json_string_value(parse, "name");
    if (!name || name[0] == '\0') {
        goto cleanup;
    }
    params = adapter_get_json_node_value(parse, "params");

    if (strcmp(name, DCP_AUDIO_INPUT_TEXT) == 0) {
        data_audio_input_text_ptr text_param = alloc_zeroed(sizeof(data_audio_input_text_t));
        const char *text = params ? adapter_get_json_string_value(params, "text") : NULL;
        if (!text_param) {
            goto cleanup;
        }
        if (text) {
            strncpy(text_param->text, text, sizeof(text_param->text) - 1);
        }
        *out_param = text_param;
        cmd_type = DATA_AUDIO_INPUT_TEXT;
    } else if (strcmp(name, DCP_AUDIO_OUTPUT_START) == 0) {
        control_audio_output_state_ptr output_state = alloc_zeroed(sizeof(control_audio_output_state_t));
        if (!output_state) {
            goto cleanup;
        }
        output_state->state = ON;
        *out_param = output_state;
        cmd_type = CONTROL_AUDIO_OUTPUT_STATE;
    } else if (strcmp(name, DCP_AUDIO_OUTPUT_TEXT) == 0) {
        data_audio_output_text_ptr output_text = alloc_zeroed(sizeof(data_audio_output_text_t));
        const char *text = params ? adapter_get_json_string_value(params, "text") : NULL;
        const char *emotion = params ? adapter_get_json_string_value(params, "emotion") : NULL;
        if (!output_text) {
            goto cleanup;
        }
        if (text) {
            strncpy(output_text->text, text, sizeof(output_text->text) - 1);
        }
        if (emotion) {
            strncpy(output_text->emotion, emotion, sizeof(output_text->emotion) - 1);
        }
        *out_param = output_text;
        cmd_type = DATA_AUDIO_OUTPUT_TEXT;
    } else if (strcmp(name, DCP_AUDIO_OUTPUT_STOP) == 0) {
        control_audio_output_state_ptr output_state = alloc_zeroed(sizeof(control_audio_output_state_t));
        if (!output_state) {
            goto cleanup;
        }
        output_state->state = OFF;
        *out_param = output_state;
        cmd_type = CONTROL_AUDIO_OUTPUT_STATE;
    } else if (strcmp(name, DCP_SYSTEM_NOTIFICATION) == 0) {
        event_system_notification_ptr notification = alloc_zeroed(sizeof(event_system_notification_t));
        const char *level = params ? adapter_get_json_string_value(params, "level") : NULL;
        const char *emotion = params ? adapter_get_json_string_value(params, "emotion") : NULL;
        const char *message = params ? adapter_get_json_string_value(params, "message") : NULL;
        if (!notification) {
            goto cleanup;
        }
        notification->level = parse_system_notification_level_value(level);
        if (emotion) {
            strncpy(notification->emotion, emotion, sizeof(notification->emotion) - 1);
        }
        if (message) {
            strncpy(notification->message, message, sizeof(notification->message) - 1);
        }
        *out_param = notification;
        cmd_type = EVENT_SYSTEM_NOTIFICATION;
    } else if (strcmp(name, DCP_DEVICE_LIFECYCLE) == 0) {
        event_device_lifecycle_ptr lifecycle = alloc_zeroed(sizeof(event_device_lifecycle_t));
        void *capabilities = params ? adapter_get_json_node_value(params, "capabilities") : NULL;
        const char *subscription = params ? adapter_get_json_string_value(params, "subscription") : NULL;
        if (!lifecycle) {
            goto cleanup;
        }
        lifecycle->activated = params ? adapter_get_json_int_value(params, "activated", 0) : 0;
        if (params && lifecycle->activated == 0 && adapter_get_json_boolean_value(params, "activated", false)) {
            lifecycle->activated = 1;
        }
        lifecycle->subscription = parse_subscription_status_value(subscription);
        if (capabilities) {
            lifecycle->capabilities.chat = adapter_get_json_int_value(capabilities, "chat", 0);
            lifecycle->capabilities.vision = adapter_get_json_int_value(capabilities, "vision", 0);
        }
        *out_param = lifecycle;
        cmd_type = EVENT_DEVICE_LIFECYCLE;
    } else if (strcmp(name, DCP_DEVICE_MODE_SET) == 0) {
        control_device_mode_set_ptr mode_set = alloc_zeroed(sizeof(control_device_mode_set_t));
        const char *mode = params ? adapter_get_json_string_value(params, "mode") : NULL;
        if (!mode_set) {
            goto cleanup;
        }
        mode_set->mode = parse_device_mode_value(mode);
        *out_param = mode_set;
        cmd_type = CONTROL_DEVICE_MODE_SET;
    } else if (strcmp(name, DCP_DEVICE_MOTION_EXECUTE) == 0) {
        control_device_motion_execute_ptr motion_execute = alloc_zeroed(sizeof(control_device_motion_execute_t));
        const char *action = params ? adapter_get_json_string_value(params, "action") : NULL;
        const char *priority = params ? adapter_get_json_string_value(params, "priority") : NULL;
        if (!motion_execute) {
            goto cleanup;
        }
        motion_execute->action = parse_device_motion_value(action);
        motion_execute->priority = parse_device_motion_priority_value(priority);
        motion_execute->repeat = params ? adapter_get_json_int_value(params, "repeat", 0) : 0;
        motion_execute->speed = params ? parse_float_value(params, "speed", 1.0f) : 1.0f;
        *out_param = motion_execute;
        cmd_type = CONTROL_DEVICE_MOTION_EXECUTE;
    } else if (strcmp(name, DCP_DEVICE_MOTION_STOP) == 0) {
        control_device_motion_stop_ptr motion_stop = alloc_zeroed(sizeof(control_device_motion_stop_t));
        const char *scope = params ? adapter_get_json_string_value(params, "scope") : NULL;
        const char *type = params ? adapter_get_json_string_value(params, "type") : NULL;
        if (!motion_stop) {
            goto cleanup;
        }
        motion_stop->scope = parse_device_motion_stop_scope_value(scope);
        motion_stop->type = parse_device_motion_value(type);
        *out_param = motion_stop;
        cmd_type = CONTROL_DEVICE_MOTION_STOP;
    }

cleanup:
    if (parse) {
        adapter_delete_json_object(parse);
    }
    if (cmd_type == INVALID_CMD && *out_param) {
        free(*out_param);
        *out_param = NULL;
    }
    return cmd_type;
}



// 收到 SIP 消息时的第一步处理函数
// 参数：
//   raw_msg:     原始消息缓冲区
//   msg_len:     消息长度
//   msg_info:    输出解析后的消息信息（可为 NULL）
// 返回：解析结果
sip_ret_t sip_parse_incoming_message(const char *raw_msg,  size_t msg_len, received_sip_message_ptr *msg_info)
{
    if (!raw_msg || msg_len == 0 || !msg_info) {
        return RET_ERROR;
    }

    // 初始化 osip 消息结构
    osip_message_t* sip = NULL;
    if (0 != osip_message_init(&sip)) {
        return RET_ERROR;
    }

    // 解析 SIP 消息
    int rc = osip_message_parse(sip, raw_msg, msg_len);
    if (rc != 0) {
        goto cleanup;
    }

    //log_info("SIP message parsed successfully\n");

    // 基本验证
    if (!sip->call_id || !sip->call_id->number) {
        goto cleanup;
    }

    if (!sip->cseq || !sip->cseq->number || !sip->cseq->method) {
        goto cleanup;
    }

    if (!sip->from || !sip->to) {
        goto cleanup;
    }

    size_t body_length = 0;
    char *body_content = NULL;

    // 消息体
    osip_body_t *body = NULL;
    if (!osip_list_eol(&sip->bodies, 0)) {
        body = (osip_body_t *)osip_list_get(&sip->bodies, 0);
        if (body && body->body) {
            osip_body_to_str(body, &body_content, &body_length);
           
        }
    }

    if (body_content && body_length > 0) {
        /**
         * 长度预留一个'\0'
         */
        *msg_info = malloc(body_length + 1 + sizeof(received_sip_message_t)); 
        if (!*msg_info) {
            goto cleanup;
        }
        memset(*msg_info, 0, sizeof(received_sip_message_t) + body_length + 1);
        
        (*msg_info)->body_length = body_length;
        strncpy((*msg_info)->message_body, body_content, body_length);
        osip_free(body_content);
    
    } else {
        *msg_info = malloc(sizeof(received_sip_message_t));
        if (!*msg_info) {
            goto cleanup;
        }
        memset(*msg_info, 0, sizeof(received_sip_message_t));
    }
    received_sip_message_ptr message = *msg_info;
    strncpy(message->method, sip->cseq->method, sizeof(message->method) - 1);
    message->status_code = sip->status_code;
    strncpy(message->reason_phrase,  sip->reason_phrase ? sip->reason_phrase : "", sizeof(message->reason_phrase) - 1);  
    
    
    // From 信息
    if (sip->from) {
        char *from_str = NULL;
        
        //strncpy(message->from_header, "<sip:5442993809231380480@server.lovaiot.com>;tag=srvtg1763532272424", sizeof(message->from_header) - 1);
        if (OSIP_SUCCESS != osip_from_to_str(sip->from, &from_str) || !from_str) {
            goto cleanup;
        }
        strncpy(message->from_header, from_str, sizeof(message->from_header) - 1);
        osip_free(from_str);
    }

    if (sip->to){
        char *to_str = NULL;
        if (OSIP_SUCCESS != osip_to_to_str(sip->to, &to_str)){
            goto cleanup;
        }
        strncpy(message->to_header, to_str, sizeof(message->to_header) - 1);
        osip_free(to_str);
    }

    if (!osip_list_eol(&sip->vias, 0)) {
        osip_via_t *via = (osip_via_t *)osip_list_get(&sip->vias, 0);
        char *via_header = NULL;
        if (OSIP_SUCCESS != osip_via_to_str(via, &via_header)) {
            goto cleanup;
        }
        strncpy(message->via_header, via_header, sizeof(message->via_header) - 1);
        osip_free(via_header);
    }
    
    if (sip->call_id){
        char* call_id = NULL;
        if (OSIP_SUCCESS != osip_call_id_to_str(sip->call_id, &call_id)){
            goto cleanup;
        }
        strncpy(message->call_id_header, call_id, sizeof(message->call_id_header) - 1);
        osip_free(call_id);

        strncpy(message->call_id, sip->call_id->number, sizeof(message->call_id) - 1);        
    }

    if (sip->cseq){
        char* cseq_header = NULL;
        if (OSIP_SUCCESS != osip_cseq_to_str(sip->cseq, &cseq_header)){
            goto cleanup;
        }
        strncpy(message->cseq_header, cseq_header, sizeof(message->cseq_header) - 1);
        osip_free(cseq_header);

        safe_str_to_int(sip->cseq->number, &message->cseq_num);
    }

    if (!osip_list_eol(&sip->contacts, 0)) {
        osip_contact_t *ct = (osip_contact_t *)osip_list_get(&sip->contacts, 0);
        if (ct) {
            char *contact_header = NULL;
            osip_contact_to_str(ct, &contact_header);
            strncpy(message->contact_header, contact_header, sizeof(message->contact_header) - 1);
            osip_free(contact_header);
        }
    }

    osip_content_type_t *ct = osip_message_get_content_type(sip);
    if (ct && ct->type) {
        strncpy(message->content_type, ct->type, sizeof(message->content_type) - 1);
    }

    // 获取名为 "x-reason-code" 的头部，pos=0 表示获取第一个匹配项
    message->x_reason_code = 0; // 或其他默认值
    osip_header_t *reason_header = NULL;
    int ret = osip_message_header_get_byname(sip, "x-reason-code", 0, &reason_header);
    
    if (ret == 0 && reason_header != NULL) {
        if (reason_header->hvalue != NULL) {            
            // 如果需要转为整数
            message->x_reason_code = atoi(reason_header->hvalue);
        }
    } 
    
    return RET_OK;

cleanup:
    if (sip) {
        osip_message_free(sip);
    }
    if (*msg_info) {
        free(*msg_info);
        *msg_info = NULL;
    }
    return RET_ERROR;
}

void init_sip(void) {
    parser_init();
}


int is_response_message(received_sip_message_ptr msg){
    if (!msg) return 0;
    return (msg->status_code != 0);
}

int is_response_ok(received_sip_message_ptr msg){
    if (!msg) return 0;
    return (msg->status_code == 200);
}



received_sip_message_ptr build_dialog_anchor_from_invite_request(received_sip_message_ptr request, const char *to_tag, const char *uid, const char *device_ip)
{
    static const char to_tag_suffix[] = ";tag=";

    if (!request || !to_tag || to_tag[0] == '\0') {
        return NULL;
    }

    received_sip_message_ptr dialog = calloc(1, sizeof(received_sip_message_t));
    if (!dialog) {
        return NULL;
    }

    strncpy(dialog->method, request->method, sizeof(dialog->method) - 1);
    dialog->status_code = 200;
    strncpy(dialog->reason_phrase, "OK", sizeof(dialog->reason_phrase) - 1);
    dialog->cseq_num = request->cseq_num;
    strncpy(dialog->call_id, request->call_id, sizeof(dialog->call_id) - 1);
    strncpy(dialog->call_id_header, request->call_id_header, sizeof(dialog->call_id_header) - 1);
    strncpy(dialog->cseq_header, request->cseq_header, sizeof(dialog->cseq_header) - 1);
    strncpy(dialog->to_header, request->from_header, sizeof(dialog->to_header) - 1);
    {
        int written = snprintf(dialog->contact_header, sizeof(dialog->contact_header), "<sip:%s@%s>", uid, device_ip);
        if (written < 0 || written >= (int)sizeof(dialog->contact_header)) {
            free(dialog);
            return NULL;
        }
    }

    if (strstr(request->to_header, ";tag=") != NULL) {
        strncpy(dialog->from_header, request->to_header, sizeof(dialog->from_header) - 1);
    } else {
        size_t to_header_len = strnlen(request->to_header, sizeof(dialog->from_header));
        size_t to_tag_len = strlen(to_tag);
        size_t suffix_len = sizeof(to_tag_suffix) - 1;

        if (to_header_len >= sizeof(dialog->from_header) ||
            to_header_len + suffix_len + to_tag_len >= sizeof(dialog->from_header)) {
            free(dialog);
            return NULL;
        }

        memcpy(dialog->from_header, request->to_header, to_header_len);
        memcpy(dialog->from_header + to_header_len, to_tag_suffix, suffix_len);
        memcpy(dialog->from_header + to_header_len + suffix_len, to_tag, to_tag_len + 1);
    }

    return dialog;
}