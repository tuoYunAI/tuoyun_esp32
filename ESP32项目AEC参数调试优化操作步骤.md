# ESP32 项目 AEC 参数调试优化操作步骤

## 1. 文档目的

本文档用于指导以下ESP32项目进行可重复的 AEC 参数调试和效果优化：

主要源码目录：

```text
../tuoyun_esp32/main
```

调试目标：

1. 在设备播放 TTS 时降低上传音频中的扬声器回声；
2. 保留播放期间用户插话的词头、辅音、词尾和声纹特征；
3. 避免纯 TTS 残留持续触发服务器 VAD；
4. 保证本地唤醒词可以立即触发 `AbortSpeaking`；
5. 建立可量化、可回退、可移植的参数优化流程。

---

## 2. 当前方案基线

当前设备为单 MIC，AEC 使用两路数字信号：

- `MIC`：麦克风原始声音，包含用户语音、环境声音和扬声器回声；
- `REF`：实际播放 PCM 的软件参考，不是第二个 MIC。

```text
播放 PCM ──┬──> Codec / 功放 / 扬声器
           └──> 软件 REF ─────────┐
                                  ├──> ESP-SR AFE ─> 上传门控 ─> Opus ─> 服务器
单 MIC ─────────────────> MIC ─────┘

单 MIC 原始音频 ─> 本地唤醒词 ─> AbortSpeaking
```

当前基线参数：

```text
软件 REF 启动补偿：25 ms
新播放段判定间隔：120 ms
AEC 模式：AEC_MODE_FD_HIGH_PERF
AEC filter length：4
NLP：AEC_NLP_LEVEL_NORMAL
AGC：关闭
AFE VAD：设备 AEC 模式下关闭
AFE 输出延迟：2 帧，约 64 ms
```

当前上传门控参数：

```cpp
constexpr double kReferenceActiveEnergy = 250.0 * 250.0;
constexpr double kNearEndMicEnergy = 180.0 * 180.0;
constexpr double kNearEndOutputToMicEnergy = 0.42 * 0.42;
constexpr double kNearEndMicToReferenceEnergy = 0.32 * 0.32;
constexpr int kNearEndHangoverFrames = 3;
```

后续调试应从这组基线开始，不要同时随机修改多个参数。

---

## 3. 调试总原则

1. 先处理硬件、增益和削波，再调软件参数；
2. 先校准 `MIC/REF` 时序，再调 AFE，再调上传门控；
3. 一次只修改一类参数；
4. 每组参数至少重复测试 3 次；
5. 每次使用相同 TTS、说话内容、距离、方向和音量；
6. 同时测试低、中、高播放音量；
7. 不以一次 ASR 正确或一次声纹通过作为最终结论；
8. 每组日志、PCM、服务器日志和固件使用同一个测试编号；
9. 参数变差时立即回退到上一组有效基线；
10. 调试完成后关闭 PCM 和周期统计日志。

---

## 4. 调试前固定测试条件

调试期间不要更换或移动以下部件：

- MIC 型号和安装位置；
- 扬声器型号和安装位置；
- Codec 和功放；
- 机壳、扬声器腔体和密封结构；
- MIC 硅胶套或声学密封结构；
- MIC 输入增益；
- 播放 EQ、动态增益和限幅策略；
- 测试时设备摆放位置。

建议固定测试条件：

```text
说话距离：例如 30 cm
说话方向：正对 MIC
环境：安静房间
低音量：30%
中音量：50%
高音量：70%
最大安全音量：不产生削波和明显破音的最高音量
```

### 4.1 先检查削波

检查 `MIC.pcm` 和 `REF.pcm` 是否存在大量接近以下数值的采样：

```text
+32767
-32768
```

如果高音量下出现以下现象，应先处理硬件或增益：

- 功放削波；
- 扬声器破音；
- MIC 或 ADC 饱和；
- 塑料壳、PCB 或支架明显共振；
- 扬声器前后腔漏气；
- MIC 声道与机壳内部公共腔体串通。

AEC 主要建模线性回声路径，无法可靠消除严重削波和非线性失真。

