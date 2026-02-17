#include <Arduino.h>

#define ADC_PIN A0            // Analog pin connected to the sensor
#define SAMPLE_RATE_HZ 250    
#define BUFFER_SIZE 500       

volatile uint16_t adcBuffer[BUFFER_SIZE];
volatile uint16_t bufferIndex = 0;
volatile uint32_t timeBuffer[BUFFER_SIZE];
volatile bool bufferFull = false;



// ADC sampling function
void sampleADC() {
  uint32_t now = micros();     // timestamp in μs
  uint16_t sample = analogRead(ADC_PIN);

  adcBuffer[bufferIndex] = sample;
  timeBuffer[bufferIndex] = now;

  bufferIndex++;
  if (bufferIndex >= BUFFER_SIZE) {
    bufferIndex = 0;
    bufferFull = true;
  }
}


void setup() {
  Serial.begin(115200);
  pinMode(ADC_PIN, INPUT);
  analogReadResolution(12); // 12-bit ADC

  // Configure Timer1 for 250 Hz interrupts
  NRF_TIMER1->MODE      = TIMER_MODE_MODE_Timer;   // Timer mode
  NRF_TIMER1->BITMODE   = TIMER_BITMODE_BITMODE_16Bit; // 16-bit timer
  NRF_TIMER1->PRESCALER = 4;  // 16 MHz / 2^4 = 1 MHz timer frequency
  NRF_TIMER1->CC[0]     = 4000; // 1 MHz / 4000 = 250 Hz
  NRF_TIMER1->SHORTS    = TIMER_SHORTS_COMPARE0_CLEAR_Enabled << TIMER_SHORTS_COMPARE0_CLEAR_Pos;
  NRF_TIMER1->INTENSET  = TIMER_INTENSET_COMPARE0_Set << TIMER_INTENSET_COMPARE0_Pos;
  
  NVIC_EnableIRQ(TIMER1_IRQn);   // Enable interrupt in NVIC
  NRF_TIMER1->TASKS_START = 1;   // Start timer
}

// Timer1 interrupt handler
extern "C" void TIMER1_IRQHandler() {
  if (NRF_TIMER1->EVENTS_COMPARE[0]) {
    NRF_TIMER1->EVENTS_COMPARE[0] = 0;
    sampleADC();
  }
}

void loop() {
  // Printing samples
  if (bufferFull) {
    noInterrupts();
    bufferFull = false;
    interrupts();

    for (int i = 0; i < BUFFER_SIZE; i++) {
      Serial.print(timeBuffer[i]);
      Serial.print(",");
      Serial.println(adcBuffer[i]);
    }
    Serial.println("END");
  }
}

