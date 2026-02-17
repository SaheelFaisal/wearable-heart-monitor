#include <Arduino.h>
#include "test_signal.h" // Ensure this file exists in your 'include' folder

// ================================================================
// CONFIGURATION
// ================================================================
const int SAMPLE_RATE_HZ = 250;
const int SAMPLE_INTERVAL_US = 1000000 / SAMPLE_RATE_HZ; // 4000us

// --- ALGORITHM SETTINGS ---
const int MWI_WINDOW_SIZE = 37;       // 150ms window @ 250Hz
const unsigned long REFRACTORY_PERIOD_MS = 350; // Minimum time between beats

// ================================================================
// FILTER COEFFICIENTS (3rd Order Butterworth 5-15Hz @ 250Hz)
// ================================================================
const float SOS[3][6] = {
  // { b0, b1, b2, a0, a1, a2 }
  { 1.56701035e-03,  3.13402070e-03,  1.56701035e-03, 1.0, -1.73356294e+00, 7.75679511e-01 },
  { 1.00000000e+00,  0.00000000e+00, -1.00000000e+00, 1.0, -1.71760092e+00, 8.33876287e-01 },
  { 1.00000000e+00, -2.00000000e+00,  1.00000000e+00, 1.0, -1.91702538e+00, 9.33967717e-01 }
};

// ================================================================
// GLOBAL VARIABLES
// ================================================================
// Filter & Algorithm State
float w[3][2] = {0}; 
float prev_filtered_sample = 0;
float mwi_buffer[MWI_WINDOW_SIZE] = {0};
int mwi_ptr = 0;
float mwi_sum = 0;

// Peak Detection State
float threshold_i = 2000.0;    
float threshold_f = 500.0;     
float signal_peak = 0.0;
float noise_peak = 0.0;
unsigned long last_beat_time = 0; // Time of the *previous* valid beat

// BPM Calculation State
float current_bpm = 0.0;

// Timing State
unsigned long last_sample_time = 0;
int playback_index = 0; 

// --- FUNCTION PROTOTYPES ---
void processPanTompkins(int raw_input);
void detectPeak(float signal);
void calculateBPM(unsigned long now);

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  
  // Turn LED off initially (High is OFF for XIAO)
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop() {
  unsigned long current_time = micros();

  // --- STRICT TIMING LOOP (250Hz) ---
  if (current_time - last_sample_time >= SAMPLE_INTERVAL_US) {
    last_sample_time = current_time;

    // READ FROM ARRAY (Playback Mode)
    int raw_adc = TEST_SIGNAL[playback_index];

    // Loop the playback
    playback_index++;
    if (playback_index >= TEST_SIGNAL_LEN) {
      playback_index = 0;
    }

    // PROCESS SAMPLE
    processPanTompkins(raw_adc);
  }
}

// ================================================================
// CORE ALGORITHM
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

  // 5. PEAK DETECTION
  detectPeak(integrated);
}

void detectPeak(float signal) {
  unsigned long now = millis();

  // --- SERIAL OUTPUT FOR PYTHON ---
  // Format: "Signal:123,Threshold:456,BPM:72"
  Serial.print("Signal:");
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
    
    // Calculate BPM before updating time
    calculateBPM(now);
    
    last_beat_time = now;
    
    // Blink LED
    digitalWrite(LED_BUILTIN, LOW);  // ON
    delay(20);                       // Visible Blip
    digitalWrite(LED_BUILTIN, HIGH); // OFF

  } else {
    // NOISE PROCESSING
    noise_peak = signal;
    threshold_f = (0.125 * noise_peak) + (0.875 * threshold_f);
    
    // CRITICAL FIX: DECAY WITH A FLOOR
    // Decay slowly
    threshold_i *= 0.9995; 
    
    // FORCE A MINIMUM: Prevent it from dropping below 2000 (Adjust based on your plot)
    if (threshold_i < 2000.0) {
      threshold_i = 2000.0; 
    }
  }
}

void calculateBPM(unsigned long now) {
  if (last_beat_time == 0) return; // Ignore first beat (need interval)

  unsigned long delta_ms = now - last_beat_time;
  
  // Calculate Instantaneous BPM
  // 60,000 ms in a minute / ms between beats
  float instantaneous_bpm = 60000.0 / delta_ms;

  // --- DEBUG: PRINT RAW BPM ---
  Serial.print("Raw_Inst_BPM:");
  Serial.println(instantaneous_bpm); // Print this to catch the glitch

  // Simple Sanity Check (Human heart range 30-220)
  if (instantaneous_bpm > 30 && instantaneous_bpm < 240) {
    // Apply Smoothing (Running Average)
    // 10% new value, 90% old value -> Smooths out jitter
    if (current_bpm == 0) {
      current_bpm = instantaneous_bpm; // First valid reading
    } else {
      current_bpm = (0.9 * current_bpm) + (0.1 * instantaneous_bpm);
    }
  }
}