---

## 5. 开启 AEC 调试采集

修改文件：

```text
.../tuoyun_esp32/main/boards/echoear/config.json
```

将调试开关设置为：

```json
"CONFIG_USE_DEVICE_AEC=y",
"CONFIG_AEC_PCM_HEX_DUMP=y",
"CONFIG_AEC_PCM_HEX_DUMP_SECONDS=2"
```

调试开启后同步采集四路 PCM：

| 通道 | 内容 |
| --- | --- |
| `MIC` | 原始 MIC，包含用户声音和扬声器回声 |
| `REF` | 实际提供给 AEC 的播放参考 |
| `AEC` | ESP-SR AFE 输出，上传门控之前 |
| `UPLOAD` | 实际送入 Opus 编码器并上传服务器的音频 |

PCM 会在设备空闲后以十六进制输出，避免实时打印干扰音频处理。

---

## 6. 编译和烧录

进入项目目录：

```bash
cd tuoyun_esp32
```

加载 ESP-IDF 环境：



---

## 7. 标准测试矩阵

每一组参数至少执行以下场景。

### 7.1 纯播放测试

操作：

1. 设备播放持续 5 秒以上的 TTS；
2. 用户完全不说话；
3. 分别测试低、中、高音量；
4. 同时保存设备日志和服务器日志。

预期：

- `MIC` 中存在明显 TTS 回声；
- `REF` 与实际播放内容一致；
- `AEC` 中 TTS 残留明显降低；
- `UPLOAD` 在纯播放区间接近静音；
- 服务器 VAD 不跟随整段 TTS 持续触发；
- 纯 TTS 不能通过用户声纹验证。

### 7.2 纯近端语音测试

操作：

1. 扬声器不播放；
2. 用户说短词，例如“打断”；
3. 用户说约 2 秒的完整句子；
4. 用户从正常距离和较远距离各测试一次。

预期：

- 用户声音不能被门控压成静音；
- 词头、辅音和词尾完整；
- ASR 和声纹结果接近关闭 AEC 时的基线；
- 无播放时门控应直接放行。

### 7.3 双讲测试

操作：

1. 先让 TTS 播放至少 1.5 秒；
2. 播放过程中分别说：
   - “打断”；
   - “打断打断打断”；
   - 一句约 2 秒的完整语句；
3. 在低、中、高播放音量下重复测试。

预期：

- `AEC` 中用户声音仍然可辨认；
- `UPLOAD` 保留用户插话；
- 用户停止后 `UPLOAD` 快速恢复静音；
- 短词和长句均能保留；
- 不能仅凭重复多次后的长音频效果判断短词已经可用。

### 7.4 唤醒词打断测试

操作：

1. TTS 播放期间说唤醒词；
2. 检查设备是否立即向服务器发送 `AbortSpeaking`；
3. 在低、中、高音量下重复测试。

预期：

- 本地唤醒词检测读取原始 MIC；
- 唤醒词不受上传门控影响；
- `AbortSpeaking` 不依赖服务器 VAD、ASR 或声纹完成。

---

## 8. 从日志导出四路 PCM

将设备日志保存为：

```text
.../tuoyun_esp32/aec.log
```

导出四路 PCM：

```bash
cd .../tuoyun_esp32

for ch in MIC REF AEC UPLOAD; do
  awk -F'|' -v ch="$ch" \
    '$1=="PCM_HEX" && $2==ch {print $4}' aec.log \
    | xxd -r -p > "${ch}.pcm"
done
```

转换为 WAV：

```bash
for ch in MIC REF AEC UPLOAD; do
  ffmpeg -y -f s16le -ar 16000 -ac 1 \
    -i "${ch}.pcm" "${ch}.wav"
done
```

直接播放 PCM：

```bash
ffplay -f s16le -ar 16000 -ac 1 UPLOAD.pcm
```

确认四路文件采样数接近一致，避免把不同抓取轮次的数据混在一起分析。

---

## 9. 第一步：校准软件 REF 时序

参数位置：

```text
.../tuoyun_esp32/main/boards/echoear/config.h
```

当前值：

