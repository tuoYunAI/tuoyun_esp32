#ifndef __OSIP_ADAPTER_H__
#define __OSIP_ADAPTER_H__

#include "adapter.h"

/**
 * OPUS audio frame duration of this device, unit: ms
 */
#define OPUS_FRAME_DURATION_MS 60

/**

 * Device control event name in SIP message payload
 */
#define DEVICE_CTRL_EVENT_ALERT       "alert"
#define DEVICE_CTRL_EVENT_ACTIVATE    "activate"
#define DEVICE_CTRL_EVENT_EXPIRE      "expire"
#define DEVICE_CTRL_EVENT_STT         "stt"
#define DEVICE_CTRL_EVENT_LISTEN      "listen"
#define DEVICE_CTRL_EVENT_SPEAKER     "speaker"


#define WORKING_CMD_START        "start"
#define WORKING_CMD_STOP         "stop"
#define WORKING_CMD_TEXT         "text"
#define WORKING_CMD_SENTENCE_START   "sentence_start"



/**
 * 设备发起SIP REGISTER参数结构体
 * 结构体中的指针指向的字符串在build_register调用返回后就不再使用, 可以释放
 */
typedef struct sip_register_param{
  /**
   * 设备的UID, 从服务器中获取
   */
  char *uid;
  /**
   * 设备的IP地址
   */
  char *device_ip;
  /**
   * 注册有效时长, 单位秒
   */
  int expires_sec;
  /**
   * 序号
   */
  int cseq_num;
}sip_register_param_t, *sip_register_param_ptr;

/**
 * 设备发起SIP INVITE时SDP参数结构体
 * 结构体中的指针指向的字符串在make_sdp调用返回后就不再使用, 可以释放
 */
typedef struct uplink_sdp_parameter {
  /**
   * 设备的UID, 从服务器中获取
   */
  char *uid;
  /**
   * 设备的IP地址
   */
  char *device_ip;
  /**
   * 音频格式 一般为 "PCM" 或 "OPUS"
   */
  char *codec;
  /**
   * 采样率 如 16000
   */
  int sample_rate;
  /**
   * 通道数 如 1
   */
  int channels;
  /**
   * 帧长度 毫秒 如 60
   */
  int frame_duration_ms;
  /**
   * 是否使用CBR编码 0表示不使用，1表示使用
   */
  int cbr;

  /**
   * 服务端下发语音的帧间隔 毫秒 10ms
   */
  int frame_gap;
  
  /**
   * 是否支持MCP协议 0表示不支持，1表示支持
   */
  int support_mcp;
  /**
   * 唤醒词 可选参数, 如果没有唤醒词则传NULL
   */
  char *wake_up_word;
} uplink_sdp_parameter_t, *uplink_sdp_parameter_ptr;


/**
 * 
 * 设备接收SIP INVITE响应时SDP参数结构体
 * 
 */
typedef struct downlink_sdp_parameter{
  /**
   * 服务端的IP地址
   */
  char ip[16];
  /**
   * 服务端的端口号
   */
  int port;
  /**
   * 音频格式 一般为 "PCM" 或 "OPUS"
   */
  char codec[16];
  /**
   * 音频传输协议 例如 "udp"
   */
  char transport[8]; 
  /**
   * 采样率 如 16000
   */
  int sample_rate; 
  /**
   * 通道数 如 1
   */
  int channels;
  /**
   * 帧长度 毫秒 如 60
   */
  int frame_duration; 
  /**
   * 加密方式 例如 "aes-128-cbc"
   */
  char encryption[16]; 
  /**
   * AES 初始向量/nonce
   */
  uint8_t nonce[16];   
  /**
   * AES 128位密钥
   */ 
  uint8_t aes_key[16];
}downlink_sdp_parameter_t, *downlink_sdp_parameter_ptr;


/**
 * 设备发起Invite时的参数
 * 
 */
typedef struct sip_invite_param{
  /**
   * 设备的UID, 从服务器中获取
   */
  char* uid;
  /** 
   * 设备的IP地址
   */
  char* device_ip;
  /**
   * CSeq序列号
   */
  int cseq_num;
  /**
   * SDP内容
   */
  uplink_sdp_parameter_ptr sdp;
}sip_invite_param_t, *sip_invite_param_ptr;

/**
 * 接收到的SIP消息结构体
 */
typedef struct received_sip_message{
  /**
   * 消息方法 如 INVITE, REGISTER 等
   */
  char method[16];
  /**
   * 状态码 如 200, 404 等, 如果是request消息则该字段为0
   */
  int status_code;

  /**
   * 扩展状态码 如1000以上的自定义状态码, 如果是request消息则该字段为0
   */
  int x_reason_code;

  /**
   * 原因短语 如 OK, Not Found 等, 如果是request消息则该字段为空字符串
   */
  char reason_phrase[64];

  /**
   * CSeq序号
   */
  int cseq_num;

  /**
   * Call-ID 字符串
   */
  char call_id[64];

  /**
   * From 头部字符串（例如 "Alice <sip:alice@example.com>;tag=1928301774"）
   */
  char from_header[128];
  /**
   * To 头部字符串（例如 "Bob <sip:bob@example.com>"）
   */
  char to_header[128];

  /**
   * Via 头部字符串（例如 "SIP/2.0/UDP 192.0.2.4;branch=z9hG4bK776asdhds"）
   */
  char via_header[256];
  /**
   * Call-ID头部字符串（例如 "a84b4c76e66710@192.168.1.1"）
   */
  char call_id_header[64];
  /**
   * CSeq 头部字符串（例如 "314159 INVITE"）
   */
  char cseq_header[64];
  /**
   * Contact 头部字符串（例如 "<sip:alice@pc33.example.com>"，可为 NULL）
   */
  char contact_header[128];
  /**
   *  Content-Type（例如 "application/sdp"，可为 NULL）
   */
  char content_type[64];

  /**
   * 消息体长度
   */
  size_t body_length;
  /**
   * 消息体内容, 实际长度为body_length,（可为 NULL）
   */
  char message_body[0];
}received_sip_message_t, *received_sip_message_ptr;


typedef struct info_param{
  char event[32];
  char command[32]; 
  char mode[32];
}info_param_t, *info_param_ptr;



/**
 * 是否为响应消息, 是则返回1, 否则返回0
 */
int is_response_message(received_sip_message_ptr msg);

/**
 * 是否为200 OK响应消息, 是则返回1, 否则返回0
 */ 
int is_response_ok(received_sip_message_ptr msg);
      

int build_200_ok_response(received_sip_message_ptr request, char **out_msg, size_t *out_len);

int build_register(sip_register_param_ptr param, char **out_msg, size_t *out_len);

int parse_sdp(const char *sdp_buf, downlink_sdp_parameter_ptr param);

int build_invite(sip_invite_param_ptr param, char **out_msg, size_t *out_len);



int build_ack(received_sip_message_ptr response, const char *uid, const char *device_ip, int cseq_num, char **out_msg, size_t *out_len);

int build_bye(received_sip_message_ptr invite, char* uid, char* device_ip, int cseq_num, char** out_msg, size_t* out_len); 

int build_listen_info(
               received_sip_message_ptr response,
               const char *uid,
               const char *device_ip,
               int cseq_num,
               info_param_ptr info,
               char **out_msg,
               size_t *out_len);    
       
int sip_parse_incoming_message(const char *raw_msg, size_t msg_len, received_sip_message_ptr *msg_info);

void free_sip_message(char* sip);
   

/**
 * SIP模块初始化, 使用sip模块前必须调用该函数，只需调用一次
 */
void init_sip();
#endif