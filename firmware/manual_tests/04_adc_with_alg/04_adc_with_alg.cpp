#include <Arduino.h>
#include "mbed.h"  // <--- ADD THIS: Brings in the RTOS timer libraries

// ================================================================
// HARDWARE CONFIGURATION
// ================================================================
#define ADC_PIN A0            // Pin connected to Custom AFE (InAmp Output)

// Global Flags for Interrupt Communication
volatile bool data_ready = false;
volatile int latest_sample = 0;

// ================================================================
// ALGORITHM CONFIGURATION
// ================================================================
const int MWI_WINDOW_SIZE = 37;       // 150ms window @ 250Hz
const unsigned long REFRACTORY_PERIOD_MS = 350; // Minimum time between beats

// ================================================================
// FILTER COEFFICIENTS (3rd Order Butterworth 5-15Hz @ 250Hz)
// ================================================================
const float SOS[3][6] = {
  { 1.56701035e-03,  3.13402070e-03,  1.56701035e-03, 1.0, -1.73356294e+00, 7.75679511e-01 },
  { 1.00000000e+00,  0.00000000e+00, -1.00000000e+00, 1.0, -1.71760092e+00, 8.33876287e-01 },
  { 1.00000000e+00, -2.00000000e+00,  1.00000000e+00, 1.0, -1.91702538e+00, 9.33967717e-01 }
};

// ================================================================
// GLOBAL VARIABLES (Algorithm State)
// ================================================================
float w[3][2] = {0}; 
float prev_filtered_sample = 0;

float mwi_buffer[MWI_WINDOW_SIZE] = {0};
int mwi_ptr = 0;
float mwi_sum = 0;

float threshold_i = 2000.0;    
float threshold_f = 500.0;     
float signal_peak = 0.0;
float noise_peak = 0.0;
unsigned long last_beat_time = 0;

float current_bpm = 0.0;

// --- FUNCTION PROTOTYPES ---
void processPanTompkins(int raw_input);
void detectPeak(float signal, int raw_signal);
void calculateBPM(unsigned long now);

// Create the RTOS Hardware Ticker Object
mbed::Ticker sample_ticker;

// ================================================================
// INTERRUPT SERVICE ROUTINE (Runs exactly every 4ms)
// ================================================================
// Notice it is no longer an "extern C" handler, just a standard function
void timer_isr() {
  data_ready = true; 
}

// ================================================================
// SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(2000); 
  Serial.println("\n--- MCU BOOTING UP ---");

  pinMode(ADC_PIN, INPUT);
  analogReadResolution(12); 

  // --- START THE HARDWARE TICKER ---
  // 0.004 seconds = 4 milliseconds = 250Hz
  sample_ticker.attach(&timer_isr, 0.004); 
  
  Serial.println("--- mbed RTOS TICKER STARTED (250Hz) ---");
}

// ================================================================
// MAIN LOOP
// ================================================================
void loop() {
  // Wait for the hardware timer to give us the green light
  if (data_ready) {
    data_ready = false; // Reset flag immediately
    
    // 1. Read Sensor Safely Outside the ISR
    int raw_val = analogRead(ADC_PIN);

    // 2. Run the DSP Algorithm
    processPanTompkins(raw_val);
  }
}

// ================================================================
// ALGORITHM IMPLEMENTATION
// ================================================================
void processPanTompkins(int raw_input) {
  
  // 1. BANDPASS FILTER
  float x = (float)raw_input;
  for (int i = 0; i < 3; i++) {
    float b0 = SOS[i][0]; float b1 = SOS[i][1]; float b2 = SOS[i][2];
    float a1 = SOS[i][4]; float a2 = SOS[i][5];

    float y = b0 * x + w[i][0];
    w[i][0] = b1 * x - a1 * y + w[i][1];
    w[i][1] = b2 * x - a2 * y;
    x = y; 
  }
  float filtered = x;

  // 2. DERIVATIVE
  float derivative = filtered - prev_filtered_sample;
  prev_filtered_sample = filtered;

  // 3. SQUARING
  float squared = derivative * derivative;

  // 4. MOVING WINDOW INTEGRATION
  mwi_sum -= mwi_buffer[mwi_ptr];
  mwi_buffer[mwi_ptr] = squared;
  mwi_sum += squared;
  
  mwi_ptr++;
  if (mwi_ptr >= MWI_WINDOW_SIZE) mwi_ptr = 0;

  float integrated = mwi_sum / MWI_WINDOW_SIZE;

  // 5. PEAK DETECTION (Passing raw signal for telemetry)
  detectPeak(integrated, raw_input);
}

void detectPeak(float signal, int raw_signal) {
  unsigned long now = millis();

  // --- SERIAL OUTPUT FOR PYTHON PLOTTER ---
  Serial.print("Raw:");
  Serial.print(raw_signal);
  Serial.print(",");
  Serial.print("Integrated:");
  Serial.print(signal);
  Serial.print(",");
  Serial.print("Threshold:");
  Serial.print(threshold_i);
  Serial.print(",");
  Serial.print("BPM:");
  Serial.println(current_bpm);

  // 1. Refractory Period Check
  if (now - last_beat_time < REFRACTORY_PERIOD_MS) {
    return; 
  }

  // 2. Identify Peaks
  if (signal > threshold_i) {
    
    // VALID BEAT DETECTED
    signal_peak = signal;
    threshold_i = (0.125 * signal_peak) + (0.875 * threshold_i); 
    
    // Calculate BPM
    calculateBPM(now);
    last_beat_time = now;

  } else {
    // NOISE PROCESSING
    noise_peak = signal;
    threshold_f = (0.125 * noise_peak) + (0.875 * threshold_f);
    
    // Decay Threshold
    threshold_i *= 0.9995; 
    
    // FLOOR CLAMP
    if (threshold_i < 2000.0) {
      threshold_i = 2000.0; 
    }
  }
}

void calculateBPM(unsigned long now) {
  if (last_beat_time == 0) return; // Ignore first beat

  unsigned long delta_ms = now - last_beat_time;
  float instantaneous_bpm = 60000.0 / delta_ms;

  // Sanity Check (30-240 BPM)
  if (instantaneous_bpm > 30 && instantaneous_bpm < 240) {
    if (current_bpm == 0) {
      current_bpm = instantaneous_bpm; 
    } else {
      current_bpm = (0.9 * current_bpm) + (0.1 * instantaneous_bpm);
    }
  }
}