```cpp
#define AUDIO_INPUT_REFERENCE_DELAY_MS 25
```

调试方法：

1. 使用纯播放或播放占主导的采集片段；
2. 计算 `REF` 与 `MIC` 的归一化互相关；
3. 找出相关峰对应的 `MIC/REF` 延迟；
4. 检查同一硬件多次采集时延迟是否稳定；
5. 依次测试 `15、20、25、30、35 ms`；
6. 每次只改该参数；
7. 选择纯播放期间 `AEC` 残留能量最低且双讲稳定的值。

注意事项：

- 该参数是软件播放参考的启动补偿；
- 它不是 AFE 固定输出延迟；
- `REF` 应与实际送往播放链路的 PCM 顺序一致；
- `REF` 不能丢帧、重复帧或在连续播放中重复插入静音。

新播放段判断位于：

```text
.../tuoyun_esp32/main/audio/codecs/box_audio_codec.cc
```

当前值：

```cpp
constexpr int64_t kReferenceBurstGapUs = 120 * 1000;
```

只有实际播放间隔超过约 `120 ms`，才重新插入一次启动补偿。连续约 60 ms 的 TTS 数据包之间不能重复增加延迟。

---

## 10. 第二步：调试 ESP-SR AFE

参数位置：

```text
.../tuoyun_esp32/main/audio/processors/afe_audio_processor.cc
```

当前基线：

```cpp
afe_config->aec_mode = AEC_MODE_FD_HIGH_PERF;
afe_config->aec_filter_length = 4;
afe_config->aec_nlp_level = AEC_NLP_LEVEL_NORMAL;
afe_config->agc_init = false;
```

建议调整顺序：

1. 固定 `AEC_MODE_FD_HIGH_PERF`；
2. 完成 REF 时延校准；
3. 比较 AEC filter length；
4. 用纯播放残留评价回声抑制；
5. 用双讲语音评价近端损伤；
6. 最后比较 NLP 等级；
7. AGC 暂时保持关闭。

评价时必须同时关注：

- 纯播放残留是否下降；
- 用户语音是否被削弱；
- 用户辅音和频谱包络是否受损；
- ASR 短词正确率是否下降；
- 声纹分数是否下降。

NLP 越强，纯播放残留可能越低，但双讲时用户音色和声纹也可能受到更明显的修改。当前业务重视声纹，因此基线采用 `AEC_NLP_LEVEL_NORMAL`。

---

## 11. 第三步：校准 AFE 固定输出延迟

当前参数：

```cpp
constexpr size_t kAecOutputDelayFrames = 2;
```

当前实测约为：

```text
2 × 32 ms = 64 ms
```

调试方法：

1. 在 `MIC` 与 `AEC` 中找到同一段用户语音或明显瞬态；
2. 测量 `AEC` 相对 `MIC` 的固定偏移；
3. 将偏移换算为 AFE 帧数；
4. 连续采集多次，确认固定延迟没有漂移；
5. 使用该帧数对齐门控所用的 MIC、REF 能量和当前 AEC 输出。

该值错误时可能出现：

- 纯播放被误判为用户语音；
- 用户语音被误压成静音；
- 词头丢失；
- 门控延迟打开；
- 用户已经停止，门控仍在放行旧片段。

---

## 12. 第四步：校准上传门控

参数位置：

```text
.../tuoyun_esp32/main/audio/processors/afe_audio_processor.cc
```

门控基本逻辑：

1. REF 不活跃：直接放行；
2. REF 活跃且为纯播放：上传静音；
3. REF 活跃且存在近端用户：放行 AEC 输出；
4. 用户停止后保留短暂挂起，避免切掉词尾。

### 12.1 REF 活跃阈值

```cpp
kReferenceActiveEnergy
```

目标：

- TTS 播放时稳定判定 REF 活跃；
- 播放停止后快速恢复不活跃；
- REF 底噪不能触发；
- 低音量播放也能够被识别。

### 12.2 MIC 近端阈值

```cpp
kNearEndMicEnergy
```

目标：

