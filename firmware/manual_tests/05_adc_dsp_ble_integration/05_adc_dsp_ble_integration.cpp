#include <Arduino.h>
#include "mbed.h"
#include <ArduinoBLE.h> // <--- ADDED BLE LIBRARY

// ================================================================
// HARDWARE & BLE CONFIGURATION
// ================================================================
#define ADC_PIN A0            

// BLE GATT Server Setup
BLEService heartRateService("180D");
BLECharacteristic heartRateMeasurement("2A37", BLENotify, 2);

// Global Flags for Interrupt
volatile bool data_ready = false;
volatile int latest_sample = 0;

// ================================================================
// ALGORITHM CONFIGURATION
// ================================================================
const int MWI_WINDOW_SIZE = 37;       
const unsigned long REFRACTORY_PERIOD_MS = 350; 

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

// Function Prototypes
void processPanTompkins(int raw_input);
void detectPeak(float signal, int raw_signal);
void calculateBPM(unsigned long now);

mbed::Ticker sample_ticker;

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

  // --- 1. INITIALIZE BLE ---
  if (!BLE.begin()) {
    Serial.println("Failed to start BLE!");
    while (1); // Halt if radio fails
  }

  // REQUEST LOW LATENCY (< 30ms)
  // Parameters are in 1.25ms units (12 * 1.25 = 15ms, 24 * 1.25 = 30ms)
  BLE.setConnectionInterval(12, 24);

  BLE.setDeviceName("ECG HEART RATE MONITOR");
  BLE.setLocalName("ECG HEART RATE MONITOR");
  BLE.setAdvertisedService(heartRateService);
  heartRateService.addCharacteristic(heartRateMeasurement);
  BLE.addService(heartRateService);
  
  // Set initial dummy value
  uint8_t hrData[2] = {0x00, 0};
  heartRateMeasurement.writeValue(hrData, 2);
  
  BLE.advertise();
  Serial.println("--- BLE ADVERTISING STARTED ---");

  // --- 2. START THE HARDWARE TICKER ---
  sample_ticker.attach(&timer_isr, 0.004); 
  Serial.println("--- mbed RTOS TICKER STARTED (250Hz) ---");
}

// ================================================================
// MAIN LOOP
// ================================================================
void loop() {
  // Let the BLE library handle background connections and polling automatically
  BLE.poll();

  if (data_ready) {
    data_ready = false; 
    
    int raw_val = analogRead(ADC_PIN);
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

  // 5. PEAK DETECTION
  detectPeak(integrated, raw_input);
}

void detectPeak(float signal, int raw_signal) {
  unsigned long now = millis();

  if (now - last_beat_time < REFRACTORY_PERIOD_MS) {
    return; 
  }

  if (signal > threshold_i) {
    signal_peak = signal;
    threshold_i = (0.125 * signal_peak) + (0.875 * threshold_i); 
    
    // --- VALID BEAT DETECTED ---
    calculateBPM(now);
    last_beat_time = now;

    // ONLY print to Serial when a beat happens! (Saves massive CPU time)
    Serial.print("BEAT DETECTED! Current BPM: ");
    Serial.println(current_bpm);

  } else {
    noise_peak = signal;
    threshold_f = (0.125 * noise_peak) + (0.875 * threshold_f);
    threshold_i *= 0.9995; 
    
    if (threshold_i < 2000.0) {
      threshold_i = 2000.0; 
    }
  }
}

void calculateBPM(unsigned long now) {
  if (last_beat_time == 0) return; 

  unsigned long delta_ms = now - last_beat_time;
  float instantaneous_bpm = 60000.0 / delta_ms;

  if (instantaneous_bpm > 30 && instantaneous_bpm < 240) {
    if (current_bpm == 0) {
      current_bpm = instantaneous_bpm; 
    } else {
      current_bpm = (0.9 * current_bpm) + (0.1 * instantaneous_bpm);
    }

    // ================================================================
    // BLE NOTIFICATION (Pushing live data to the phone)
    // ================================================================
    BLEDevice central = BLE.central();
    if (central && central.connected()) {
      uint8_t hrData[2] = {0x00, (uint8_t)current_bpm};
      heartRateMeasurement.writeValue(hrData, 2);
    }
  }
}