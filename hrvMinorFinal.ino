/*
 * FINAL STABLE ANXIETY MONITOR (Anti-Flicker Edition)
 * Developers: Vishwas Paliwal, Sumit Yaduwanshi, Nikhil Misal, Md. Talha Khan
 */

#define BLYNK_TEMPLATE_ID "[TEMPLATE]"
#define BLYNK_TEMPLATE_NAME "HRV Anxiety Monitor"
#define BLYNK_AUTH_TOKEN "[TOKEN]"
#define BLYNK_PRINT Serial

#include <Wire.h>
#include "MAX30105.h"
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// WiFi Credentials
char ssid[] = "[HOTSPOT_NAME]";        
char pass[] = "[HOTSPOT_PASS]";        

// Hardware Pins
#define SDA_PIN 21
#define SCL_PIN 22
#define BUZZER_PIN 26

// OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Virtual Pins
#define VPIN_HRV V0       
#define VPIN_BPM V1       
#define VPIN_STATUS V2     
#define VPIN_CALIBRATE V3  

// ========== BITMAPS ==========
const unsigned char PROGMEM heart_small[] = {
  0x00, 0x00, 0x00, 0x00, 0x06, 0x60, 0x0F, 0xF0, 0x1F, 0xF8, 0x3F, 0xFC, 0x3F, 0xFC, 0x3F, 0xFC, 
  0x1F, 0xF8, 0x0F, 0xF0, 0x07, 0xE0, 0x03, 0xC0, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char PROGMEM heart_big[] = {
  0x00, 0x00, 0x0C, 0x30, 0x1F, 0xF8, 0x3F, 0xFC, 0x7F, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE, 
  0x3F, 0xFC, 0x1F, 0xF8, 0x0F, 0xF0, 0x07, 0xE0, 0x03, 0xC0, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00
};

// ========== GLOBAL VARIABLES ==========
MAX30105 particleSensor;

const int MIN_IBI_MS = 300;   
const int MAX_IBI_MS = 2000;  
const float DEFAULT_ANXIETY_THRESHOLD = 20.0; 
const int SAMPLE_SIZE = 20;   
const int FINGER_THRESHOLD = 10000; 
const int BEAT_TRIGGER = 12000;     

long lastBeatTime = 0;        
long irValue = 0;              
float ibiHistory[SAMPLE_SIZE]; 
int bufferIndex = 0;            
int validBeatsCount = 0;        

// Calibration & Stats
bool isCalibrating = false;
float calibrationSum = 0;
int calibrationSamples = 0;
float personalBaseline = 0;

// Buzzer State
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;
const unsigned long BUZZER_DURATION = 5000; 

// Display State Tracking (To prevent flicker)
float lastDisplayedBPM = -1;
float lastDisplayedHRV = -1;
String lastDisplayedStatus = "";
unsigned long beatVisualTimer = 0;
bool isBigHeartVisible = false;

// ========== FUNCTIONS ==========

void showCredits() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Developed By:"));
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  display.setCursor(0, 15); display.println(F("Vishwas Paliwal"));
  display.setCursor(0, 27); display.println(F("Sumit Yaduwanshi"));
  display.setCursor(0, 39); display.println(F("Nikhil Misal"));
  display.setCursor(0, 51); display.println(F("Md. Talha Khan"));
  display.display();
  delay(4000);
}

void playBootAnimation() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(30, 50);
  display.print(F("System Ready"));
  for(int i=0; i<5; i++) {
    display.fillRect(56, 20, 16, 16, SSD1306_BLACK); 
    display.drawBitmap(56, 20, heart_big, 16, 16, 1);
    display.display();
    delay(200);
    display.fillRect(56, 20, 16, 16, SSD1306_BLACK);
    display.drawBitmap(56, 20, heart_small, 16, 16, 1);
    display.display();
    delay(200);
  }
}

float calculateRMSSD() {
  if (validBeatsCount < SAMPLE_SIZE) return 0;
  float sumSquaredDiffs = 0;
  for (int i = 0; i < SAMPLE_SIZE - 1; i++) {
    int currIdx = (bufferIndex + i) % SAMPLE_SIZE;
    int nextIdx = (bufferIndex + i + 1) % SAMPLE_SIZE;
    float diff = ibiHistory[currIdx] - ibiHistory[nextIdx];
    sumSquaredDiffs += (diff * diff);
  }
  return sqrt(sumSquaredDiffs / (SAMPLE_SIZE - 1));
}

// Optimized Display Update (No Flicker)
void drawStaticInterface() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.print("BPM:");
  display.setCursor(70, 0); display.print("HRV:");
  display.setCursor(0, 35); display.print("Sts:");
  
  // Frame for Anxiety Bar
  display.drawRect(0, 48, 128, 14, SSD1306_WHITE); 
  display.display();
}

void updateDisplayData(float bpm, float hrv, String statusMsg, float anxietyPercent) {
  // Only update BPM if changed
  if (abs(bpm - lastDisplayedBPM) > 1.0) {
    display.fillRect(25, 0, 40, 10, SSD1306_BLACK); // Clear old number only
    display.setCursor(25, 0);
    display.print(bpm, 0);
    lastDisplayedBPM = bpm;
  }

  // Only update HRV if changed
  if (abs(hrv - lastDisplayedHRV) > 1.0) {
    display.fillRect(95, 0, 30, 10, SSD1306_BLACK); // Clear old number only
    display.setCursor(95, 0);
    display.print(hrv, 0);
    lastDisplayedHRV = hrv;
  }

  // Only update Status if changed
  if (!statusMsg.equals(lastDisplayedStatus)) {
    display.fillRect(25, 35, 100, 10, SSD1306_BLACK); // Clear old text
    display.setCursor(25, 35);
    display.print(statusMsg);
    lastDisplayedStatus = statusMsg;
  }

  // Update Anxiety Bar (Always refresh this to be safe, or check diff)
  int barWidth = map(constrain(anxietyPercent, 0, 100), 0, 100, 0, 124);
  display.fillRect(2, 50, 124, 10, SSD1306_BLACK); // Clear internal bar
  if(barWidth > 0) {
    display.fillRect(2, 50, barWidth, 10, SSD1306_WHITE);
  }

  display.display();
}

