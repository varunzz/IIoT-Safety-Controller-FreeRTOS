# 🛡️ IIoT Safety Controller & Edge Node (FreeRTOS)

![C++](https://img.shields.io/badge/Language-C++-blue)
![Framework](https://img.shields.io/badge/Framework-FreeRTOS-orange)
![Hardware](https://img.shields.io/badge/Hardware-ESP32-lightgrey)
![Protocol](https://img.shields.io/badge/Protocol-MQTT-yellowgreen)

## 📌 Project Overview
A fault-tolerant, multithreaded Industrial Internet of Things (IIoT) edge node designed for environmental hazard monitoring and physical mitigation. Built on an ESP32 microcontroller using **FreeRTOS**, this system completely decouples time-critical life-safety hardware interrupts from slower network communications to ensure zero latency during a hazard event.

> **🚀 Live Simulation:** [Click the link to run the full hardware and network simulation in your browser via Wokwi] *https://wokwi.com/projects/456845194710339585*

**📡 Live MQTT Telemetry:** While the simulation is running, you can monitor the real-time JSON payload published by the device using the [HiveMQ WebSocket Client](https://www.hivemq.com/demos/websocket-client/) — connect on port `8884` and subscribe to the topic `Project/smarthome/telemetry`.

## ⚙️ Core Architecture & Software Engineering Principles
This project was built to demonstrate production-grade embedded software practices, specifically targeting thread safety and resource management.

* **Preemptive Multithreading:** Utilized FreeRTOS to divide the monolithic loop into four distinct, priority-based tasks.
* **Thread-Safe Data Pipelines:** Implemented **RTOS Queues** (Producer-Consumer model) to safely transfer analog ADC and digital GPIO readings across memory boundaries without race conditions.
* **Resource Synchronization:** Deployed a **Mutex (Semaphore)** to protect the global system state array. This prevents memory corruption when the local OLED display and the Cloud MQTT task attempt to access sensor telemetry simultaneously.
* **Asynchronous Network Decoupling:** The Wi-Fi and MQTT client run on the lowest priority thread. If the network drops or experiences high latency, the high-priority local hazard detection and physical mitigation (servo dampers, alarms) continue to run flawlessly.

## Fault Tolerance & Reliability
* **Task Watchdog Timer (TWDT):** Critical tasks monitored with a 10-second hardware watchdog. System auto-resets if any task hangs or deadlocks.


## 🧰 Hardware & Peripherals (Simulated via Wokwi)
* **ESP32 Core:** Running dual-core FreeRTOS scheduler.
* **Sensors:** *DHT22 (Digital Temperature & Humidity)
  * PIR Sensor (Digital Motion/Occupancy)
  * Analog Slide Potentiometer (Simulating MQ-Series Gas/Smoke Sensor for fault injection)
* **Actuators & UI:**
  * SSD1306 OLED Display (I2C Protocol)
  * Servo Motor (PWM-controlled physical mitigation damper)
  * RGB LED (Digital GPIO Annunciator)

## 📡 Cloud Telemetry (MQTT)
The device acts as an MQTT Publisher. It connects to a cloud broker, serializes the memory-protected struct into a **JSON payload** utilizing `snprintf` for memory efficiency, and pushes telemetry at a controlled bandwidth rate.

**Sample JSON Payload:**
```json
{
  "temp": 46.5,
  "smoke": 3200,
  "motion": true,
  "alarm": true
}