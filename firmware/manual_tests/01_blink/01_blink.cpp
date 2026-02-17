#include <Arduino.h>


void setup() {
  // 1. Setup the LED pin
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  // 2. Turn them all off initially (HIGH = OFF)
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  // 3. Initialize Serial (Good to test if your USB driver works)
  Serial.begin(115200);
}

void loop() {
  Serial.println("Blink!"); // Check your 'Serial Monitor' to see this

  // Red ON
  digitalWrite(LED_RED, LOW);
  delay(500);
  
  // Red OFF
  digitalWrite(LED_RED, HIGH);
  delay(500);
  
  // Green ON
  digitalWrite(LED_GREEN, LOW);
  delay(500);

  // Green OFF
  digitalWrite(LED_GREEN, HIGH);
  delay(500);
}