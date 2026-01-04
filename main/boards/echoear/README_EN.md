# EchoEar 喵伴

## Introduction

<div align="center">
    <a href="https://oshwhub.com/esp-college/echoear"><b> LCSC Open Source Platform </b></a>
</div>

EchoEar 喵伴 is an intelligent AI development kit equipped with ESP32-S3-WROOM-1 module, 1.85-inch QSPI circular touch screen, dual microphone array, supporting offline voice wake-up and sound source localization algorithms. For hardware details, please refer to the [LCSC Open Source Project](https://oshwhub.com/esp-college/echoear).

## Configuration and Build Commands

### First-time Build Preparation

**⚠️ Important**: Before the first build, you need to run the following command in the project root directory to generate the default configuration file:

```bash
python scripts/release.py echoear
```

### Version Selection

EchoEar has two hardware versions:
- **V1_0** (Open source version, default)
- **V1_2**

If you are using the V1_2 version, you need to manually modify the `select_board` configuration in the `main/boards/echoear/config.h` file.

### Configure Build Target to ESP32S3

```bash
idf.py set-target esp32s3
```

### Open menuconfig and Configure

```bash
idf.py menuconfig
```

Configure the following options:

#### Basic Configuration
- `Xiaozhi Assistant` → `Board Type` → Select `EchoEar`

### UI Style Selection

EchoEar supports multiple different UI display styles, selectable through menuconfig:

- `Xiaozhi Assistant` → `Select display style` → Select display style

#### Available Styles

##### Expression Assets Mode - Default
- **Configuration Option**: `CONFIG_FLASH_EXPRESSION_ASSETS=y`
- **Features**: Generates asset files from `esp_emote_assets`
- **Functions**: Uses custom asset files, asset files location is in `boards/echoear/assets` directory
- **Use Case**: Default mode for firmware with base
- **Class**: `emote::EmoteDisplay`

##### Emote Animation Style - Recommended
- **Configuration Option**: `USE_EMOTE_MESSAGE_STYLE`
- **Features**: Uses custom `EmoteDisplay` expression display system
- **Functions**: Supports rich expression animations, eye animations, and status icon display
- **Use Case**: Smart assistant scenarios, providing a more vivid human-computer interaction experience
- **Class**: `emote::EmoteDisplay`

**⚠️ Important**: Selecting this style requires additional configuration of custom asset files:
1. `Xiaozhi Assistant` → `Flash Assets` → Select `Flash Custom Assets`
2. `Xiaozhi Assistant` → `Custom Assets File` → Enter the asset file URL:
   ```
   https://dl.espressif.com/AE/wn9_nihaoxiaozhi_tts-font_puhui_common_20_4-echoear.bin
   ```

##### Default Message Style
- **Configuration Option**: `USE_DEFAULT_MESSAGE_STYLE` (default)
- **Features**: Uses standard message display interface
- **Functions**: Traditional text and icon display interface
- **Use Case**: Standard conversation scenarios
- **Class**: `SpiLcdDisplay`

##### WeChat Message Style
- **Configuration Option**: `USE_WECHAT_MESSAGE_STYLE`
- **Features**: WeChat-like chat interface style
- **Functions**: WeChat-like message bubble display
- **Use Case**: For users who prefer WeChat style
- **Class**: `SpiLcdDisplay`

> **Note**: EchoEar uses 16MB Flash and requires a dedicated partition table configuration to properly allocate storage space for applications, OTA updates, asset files, etc.

Press `S` to save, press `Q` to exit.

**Build**

```bash
idf.py build
```

**Flash**

Connect EchoEar to your computer, **make sure to turn on the power**, and run:

```bash
idf.py flash
```

## Feature Usage Instructions

### 1. DOA Sound Direction Following Mode

- **Default State**: Enabled by default on power-on
- **Function**: Head can rotate following the sound direction
- **Voice Control**: Say "doa_follow" or "DOA sound direction following mode" to enable

### 2. Beat Detection Mode

- **Function**: Head can sway following the beat of music
- **Switching Method**: Switch via voice command
- **Voice Control**: Say "beat_detection" or "beat detection mode, dance with the music"

### 3. Pomodoro Timer Mode

- **Activation Method**:
  - **Swipe left** or **swipe down** on the standby expression home screen to automatically start
  - Default time: 5 minutes
  - Time can be manually adjusted

- **Operation Instructions**:
  - **Tap** the center of the pomodoro timer: Pause/Resume
  - After time ends, automatically jump to the end screen
  - On the end screen, you can choose:
    - Continue for 5 minutes
    - Swipe to end and return to standby home screen

- **Voice Control**:
  - "Help me start a (1-60 minutes) pomodoro timer" - Set a pomodoro timer with specified duration
  - "Pause pomodoro timer" - Pause the running pomodoro timer
  - "Start pomodoro timer" - Resume the paused pomodoro timer

### 4. 24-Hour Clock Mode

- **Activation Method**: **Swipe right** or **swipe up** on the standby expression home screen to enter
- **Time Settings**:
  - Start time: Automatically set to current time
  - End time: Can be manually adjusted
- **Display Mode Toggle**: **Tap** the center of the clock interface to switch between the following two modes:
  - `xx:xx - xx:xx` (Time range display)
  - `xx hr` (Duration display)
- **Voice Control**: "Help me set an alarm for XX:XX" (e.g., "Help me set an alarm for 8:30")

### 5. Voice Wake-up Function

Supports the following 4 wake-up methods:

1. **Move the magnetic switch at the base neck position** - Physical wake-up
2. **Voice wake-up** - Say "Hello 喵伴" (Hello Miaoban)
3. **Tap the standby screen** - Touch wake-up
4. **Touch the head for 1.2 seconds** - Long press touch, will send a head touch event to the server

### 6. Base Calibration

- **Manual Calibration**:
  - **Long press** the base BOOT button to trigger the magnetic button calibration function
  - UI will prompt in sequence:
    - "Step 1: Move the magnetic button in the opposite direction"
    - "Step 2: Remove the magnetic button"

- **Voice Calibration**: Say "Calibrate base"