// Separate Function for Heart Animation (Fast Refresh)
void updateHeartAnimation(bool big) {
  if (big == isBigHeartVisible) return; // No change needed

  // Clear heart area only
  display.fillRect(56, 15, 16, 16, SSD1306_BLACK); 
  
  if (big) {
    display.drawBitmap(56, 15, heart_big, 16, 16, 1);
  } else {
    display.drawBitmap(56, 15, heart_small, 16, 16, 1);
  }
  
  display.display();
  isBigHeartVisible = big;
}

void handleBuzzer() {
  if (buzzerActive) {
    if (millis() - buzzerStartTime >= BUZZER_DURATION) {
      digitalWrite(BUZZER_PIN, LOW); 
      buzzerActive = false;
    }
  }
}

void triggerAlarm() {
  if (!buzzerActive) { 
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerStartTime = millis();
    buzzerActive = true;
  }
}

BLYNK_WRITE(VPIN_CALIBRATE) {
  if (param.asInt() == 1) {
    isCalibrating = true;
    calibrationSum = 0;
    calibrationSamples = 0;
    Blynk.virtualWrite(VPIN_STATUS, "Calibrating...");
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Wire.begin(SDA_PIN, SCL_PIN);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 failed"));
    for(;;);
  }
  
  showCredits();
  playBootAnimation();

  // WiFi Connection Screen
  display.clearDisplay();
  display.setCursor(0, 20);
  display.print("Connecting WiFi...");
  display.display();
  
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Sensor Error!");
    display.display();
    while (1);
  }

  particleSensor.setup(60, 4, 2, 400, 411, 4096);
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeIR(0x1F);

  // Draw Static Interface once
  drawStaticInterface();
  updateHeartAnimation(false);
}

// ========== MAIN LOOP ==========
void loop() {
  Blynk.run();
  handleBuzzer(); 
  
  irValue = particleSensor.getIR();

  // --- NO FINGER ---
  if (irValue < FINGER_THRESHOLD) {
    if (validBeatsCount > 0) {
      validBeatsCount = 0; 
      bufferIndex = 0;  
      
      // Reset Display
      drawStaticInterface();
      updateDisplayData(0, 0, "No Finger", 0);
      updateHeartAnimation(false);
      
      Blynk.virtualWrite(VPIN_STATUS, "No Finger");
      digitalWrite(BUZZER_PIN, LOW); 
      buzzerActive = false;
    }
    return;
  }

  // --- BEAT DETECTION ---
  long currentTime = millis();
  long timeSinceLastBeat = currentTime - lastBeatTime;

  // Animation Logic: Shrink heart if time passed
  if (isBigHeartVisible && (currentTime - beatVisualTimer > 150)) {
      updateHeartAnimation(false); // Make heart small again
  }

  if (irValue > BEAT_TRIGGER && timeSinceLastBeat > MIN_IBI_MS) {
     if (timeSinceLastBeat < MAX_IBI_MS) {
        
        // ** Beat Detected **
        beatVisualTimer = millis(); 
        updateHeartAnimation(true); // Make heart big immediately

        float currentIBI = (float)timeSinceLastBeat;
        float currentBPM = 60000.0 / currentIBI;
        
        ibiHistory[bufferIndex] = currentIBI;
        bufferIndex = (bufferIndex + 1) % SAMPLE_SIZE;
        if (validBeatsCount < SAMPLE_SIZE) validBeatsCount++;

        Blynk.virtualWrite(VPIN_BPM, currentBPM);

        float anxietyPercentage = 0;
        float rmssd = 0;
        String statusText = "Reading...";

        if (validBeatsCount >= SAMPLE_SIZE) {
          rmssd = calculateRMSSD();
          Blynk.virtualWrite(VPIN_HRV, rmssd);
          anxietyPercentage = map(constrain(rmssd, 10, 100), 10, 100, 100, 0);

          if (isCalibrating) {
             calibrationSum += rmssd;
             calibrationSamples++;
             statusText = "Calib..";
             if (calibrationSamples >= 20) {
                personalBaseline = calibrationSum / 20.0;
                isCalibrating = false;
             }
          } 
          else {
             float threshold = (personalBaseline > 0) ? personalBaseline : DEFAULT_ANXIETY_THRESHOLD;
             if (rmssd < threshold) {
                statusText = "ANXIETY!";
                triggerAlarm(); 
                Blynk.virtualWrite(VPIN_STATUS, "Stress!");
             } else {
                statusText = "Relaxed";
                Blynk.virtualWrite(VPIN_STATUS, "Relaxed");
             }
          }
        } else {
           anxietyPercentage = (validBeatsCount * 100) / SAMPLE_SIZE; 
           statusText = "Analyzing";
        }

        // Update Numbers (Only happens on beat)
        updateDisplayData(currentBPM, rmssd, statusText, anxietyPercentage);
     }
     lastBeatTime = currentTime;
  } 
}
