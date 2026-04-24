#include <Arduino.h>
#include "mbed.h"
#include <ArduinoBLE.h> 

// ================================================================
// HARDWARE PINS
// ================================================================
#define ADC_PIN A0            

// Seeed Studio XIAO nRF52840 Internal Battery Pins
#define VBAT_ENABLE 14 // P0.14 turns on the voltage divider
#define PIN_VBAT 32    // P0.31 reads the divided voltage

// ================================================================
// BLE CONFIGURATION
// ================================================================
// 1. Standard Heart Rate Service (BPM)
BLEService heartRateService("180D");
BLECharacteristic heartRateMeasurement("2A37", BLENotify, 2);

// 2. Custom ECG Waveform Service (Live Signal + R-Peak Flag)
BLEService ecgService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLECharacteristic ecgWaveformChar("19B10001-E8F2-537E-4F6C-D104768A1214", BLENotify, 20); // 10 samples * 2 bytes

// 3. Standard Battery Service (Percentage)
BLEService batteryService("180F");
BLEUnsignedCharCharacteristic batteryLevelChar("2A19", BLERead | BLENotify);

// ================================================================
// GLOBAL BUFFERS & FLAGS
// ================================================================
volatile bool data_ready = false;

// Waveform Buffer
#define BLE_BUFFER_SIZE 10
int16_t ble_buffer[BLE_BUFFER_SIZE];
int ble_buffer_idx = 0;
bool is_peak_detected = false; 

// Battery Timer
unsigned long last_battery_check = 0;

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

// Algorithm State Variables
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

// Hardware Timer
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

  // 1. Initialize Analog and Battery Pins
  pinMode(ADC_PIN, INPUT);
  analogReadResolution(12); 
  
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW); // Enable the battery monitoring circuit

  // 2. Initialize BLE
  if (!BLE.begin()) {
    Serial.println("Failed to start BLE!");
    while (1); 
  }

  // Request Low Latency connection intervals (15ms - 30ms)
  BLE.setConnectionInterval(12, 24);
  BLE.setDeviceName("ECG HEART RATE MONITOR");
  BLE.setLocalName("ECG HEART RATE MONITOR");
  
  // Set up standard HR Service
  BLE.setAdvertisedService(heartRateService);
  heartRateService.addCharacteristic(heartRateMeasurement);
  BLE.addService(heartRateService);
  
  // Set up Custom ECG Waveform Service
  ecgService.addCharacteristic(ecgWaveformChar);
  BLE.addService(ecgService);

  // Set up Standard Battery Service
  batteryService.addCharacteristic(batteryLevelChar);
  BLE.addService(batteryService);
  
  // Initialize default characteristics
  uint8_t hrData[2] = {0x00, 0};
  heartRateMeasurement.writeValue(hrData, 2);
  batteryLevelChar.writeValue(100); 
  
  BLE.advertise();
  Serial.println("BLE ADVERTISING STARTED");

  // 3. Start the Hardware Ticker (250Hz -> 4ms)
  sample_ticker.attach(&timer_isr, 0.004); 
}

// ================================================================
// MAIN LOOP
// ================================================================
void loop() {
  // Let the BLE library handle background connections and polling
  BLE.poll();

  unsigned long current_time = millis();

  // --- NON-BLOCKING BATTERY CHECK (Every 10 seconds) ---
    if (current_time - last_battery_check > 10000) {
        last_battery_check = current_time;

        int vbat_raw = analogRead(PIN_VBAT);
        float battery_voltage = (vbat_raw * 9.0) / 4096.0;
        
        int battery_percent = 0;

        // Piecewise approximation for LiPo discharge curve
        if (battery_voltage >= 4.20) {
        battery_percent = 100;
        } else if (battery_voltage > 3.80) {
        // Upper range: 4.2V to 3.8V represents ~100% down to 40%
        battery_percent = map(battery_voltage * 100, 380, 420, 40, 100);
        } else if (battery_voltage > 3.70) {
        // The drop-off: 3.8V to 3.7V represents ~40% down to 20%
        battery_percent = map(battery_voltage * 100, 370, 380, 20, 40);
        } else if (battery_voltage > 3.30) {
        // The cliff: 3.7V to 3.3V represents ~20% down to 0%
        battery_percent = map(battery_voltage * 100, 330, 370, 0, 20);
        } else {
        battery_percent = 0;
        }

        BLEDevice central = BLE.central();
        if (central && central.connected()) {
        batteryLevelChar.writeValue((uint8_t)battery_percent);
        }
    }


  // --- HIGH SPEED DSP LOOP (Triggered at 250Hz by Ticker) ---
  if (data_ready) {
    data_ready = false; 
    
    int raw_val = analogRead(ADC_PIN);
    
    // Reset peak flag for this specific sample
    is_peak_detected = false;
    
    // Process math (changes is_peak_detected to true if an R-peak occurs)
    processPanTompkins(raw_val);
    
    // Pack data: 12-bit ADC value in lower bits, Peak flag in Bit 15
    int16_t packed_data = (raw_val & 0x0FFF); 
    if (is_peak_detected) {
      packed_data |= 0x8000; 
    }
    
    // Add to buffer
    ble_buffer[ble_buffer_idx] = packed_data;
    ble_buffer_idx++;
    
    // Send buffer if full (every 10 samples)
    if (ble_buffer_idx >= BLE_BUFFER_SIZE) {
      BLEDevice central = BLE.central();
      if (central && central.connected()) {
        ecgWaveformChar.writeValue((uint8_t*)ble_buffer, sizeof(ble_buffer));
      }
      ble_buffer_idx = 0; // Reset index
    }
  }
}

// ================================================================
// ALGORITHM IMPLEMENTATION (Pan-Tompkins)
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
    is_peak_detected = true; 
    
    calculateBPM(now);
    last_beat_time = now;

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

    // Push live BPM to the phone
    BLEDevice central = BLE.central();
    if (central && central.connected()) {
      uint8_t hrData[2] = {0x00, (uint8_t)current_bpm};
      heartRateMeasurement.writeValue(hrData, 2);
    }
  }
}