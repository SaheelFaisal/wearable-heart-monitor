# Wearable Wireless Heart Monitor (Capstone Project)

**Role:** Firmware Engineer & System Architect  
**Status:** Prototype Validated (Week 5)  
**Target:** 250Hz Real-Time ECG Analysis

## Project Overview
A low-power, wearable ECG device capable of real-time R-peak detection, heart rate variability analysis, and Bluetooth transmission. This project ports the industry-standard **Pan-Tompkins Algorithm** to an embedded Cortex-M4 environment, optimized for battery-constrained operation.

- **MCU:** Seeed XIAO nRF52840 (ARM Cortex-M4F @ 64MHz)
- **Sensor:** AD8232 Single Lead Heart Rate Monitor (Analog Front End)
- **Power:** <50mA active current (Lithium-Ion)
- **Sampling Rate:** 250 Hz (4ms period)

## Key Features (Implemented)
- **Real-Time DSP Pipeline:**
    - **Digital Filtering:** 3rd-Order Butterworth Bandpass (5-15Hz) implemented using Second-Order Sections (SOS) for stability.
    - **QRS Enhancement:** Five-point Derivative, Squaring, and Moving Window Integration (150ms window).
- **Adaptive Thresholding:** Automatic adjustment of signal and noise thresholds to detect R-peaks dynamically.
- **Verification Suite:**
    - Hardware-in-the-Loop (HIL) simulation using **MIT-BIH Arrhythmia Database (Record 100)**.
    - Python-based real-time telemetry and plotting tool.

## Repository Structure
```text
wearable-heart-monitor/
├── firmware/                 # PlatformIO C++ Project (nRF52840)
│   ├── src/                  # Main application logic
│   │   └── main.cpp          # Pan-Tompkins implementation
│   ├── include/              # Header files
│   │   └── test_signal.h     # MIT-BIH Dataset (Record 100)
│   └── platformio.ini        # Build configuration
│
├── simulation/               # Python Validation Tools
│   ├── realtime_plotter.py   # Live serial grapher (Matplotlib)
│   └── requirements.txt      # Python dependencies
│
└── docs/                     # Architecture & Weekly Reports
```



## How to Run Hardware-in-the-Loop Simulation

This verifies firmware logic using the MIT-BIH dataset instead of the physical sensor.



### 1. Flash the Firmware

- Open the `firmware/` folder in VS Code (with PlatformIO installed).
- Connect the Seeed XIAO nRF52840 via USB.
- Click **PlatformIO: Upload**.



### 2. Install Python Dependencies

```bash
pip install -r simulation/requirements.txt
```

### 3. Run the Real-Time Plotter

```bash
python simulation/realtime_plotter.py
```

> ⚠ Ensure the COM port inside `realtime_plotter.py` matches your device.



## Performance Metrics (Week 5 Validation)
- **Validation Source:** MIT-BIH Record 100 (Playback Mode)
- **Average Heart Rate:** ~75 BPM (consistent with dataset ground truth)
- **Processing Latency:** < 50 µs per sample (estimated on Cortex-M4F @ 64MHz)
- **CPU Idle Time:** > 95% (System sleeps between 4 ms sampling intervals)

## Embedded Design Decisions
- **Architecture:** "Super-Loop" with Interrupt Flags (Timer 1 triggers sampling; Main Loop processes data).
- **Timing:** Strict 250 Hz sampling enforced by Hardware Timer (nRF52 Timer 1).
- **Data Types:** `float` (32-bit) DSP leverage the nRF52840's hardware FPU for single-cycle math.
- **Memory Management:** Fixed-size circular buffer (`mwi_buffer`) to prevent heap fragmentation.
- **Signal Smoothing:** Exponential Moving Average (EMA, α=0.1) on BPM calculation to filter outlier beats.