- 正常距离说话能够打开门控；
- 较轻的用户语音不会完全丢失；
- MIC 底噪、壳体振动和扬声器轻微残留不能打开门控。

### 12.3 AEC/MIC 能量比例

```cpp
kNearEndOutputToMicEnergy
```

含义：AEC 输出中是否仍保留了足够的近端用户能量。

- 设置过高：用户声音被 AEC 削弱后可能无法放行；
- 设置过低：TTS 残留可能被误判为用户声音。

### 12.4 MIC/REF 能量比例

```cpp
kNearEndMicToReferenceEnergy
```

含义：MIC 相对 REF 的增量是否足以说明存在双讲。

- 设置过高：大音量播放时用户很难打开门控；
- 设置过低：纯播放容易误开门控。

### 12.5 双讲挂起时间

```cpp
kNearEndHangoverFrames
```

当前为 3 帧，约 `96 ms`。

- 用户词尾被切掉：适当增大；
- 用户停止后 TTS 泄漏时间过长：适当减小；
- 建议在 `64–160 ms` 范围内测试。

门控参数建议按照以下顺序修改：

```text
REF 活跃阈值
    ↓
MIC 近端阈值
    ↓
AEC/MIC 比例
    ↓
MIC/REF 比例
    ↓
挂起时间
```

---

## 13. 量化评价指标

### 13.1 削波比例

统计满足以下条件的采样比例：

```text
abs(sample) >= 32760
```

该比例应尽量接近零。

### 13.2 纯播放回声抑制量

可使用 ERLE：

```text
ERLE = 10 × log10(MIC 回声能量 / AEC 残留能量)
```

ERLE 越高，说明线性回声抑制越强。比较时必须选取同一段纯播放区间。

### 13.3 残余 TTS 相关性

计算 `AEC` 与 `REF` 的最大归一化互相关：

- 相关性越低，说明 AEC 中的 TTS 残留越少；
- 应在完成时延搜索后比较最大相关值；
- 不能只比较零延迟相关性。

### 13.4 双讲用户保留率

```text
用户保留率 = RMS(AEC 用户区间) / RMS(MIC 用户区间)
```

该指标不是越高越好，但如果过低，通常表示用户声音被 AEC/NLP 过度削弱。

### 13.5 UPLOAD 静音率

- 纯播放区间应接近全静音；
- 双讲区间不能被压成静音；
- 用户停止后应快速恢复静音；
- 需要同时检查连续零样本比例和语音区间完整度。

### 13.6 服务器业务指标

每组参数记录：

- 服务器 VAD 误触发次数；
- VAD 片段持续时间；
- ASR 短词正确率；
- ASR 长句正确率；
- 声纹分数；
- `ACCEPT/UNCERTAIN/REJECT` 分布；
- 纯 TTS 是否出现声纹误接受；
- 唤醒词触发成功率；
- `AbortSpeaking` 延迟；
- 用户词头和词尾完整度。

---

## 14. 推荐调试顺序

完整调试顺序如下：

```text
1. 固定硬件和测试环境
2. 检查 MIC、Codec、功放和扬声器是否削波
3. 开启四路 PCM 调试采集
4. 建立关闭 AEC 或当前基线的对照数据
5. 校准软件 REF 启动补偿
6. 检查连续播放时 REF 是否丢帧、重复或漂移
7. 校准 ESP-SR AFE 参数
8. 测量 AFE 固定输出延迟
9. 校准上传门控阈值
10. 校准双讲挂起时间
11. 执行低、中、高音量测试矩阵
12. 对比服务器 VAD、ASR 和声纹结果
13. 重复测试并确认结果稳定
14. 固化板级参数和测试记录
15. 关闭调试日志并重新构建生产固件
```

---

## 15. 测试记录模板

每次测试建议创建如下记录：

