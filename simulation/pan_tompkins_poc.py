import numpy as np
import wfdb
import matplotlib.pyplot as plt
from scipy.signal import butter, filtfilt, find_peaks, resample

# --------------------------------------------------
# 1. LOAD MIT-BIH RECORD 100
# --------------------------------------------------
record = wfdb.rdrecord('100', pn_dir='mitdb')
signal = record.p_signal[:, 0]
fs_original = record.fs  # 360 Hz

# --------------------------------------------------
# 2. RESAMPLE TO 250 Hz (match your firmware)
# --------------------------------------------------
fs = 250  # New sampling frequency

num_samples = int(len(signal) * fs / fs_original)
signal = resample(signal, num_samples)

# --------------------------------------------------
# 3. BANDPASS FILTER (5–15 Hz for QRS detection)
# --------------------------------------------------
lowcut = 5.0
highcut = 15.0
order = 3

nyquist = 0.5 * fs
b, a = butter(order, [lowcut/nyquist, highcut/nyquist], btype='band')
filtered = filtfilt(b, a, signal)

# --------------------------------------------------
# 4. DERIVATIVE
# --------------------------------------------------
differentiated = np.diff(filtered)

# Pad to keep same length
differentiated = np.append(differentiated, 0)

# --------------------------------------------------
# 5. SQUARING
# --------------------------------------------------
squared = differentiated ** 2

# --------------------------------------------------
# 6. MOVING WINDOW INTEGRATION
# Window ≈ 150 ms (typical QRS duration)
# --------------------------------------------------
window_size = int(0.150 * fs)
integrated = np.convolve(
    squared,
    np.ones(window_size) / window_size,
    mode='same'
)

# --------------------------------------------------
# 7. PEAK DETECTION
# Minimum RR interval = 300 ms (max 200 BPM)
# --------------------------------------------------
min_distance = int(0.3 * fs)

threshold = np.mean(integrated) * 2.5

peaks, properties = find_peaks(
    integrated,
    distance=min_distance,
    height=threshold
)

# --------------------------------------------------
# 8. HEART RATE CALCULATION
# --------------------------------------------------
rr_intervals = np.diff(peaks) / fs  # seconds
bpm = 60 / np.mean(rr_intervals)

print(f"Average Heart Rate: {bpm:.2f} BPM")
print(f"RR Interval Std Dev: {np.std(rr_intervals):.4f} seconds")

b, a = butter(3, [5/(fs/2), 15/(fs/2)], btype='band')
print(b)
print(a)

sos = butter(3, [5/(fs/2), 15/(fs/2)], btype='band', output='sos')
print("\n\nsos\n", sos)

# --------------------------------------------------
# 9. VISUALIZATION
# --------------------------------------------------
plt.figure(figsize=(12, 8))

plt.subplot(3, 1, 1)
plt.plot(signal[:2000], color='lightgray', label="Raw (250 Hz)")
plt.plot(filtered[:2000], label="Filtered (5–15 Hz)", linewidth=1.5)
plt.legend()
plt.title("Step 1: Raw vs Bandpass")

plt.subplot(3, 1, 2)
plt.plot(integrated[:2000], color='orange', label="Integrated Signal")
valid_peaks = peaks[peaks < 2000]
plt.plot(valid_peaks, integrated[valid_peaks], "x", color='red', label="Detected R Peaks")
plt.legend()
plt.title("Step 2: Pan–Tompkins Detection")

plt.subplot(3, 1, 3)
plt.plot(peaks[:-1]/fs, rr_intervals)
plt.title("RR Intervals Over Time")
plt.xlabel("Time (seconds)")
plt.ylabel("RR Interval (s)")

plt.tight_layout()
plt.show()