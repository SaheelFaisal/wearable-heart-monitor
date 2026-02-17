import numpy as np
import wfdb
from scipy.signal import resample

# 1. Load Data (Same as your previous code)
record = wfdb.rdrecord('100', pn_dir='mitdb')
signal = record.p_signal[:, 0]
fs_original = record.fs

# 2. Resample to 250Hz
fs_target = 250
num_samples = int(len(signal) * fs_target / fs_original)
signal = resample(signal, num_samples)

# 3. CLIP & SCALE to 12-bit ADC range (0 to 4095)
# MIT-BIH data is usually centered around 0. We need it positive.
signal_norm = (signal - np.min(signal)) / (np.max(signal) - np.min(signal))
signal_adc = (signal_norm * 4000).astype(int) # Scale to 0-4000 (leave headroom)

# 4. Generate the C++ Array String
# We'll take just 5 seconds of data (5 * 250 = 1250 samples) to save memory
samples_to_take = 1250 

print(f"const int TEST_SIGNAL_LEN = {samples_to_take};")
print("const int TEST_SIGNAL[] = {")

# Print 10 numbers per line for readability
for i in range(samples_to_take):
    print(f"{signal_adc[i]}, ", end="")
    if (i + 1) % 10 == 0:
        print() # Newline

print("};")