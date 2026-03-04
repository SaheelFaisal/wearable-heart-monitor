import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import collections

# --- CONFIGURATION ---
SERIAL_PORT = 'COM4'   # <--- DOUBLE CHECK THIS IN PLATFORMIO
BAUD_RATE = 115200
PLOT_WINDOW = 500      

# --- DATA BUFFERS ---
raw_data = collections.deque([0] * PLOT_WINDOW, maxlen=PLOT_WINDOW)
signal_data = collections.deque([0] * PLOT_WINDOW, maxlen=PLOT_WINDOW)
threshold_data = collections.deque([0] * PLOT_WINDOW, maxlen=PLOT_WINDOW)

# --- SETUP PLOTS (2 Vertical Subplots) ---
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
fig.tight_layout(pad=4.0)

# Top Plot: Raw Signal
line_raw, = ax1.plot([], [], label='Raw ECG Signal', color='green', linewidth=1.5)
ax1.set_xlim(0, PLOT_WINDOW)
ax1.set_ylim(0, 4096)  
ax1.legend(loc='upper right')
ax1.set_title("Raw Sensor Input", fontsize=12)
ax1.set_ylabel("ADC Value")
ax1.grid(True, alpha=0.3)

# Bottom Plot: Integrated Signal & Adaptive Threshold
line_signal, = ax2.plot([], [], label='Integrated Signal', color='blue', linewidth=1.5)
line_threshold, = ax2.plot([], [], label='Adaptive Threshold', color='orange', linestyle='--', linewidth=1.5)
ax2.set_xlim(0, PLOT_WINDOW)
ax2.set_ylim(0, 4000) 
ax2.legend(loc='upper right')
ax2.set_title("Pan-Tompkins Algorithm State", fontsize=12)
ax2.set_ylabel("Amplitude")
ax2.set_xlabel("Samples")
ax2.grid(True, alpha=0.3)

current_bpm = 0.0

# --- SERIAL CONNECTION ---
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
    print(f"Connected to {SERIAL_PORT}")
    print("Listening for Telemetry... (Keep this window open)")
except Exception as e:
    print(f"Error opening serial port: {e}")
    exit()

def update(frame):
    global current_bpm
    
    try:
        while ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            # --- DEBUG PRINT ---
            # This will show you exactly what Python is hearing!
            print(f"MCU Says: {line}") 
            
            if line.startswith("Raw:"):
                parts = line.split(',')
                if len(parts) >= 4:
                    raw_val = float(parts[0].split(':')[1])
                    sig_val = float(parts[1].split(':')[1])
                    thr_val = float(parts[2].split(':')[1])
                    bpm_val = float(parts[3].split(':')[1])
                    
                    raw_data.append(raw_val)
                    signal_data.append(sig_val)
                    threshold_data.append(thr_val)
                    current_bpm = bpm_val

        # --- UPDATE PLOTS ---
        line_raw.set_ydata(raw_data)
        line_raw.set_xdata(range(len(raw_data)))
        
        line_signal.set_ydata(signal_data)
        line_signal.set_xdata(range(len(signal_data)))
        
        line_threshold.set_ydata(threshold_data)
        line_threshold.set_xdata(range(len(threshold_data)))
        
        fig.suptitle(f"Real-Time ECG Telemetry  |  Avg BPM: {current_bpm:.1f}", fontsize=16, fontweight='bold')

        if len(signal_data) > 10:
            peak = max(max(signal_data), max(threshold_data))
            if peak > ax2.get_ylim()[1] or peak < ax2.get_ylim()[1] * 0.5:
                 ax2.set_ylim(0, max(2000, peak * 1.2))

    except Exception as e:
        print(f"Data parsing error: {e}") # Unmasking the silent errors
        pass 

    return line_raw, line_signal, line_threshold

# --- RUN ---
# Added cache_frame_data=False to fix the Matplotlib warning
ani = FuncAnimation(fig, update, interval=20, blit=False, cache_frame_data=False)
plt.show()
ser.close()