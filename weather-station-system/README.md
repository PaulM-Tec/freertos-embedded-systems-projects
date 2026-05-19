# Weather Station System (FreeRTOS)

## Overview
This project implements a **multi-tasking weather station system** using **FreeRTOS**, designed to monitor, log, and display environmental data in real time.

The system simulates temperature and humidity sensors and demonstrates concurrent task execution with proper synchronization.

---

## Features
- Temperature monitoring (every 5 seconds)
- Humidity monitoring (every 7 seconds)
- Data logging (every 10 seconds)
- Real-time display updates (every 2 seconds)
- Console simulation output

---

## FreeRTOS Concepts Used
- Task scheduling and concurrency
- Mutex synchronization (shared variables)
- Periodic task execution
- Real-time data processing

---

## System Architecture

### Tasks:
- **Temperature Task**
  - Reads temperature values every 5 seconds
- **Humidity Task**
  - Reads humidity values every 7 seconds
- **Logging Task**
  - Writes data to file every 10 seconds
- **Display Task**
  - Updates console every 2 seconds

---

## Synchronization
- A mutex is used to protect shared variables
- Prevents race conditions between tasks

---

## Data Logging
- Logs written to file every 10 seconds
- Includes:
  - Timestamp
  - Temperature
  - Humidity
