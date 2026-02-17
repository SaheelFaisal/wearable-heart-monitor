# Wearable Wireless Heart Monitor (Capstone Project)

**Role:** Firmware Engineer  
**Status:** In Development (Week 3)

## Project Overview
A low-power, wearable ECG device capable of real-time R-peak detection and Bluetooth transmission.
- **MCU:** Seeed XIAO nRF52840 (ARM Cortex-M4)
- **Algorithm:** Pan-Tompkins (Real-time implementation)
- **Power:** <50mA active current (Lithium-Ion)

## Repository Structure
- `/firmware`: C++ source code for the nRF52840.
- `/simulation`: Python scripts for algorithm validation using MIT-BIH Arrhythmia Database.
- `/docs`: System architecture and weekly progress reports.

## How to Run the Simulation
1. Install dependencies: `pip install -r simulation/requirements.txt`
2. Run the POC: `python simulation/pan_tompkins_poc.py`

## License
MIT License