# verivox-smart-feeder
IoT-enabled smart aquarium management system featuring automated feeding, water quality monitoring, and fail-safe scheduling.
# VERIVOX: Smart Aquarium Management System 🌊🤖

VERIVOX is an IoT-enabled smart fish feeder and water quality management system designed for residential aquariums. It ensures fish survival and optimal water chemistry through real-time monitoring, automated feeding schedules, and hierarchical safety lockouts.

## 🚀 Core Features
* **Priority Safety Lockouts:** Automatically halts feeding and triggers visual/mobile alarms if water pH crashes to toxic levels.
* **Smart Maintenance Alerts:** Continues offline feeding but alerts users when TDS or Turbidity thresholds indicate a dirty tank.
* **Dual-Redundancy Scheduling:** Syncs with the Arduino IoT Cloud via mobile UI sliders, backed by a local DS3231 RTC module to guarantee feeding even during total Wi-Fi blackouts.
* **Chemical Dosing Control:** Integrates an MD-102 driven peristaltic pump with 2FA mobile confirmation to safely correct pH imbalances.

## 💻 Tech Stack & Hardware
* **Microcontroller:** Arduino Nano 33 IoT
* **Cloud Infrastructure:** Arduino IoT Cloud (Dashboards & Webhooks)
* **Sensors:** pH, TDS, Turbidity, DS18B20 (Temperature for polynomial TDS compensation), HC-SR04 (Food level monitoring).
* **Actuators:** MG996R Servo (Feeder), Peristaltic Pump via MD-102 Driver.

## 🧠 System Architecture Highlight
This repository contains the production firmware for the VERIVOX mainboard. The code is optimized to minimize server payload using delta thresholds (`thingProperties.h`) and features strict interrupt management (`IrReceiver.stop()`) to prevent timer conflicts between the IR hardware and PWM servo signals.

---
*Developed as a Final Year Software Engineering Project by Syajaratul Diana at Universiti Malaysia Sarawak (UNIMAS).*