```markdown
# AEC 测试记录

- 测试编号：
- 测试日期：
- 固件 SHA-256：
- 硬件版本：
- MIC 型号：
- 扬声器型号：
- 功放 / Codec：
- 机壳版本：
- 测试距离：
- 播放音量：
- MIC 增益：
- 测试 TTS：
- 用户说话内容：

## 参数

- REF delay：
- Playback burst gap：
- AEC mode：
- Filter length：
- NLP：
- AGC：
- AFE output delay frames：
- REF active threshold：
- MIC near-end threshold：
- AEC/MIC ratio：
- MIC/REF ratio：
- Hangover frames：

## PCM 指标

- MIC clipping ratio：
- REF clipping ratio：
- MIC/REF lag：
- AEC/MIC lag：
- ERLE：
- AEC/REF residual correlation：
- Near-end retention：
- UPLOAD silence ratio：

## 服务器结果

- VAD 误触发次数：
- ASR 结果：
- 声纹分数：
- 声纹判断：
- AbortSpeaking 延迟：

## 主观检查

- 是否存在 TTS 残留：
- 用户词头是否完整：
- 用户词尾是否完整：
- 用户音色是否明显失真：
- 是否出现爆音或门控断裂：

## 结论

- 相对上一版本：改善 / 持平 / 变差
- 是否保留该参数：是 / 否
- 下一步仅修改：
```

---

## 16. 结束调试并生成生产固件

完成参数确认后，将文件：

```text
.../tuoyun_esp32/main/boards/echoear/config.json
```

恢复为：

```json
"CONFIG_AEC_PCM_HEX_DUMP=n"
```

重新执行完整构建：

```bash
cd /Volumes/DevOps/code/AI.TOY/equipments/public_tuoyun_esp32
source /Users/denzellin/esp/esp-idf/export.sh
python3 scripts/release.py echoear --name echoear
```

检查最终构建配置：

```bash
rg 'AEC_PCM_HEX_DUMP|USE_DEVICE_AEC' \
  sdkconfig build/config/sdkconfig.h build/config/sdkconfig.cmake
```

预期：

```text
CONFIG_USE_DEVICE_AEC=y
# CONFIG_AEC_PCM_HEX_DUMP is not set
```

检查固件不包含调试字符串：

```bash
strings build/xiaozhi.bin | rg \
  'PCM_HEX|AFE input level|AFE output level|AEC upload gate'
```

正常情况下不应输出匹配结果。

最后记录生产固件摘要：

```bash
shasum -a 256 build/xiaozhi.bin
```

---

## 17. 验收标准

### 17.1 纯播放

- `MIC` 中能采集到扬声器声音；
- `REF` 内容和时序正确；
- `AEC` 残留明显下降；
- `UPLOAD` 接近静音；
- 服务器 VAD 不持续跟随 TTS；
- 纯 TTS 不通过用户声纹验证。

### 17.2 双讲

- 播放期间可以检测用户插话；
- 用户词头和词尾完整；
- ASR 能识别短词和完整句子；
- 声纹在有效语音足够长时稳定；
- 用户停止后 `UPLOAD` 恢复静音；
- 不因门控频繁开关产生明显爆音或断裂。

### 17.3 唤醒词

- 播放期间能够检测本地唤醒词；
- 设备立即发送 `AbortSpeaking`；
- 行为不依赖服务器声纹是否完成；
- 上传门控不影响本地唤醒词链路。

### 17.4 高音量

- MIC、Codec、功放和扬声器没有明显削波；
- AEC 效果不会随音量增加而突然恶化；
- 如果高音量仍明显变差，应优先检查硬件非线性、腔体漏气、机壳共振和 MIC 饱和，不应仅继续放宽软件门控阈值。

---

## 18. 结论

本项目 AEC 调试的核心顺序是：

```text
硬件与增益
    ↓
REF 内容与时序
    ↓
ESP-SR AFE 参数
    ↓
AFE 固定输出延迟
    ↓
上传双讲门控
    ↓
服务器 VAD / ASR / 声纹验证
```

其中最重要的约束是：

1. 软件 REF 必须连续、准确并与真实播放时序一致；
2. 播放链路和 MIC 采集链路不能发生严重削波；
3. 纯播放抑制和双讲用户保真必须同时评价；
4. 门控阈值必须按具体 MIC、扬声器、音量和机壳重新校准；
5. 调试日志只能临时开启，生产固件必须关闭。
