import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import collections
import time

# --- CONFIGURATION ---
SERIAL_PORT = 'COM7'   # <--- CHECK YOUR PORT
BAUD_RATE = 115200
PLOT_WINDOW = 500      # How many samples to show

# --- DATA BUFFERS ---
signal_data = collections.deque([0] * PLOT_WINDOW, maxlen=PLOT_WINDOW)
threshold_data = collections.deque([0] * PLOT_WINDOW, maxlen=PLOT_WINDOW)

# --- SETUP PLOT ---
fig, ax = plt.subplots(figsize=(10, 6))
line_signal, = ax.plot([], [], label='Integrated Signal', color='blue', linewidth=1.5)
line_threshold, = ax.plot([], [], label='Adaptive Threshold', color='orange', linestyle='--', linewidth=1.5)

# Style the plot
ax.set_xlim(0, PLOT_WINDOW)
ax.set_ylim(0, 4000) # Initial guess, it will auto-scale
ax.legend(loc='upper right')
ax.set_title("Waiting for Data...", fontsize=14)
ax.set_ylabel("Amplitude")
ax.grid(True, alpha=0.3)

# Global variables for the title
current_bpm = 0.0
last_raw_bpm = 0.0

# --- SERIAL CONNECTION ---
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
    print(f"Connected to {SERIAL_PORT}")
    print("Watching for 'Raw_Inst_BPM' glitches... (Keep this window open)")
except Exception as e:
    print(f"Error opening serial port: {e}")
    exit()

def update(frame):
    global current_bpm, last_raw_bpm
    
    try:
        # Read all waiting lines to clear buffer (keeps plot real-time)
        while ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            # --- TYPE 1: STANDARD STREAM (Signal:..., Threshold:..., BPM:...) ---
            if line.startswith("Signal:"):
                parts = line.split(',')
                if len(parts) >= 3:
                    # Parse Data
                    sig = float(parts[0].split(':')[1])
                    thr = float(parts[1].split(':')[1])
                    bpm = float(parts[2].split(':')[1])
                    
                    # Update Buffers
                    signal_data.append(sig)
                    threshold_data.append(thr)
                    current_bpm = bpm

            # --- TYPE 2: DEBUG EVENT (Raw_Inst_BPM:...) ---
            elif line.startswith("Raw_Inst_BPM:"):
                val = float(line.split(':')[1])
                last_raw_bpm = val
                print(f" >> BEAT DETECTED! Raw Instant BPM: {val:.1f}") 
                # ^^^ Look at this in the terminal! 

        # --- UPDATE PLOT ---
        line_signal.set_ydata(signal_data)
        line_signal.set_xdata(range(len(signal_data)))
        line_threshold.set_ydata(threshold_data)
        line_threshold.set_xdata(range(len(threshold_data)))
        
        # Dynamic Title
        ax.set_title(f"Pan-Tompkins Algorithm\nAvg BPM: {current_bpm:.1f}  |  Last Raw: {last_raw_bpm:.1f}", fontsize=14)

        # Optional: Auto-scale Y-Axis slowly
        if len(signal_data) > 10:
            peak = max(max(signal_data), max(threshold_data))
            if peak > ax.get_ylim()[1] or peak < ax.get_ylim()[1] * 0.5:
                 ax.set_ylim(0, max(2000, peak * 1.2))

    except Exception as e:
        pass 

    return line_signal, line_threshold

# --- RUN ---
ani = FuncAnimation(fig, update, interval=20, blit=False)
plt.show()
ser.close()