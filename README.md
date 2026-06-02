# Anti-Theft Alarm System using ESP32

An IoT-based anti-theft alarm system built with **ESP32**, a **reed switch**, a **buzzer**, and **Telegram alert integration**. The system detects unauthorized opening of a door, window, or other entry point and immediately triggers a local alarm while sending a notification to Telegram.

## Features

* Door/window intrusion detection using a reed switch
* Loud buzzer alarm on detection
* Instant Telegram alert notification
* ESP32-based Wi-Fi connectivity
* Simple and low-cost security solution

## Components Used

* ESP32 development board
* Reed switch
* Buzzer
* Jumper wires
* Breadboard or perfboard
* Power supply / USB cable

## Working Principle

The reed switch is placed on a door or window frame. When the door/window is closed, the magnetic contact keeps the switch in its normal state. When the door/window opens, the reed switch changes state, and the ESP32 detects the trigger.

After detecting intrusion, the ESP32:

1. Activates the buzzer
2. Sends an alert message to Telegram

## Telegram Alert Integration

This project uses Telegram Bot API to send messages to a Telegram chat or group.

### Requirements

* Telegram bot token
* Telegram chat ID

### Setup Steps

1. Create a bot using **BotFather** on Telegram.
2. Copy the bot token.
3. Get your chat ID.
4. Add the token and chat ID in the Arduino code.
5. Upload the code to ESP32.

## Circuit Connections

> Replace the pin numbers below with the ones used in your project.

* **Reed Switch**

  * One terminal to ESP32 GPIO pin
  * Other terminal to GND or VCC depending on your circuit logic
* **Buzzer**

  * Positive pin to ESP32 GPIO pin
  * Negative pin to GND

## Installation

### 1. Clone the repository

```bash
git clone https://github.com/prash-02/Anti-Theft-Alarm-System-using-ESP32.git
cd Anti-Theft-Alarm-System-using-ESP32
```

### 2. Open the project

Open the Arduino sketch file in **Arduino IDE** or **PlatformIO**.

### 3. Install required libraries

Make sure the following libraries are available:

* WiFi
* HTTPClient
* UniversalTelegramBot

### 4. Configure your credentials

Update the following values in the code:

```cpp
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";
const char* botToken = "YOUR_TELEGRAM_BOT_TOKEN";
const char* chatID = "YOUR_CHAT_ID";
```

### 5. Upload to ESP32

* Select the correct ESP32 board in Arduino IDE
* Select the correct COM port
* Click **Upload**

## Usage

1. Power on the ESP32
2. Connect the reed switch to the door/window
3. When the contact breaks, the buzzer sounds and a Telegram alert is sent

## Project Structure

```bash
anti-theft-alarm-system/
├── README.md
├── ESP32 anti theft alarm.ino
└── images/
```

## Future Improvements

* Add PIR sensor support
* Add GSM backup alerts
* Add battery backup
* Add mobile dashboard
* Add status LED indicator

## Applications

* Home security
* Locker/cabinet security
* Office door monitoring
* Window intrusion detection

