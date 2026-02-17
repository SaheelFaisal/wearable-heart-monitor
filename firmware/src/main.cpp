#include <Arduino.h>
#include "test_signal.h"

// ================================================================
// CONFIGURATION
// ================================================================
const int SAMPLE_RATE_HZ = 250;
const int SAMPLE_INTERVAL_US = 1000000 / SAMPLE_RATE_HZ; // 4000us

// --- ALGORITHM SETTINGS ---
// Window size for Moving Window Integration
// 150ms window @ 250Hz = ~37 samples
const int MWI_WINDOW_SIZE = 37;

// Refractory Period: Minimum time between valid beats (physiologically impossible <200ms)
const unsigned long REFRACTORY_PERIOD_MS = 250; 

// ================================================================
// FILTER COEFFICIENTS (3rd Order Butterworth 5-15Hz @ 250Hz)
// ================================================================
// These are "Second Order Sections" (SOS) for stability on microcontrollers.
// Generated from Python: scipy.signal.butter(3, [5, 15], btype='band', fs=250, output='sos')
const float SOS[3][6] = {
  // { b0, b1, b2, a0, a1, a2 }
  // Note: a0 is always 1.0 and is ignored in the logic, but kept here for alignment.
  
  // Section 1
  { 1.56701035e-03,  3.13402070e-03,  1.56701035e-03, 1.0, -1.73356294e+00, 7.75679511e-01 },
  
  // Section 2
  { 1.00000000e+00,  0.00000000e+00, -1.00000000e+00, 1.0, -1.71760092e+00, 8.33876287e-01 },
  
  // Section 3
  { 1.00000000e+00, -2.00000000e+00,  1.00000000e+00, 1.0, -1.91702538e+00, 9.33967717e-01 }
};

// ================================================================
// GLOBAL VARIABLES (STATE MEMORY)
// ================================================================
// Filter Delay Lines (w[section][delay_index])
float w[3][2] = {0}; 

// Derivative State
float prev_filtered_sample = 0;

// Moving Window Integration State (Circular Buffer)
float mwi_buffer[MWI_WINDOW_SIZE] = {0};
int mwi_ptr = 0;
float mwi_sum = 0;

// Peak Detection State
float threshold_i = 2000.0;    // Signal Threshold (Adaptive)
float threshold_f = 500.0;     // Noise Threshold
float signal_peak = 0.0;
float noise_peak = 0.0;
unsigned long last_beat_time = 0;

// Timing State
unsigned long last_sample_time = 0;

int playback_index = 0; // Tracks where we are in the array

// --- FUNCTION PROTOTYPES (Required in PlatformIO) ---
void processPanTompkins(int raw_input);
void detectPeak(float signal);


void setup() {
  Serial.begin(115200);
  // pinMode(A0, INPUT);      // Signal Input
  pinMode(LED_BUILTIN, OUTPUT); // Beat Indicator
  
  // Optional: Wait for serial to open (good for debugging, bad for battery)
  // while (!Serial); 
}

void loop() {
  unsigned long current_time = micros();

  // --- STRICT TIMING LOOP (Simulated 250Hz Interrupt) ---
  if (current_time - last_sample_time >= SAMPLE_INTERVAL_US) {
    last_sample_time = current_time;

    // // 1. READ ADC
    // int raw_adc = analogRead(A0);
    
    // --- CHANGED: READ FROM ARRAY INSTEAD OF PIN ---
    int raw_adc = TEST_SIGNAL[playback_index];

    // Increment index and loop back to start (Infinite Heartbeat)
    playback_index++;
    if (playback_index >= TEST_SIGNAL_LEN) {
      playback_index = 0;
    }

    // 2. PROCESS SAMPLE
    processPanTompkins(raw_adc);
  }
}


// ================================================================
// CORE ALGORITHM
// ================================================================
void processPanTompkins(int raw_input) {
  
  // --- STEP 1: BANDPASS FILTER (IIR SOS) ---
  float x = (float)raw_input;
  
  // Cascade the 3 SOS sections
  for (int i = 0; i < 3; i++) {
    float b0 = SOS[i][0];
    float b1 = SOS[i][1];
    float b2 = SOS[i][2];
    float a1 = SOS[i][4]; // Python a1
    float a2 = SOS[i][5]; // Python a2

    // Direct Form II Transposed Difference Equation
    float y = b0 * x + w[i][0];
    
    // Update delay lines
    w[i][0] = b1 * x - a1 * y + w[i][1];
    w[i][1] = b2 * x - a2 * y;
    
    x = y; // Output of this section becomes input to the next
  }
  float filtered = x;


  // --- STEP 2: DERIVATIVE (Approximation) ---
  // y[n] = x[n] - x[n-1]
  // This emphasizes the steep slope of the QRS complex
  float derivative = filtered - prev_filtered_sample;
  prev_filtered_sample = filtered;


  // --- STEP 3: SQUARING ---
  // y[n] = x[n]^2
  // Makes everything positive and amplifies the high-slope QRS
  float squared = derivative * derivative;


  // --- STEP 4: MOVING WINDOW INTEGRATION ---
  // Efficient "Running Sum" implementation
  mwi_sum -= mwi_buffer[mwi_ptr];       // Subtract oldest sample
  mwi_buffer[mwi_ptr] = squared;        // Overwrite with new sample
  mwi_sum += squared;                   // Add new sample
  
  mwi_ptr++;                            // Advance pointer
  if (mwi_ptr >= MWI_WINDOW_SIZE) {
    mwi_ptr = 0;                        // Wrap around
  }

  // The output is the average over the window
  float integrated = mwi_sum / MWI_WINDOW_SIZE;


  // --- STEP 5: PEAK DETECTION & THRESHOLDING ---
  detectPeak(integrated);
}


void detectPeak(float signal) {
  unsigned long now = millis();

  // PLOTTING: Print values to Serial Plotter
  // Format: "Label1:Value1, Label2:Value2"
  Serial.print("Signal:");
  Serial.print(signal);
  Serial.print(",");
  Serial.print("Threshold:");
  Serial.println(threshold_i);

  // 1. Refractory Period Check
  if (now - last_beat_time < REFRACTORY_PERIOD_MS) {
    return; // Ignore noise immediately after a beat
  }

  // 2. Identify Peaks
  // Simple logic: If signal crosses threshold, it's a candidate
  if (signal > threshold_i) {
    
    // We found a QRS Complex!
    signal_peak = signal;
    threshold_i = (0.125 * signal_peak) + (0.875 * threshold_i); // Adjust threshold up
    
    last_beat_time = now;
    
    // ACTION: Blink LED
    digitalWrite(LED_BUILTIN, LOW);  // ON (Active Low for XIAO)
    delay(10);                       // Short blip
    digitalWrite(LED_BUILTIN, HIGH); // OFF
    
    // (Optional) Print "Beat" marker for plotter
    // Serial.println("BEAT_DETECTED:10000"); 

  } else {
    // If it's noise (not a peak but has some energy)
    // Adjust noise level estimate
    noise_peak = signal;
    threshold_f = (0.125 * noise_peak) + (0.875 * threshold_f);
    
    // Decay the threshold slowly back down if we miss beats
    // This ensures we don't get stuck with a high threshold forever
    threshold_i *= 0.9995; 
  }
}