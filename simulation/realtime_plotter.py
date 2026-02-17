import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import collections

# --- CONFIGURATION ---
SERIAL_PORT = 'COM4'   # <--- CHANGE THIS to your actual port!
BAUD_RATE = 115200
PLOT_WINDOW = 500      # How many samples to show on screen

# --- SETUP DATA BUFFERS ---
signal_data = collections.deque([0] * PLOT_WINDOW, maxlen=PLOT_WINDOW)
threshold_data = collections.deque([0] * PLOT_WINDOW, maxlen=PLOT_WINDOW)

# --- SETUP PLOT ---
fig, ax = plt.subplots()
line_signal, = ax.plot([], [], label='Integrated Signal', color='blue')
line_threshold, = ax.plot([], [], label='Adaptive Threshold', color='orange', linestyle='--')

ax.set_xlim(0, PLOT_WINDOW)
ax.set_ylim(0, 4000) # Adjust this if your signal is huge or tiny
ax.legend(loc='upper right')
ax.set_title("Real-Time Pan-Tompkins Visualization")
ax.grid(True)

# --- SERIAL CONNECTION ---
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE)
    print(f"Connected to {SERIAL_PORT}")
except Exception as e:
    print(f"Error opening serial port: {e}")
    exit()

def update(frame):
    try:
        # Read a line from Serial: "Signal:123.45,Threshold:500.00"
        if ser.in_waiting:
            line = ser.readline().decode('utf-8').strip()
            
            # Simple parsing
            parts = line.split(',')
            if len(parts) >= 2:
                # Extract values (Assume format "Label:Value")
                sig_val = float(parts[0].split(':')[1])
                thr_val = float(parts[1].split(':')[1])
                
                # Add to buffers
                signal_data.append(sig_val)
                threshold_data.append(thr_val)
                
                # Update lines
                line_signal.set_ydata(signal_data)
                line_signal.set_xdata(range(len(signal_data)))
                line_threshold.set_ydata(threshold_data)
                line_threshold.set_xdata(range(len(threshold_data)))
                
                # Auto-scale Y-axis if needed (optional)
                # current_max = max(max(signal_data), max(threshold_data))
                # ax.set_ylim(0, current_max * 1.2)

    except ValueError:
        pass # Ignore bad data packets
    except Exception as e:
        print(f"Error: {e}")

    return line_signal, line_threshold

# --- RUN ANIMATION ---
ani = FuncAnimation(fig, update, interval=10, blit=True)
plt.show()

# Close serial on exit
ser.close()