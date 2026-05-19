# Traffic Light Control System (FreeRTOS)

## Overview
This project implements a **real-time traffic light control system** using **FreeRTOS** in a Visual Studio WIN32 simulation environment.

The system manages traffic flow at an intersection, synchronizes pedestrian signals, handles emergency vehicle prioritization, and logs all state transitions.

---

## Features
- North–South and East–West traffic light control
- Pedestrian signal synchronization
- Emergency vehicle detection and preemption
- Real-time scheduling using FreeRTOS
- Console-based simulation
- Event logging with timestamps

---

## FreeRTOS Concepts Used
- Task creation and scheduling
- Preemptive multitasking
- Event Groups (for emergency handling)
- Binary Semaphores (system resume control)
- vTaskDelay() for precise timing

---

## System Architecture

### Tasks:
- **Controller Task**
  - Manages normal traffic light sequence
- **Emergency Task**
  - Detects emergency events and overrides system behaviour

---

## Timing Specification

### North–South:
- Green: 10 seconds
- Yellow: 3 seconds
- Red: 11 seconds

### East–West:
- Green: 8 seconds
- Yellow: 3 seconds
- Red: 13 seconds

---

## Emergency Handling
When an emergency is detected:
- East–West light turns GREEN
- North–South turns RED
- Pedestrian signals are disabled
- System resumes normal flow after emergency clears

