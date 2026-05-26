# VERIVOX: Smart Aquarium Management System 🌊🤖

VERIVOX is an IoT-enabled smart fish feeder and water quality management system designed for residential aquariums. It ensures fish survival and optimal water chemistry through real-time monitoring, automated feeding schedules, and hierarchical safety lockouts.

## 🚀 Core Features
* **Priority Safety Lockouts:** Automatically halts feeding and triggers visual/mobile alarms if water pH crashes to toxic levels.
* **Smart Maintenance Alerts:** Continues offline feeding but alerts users when TDS or Turbidity thresholds indicate a dirty tank.
* **Dual-Redundancy Scheduling:** Syncs with the Arduino IoT Cloud via mobile UI sliders, backed by a local DS3231 RTC module to guarantee feeding even during total Wi-Fi blackouts.
* **Chemical Dosing Control:** Integrates an MD-102 driven peristaltic pump with 2FA mobile confirmation to safely correct pH imbalances.

## 💻 Tech Stack & Electronics
* **Microcontroller:** Arduino Nano 33 IoT
* **Cloud Infrastructure:** Arduino IoT Cloud (Dashboards & Webhooks)
* **Sensors:** pH, TDS, Turbidity, DS18B20 (Temperature for polynomial TDS compensation), HC-SR04 (Food level monitoring).
* **Actuators:** MG996R Servo (Feeder), Peristaltic Pump via MD-102 Driver.

## ⚙️ Open-Source Hardware & 3D Enclosure
The physical architecture of VERIVOX was custom-engineered using Fusion 360. All CAD files are provided in the `/3d model` directory to support the Open-Source Hardware (OSHW) community.

* **Interactive Assembly:** Click on the `VERIVOX_Full_Assembly.stl` file within GitHub to view and rotate the fully assembled hardware in 3D.
* **Manufacturing Ready:** Individual components (Main Body, Lid, Motor Mount, Worm-Screw, etc.) are provided in modern `.3mf` format, preserving crucial slicing data for 3D printing.
* **Design Highlights:** The enclosure features a precise sliding back panel for easy maintenance access, custom filament clearances, and a thumb-grip design for ergonomic manual operation.

## 🧠 System Architecture Highlight
This repository contains the production firmware for the VERIVOX mainboard. The code is optimized to minimize server payload using delta thresholds (`thingProperties.h`) and features strict interrupt management (`IrReceiver.stop()`) to prevent timer conflicts between the IR hardware and PWM servo signals.

---
*Developed as a Final Year Software Engineering Project by Syajaratul Diana at Universiti Malaysia Sarawak (UNIMAS).*
