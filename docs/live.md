# 直播场景协议说明

## 直播机器人的
  interaction: '机器人互动',
  explanation: '机器人讲解',
  duo: '双人互动',
  human_agent: '真人模式',
  manu_mode: '手动操控机器人模式'

  其中
 
## 嵌入式启动流程

1. 设备启动先建立 MQTT 连接
2. 不用等唤醒词，直接发起hello请求，建立udp session 以便建立音频通道，一旦监听到session过期则自动重建
3. 默认进入机器人互动模式，然后等待服务端下发模式切换，放弃原来的状态机

### 各个模式处理方式
     
#### 1. interaction: '机器人互动' 模式
   直播监听软件会监听直播室弹幕，并生成回答内容，通过sendtts(`${baseUrl}subscriber/vending/live/robot/text`,)接口,把回答内容发给服务器，服务器下tts语音给机器人，机器人播放tts语音 ，为了不用改服务端，嵌入式需要忽略{"session_id": "xxx", "type": "tts", "state": "stop"}TTS 结束 事件。


#### 2.  explanation: '机器人讲解',
   直播监听软件会根据规则获取用户设置的讲解内容，通过sendtts(`${baseUrl}subscriber/vending/live/robot/text`,)接口,把讲解内容发给服务器，服务器下tts语音给机器人，机器人播放tts语音 ，为了不用改服务端，嵌入式需要忽略{"session_id": "xxx", "type": "tts", "state": "stop"}TTS 结束 事件。

#### 3.  duo: '双人互动',
   直播监听软件不做任何动作，在这个状态下，设备运行模式和原来一样

#### 4.  human_agent: '真人模式',
   这个状态下，是主持人自己直播，设备不做任何处理，处于休息状态，也不会进行listen


 #### 5. manu_mode: '手动操控机器人模式'   
      这个状态下，也是收取服务端下发的tts 或者动作，播放语音或者做动作

  #### 机器人模式总结
      总的来说1，2，5 这几种状态，机器人的处理方式是一样的。
      所以总的来说，机器人只有双人模式，真人模式和另外的（1，2，5）这三种模式处理好就可以