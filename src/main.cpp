
// PCA9685 + smooth servo control with simple Web UI and Serial fallback
// - Starts as WiFi Access Point (SSID: ESP32-PCA9685)
// - Serves `data/` files from SPIFFS (use PlatformIO `data/` + Upload File System Image)
// - Offers simple REST endpoints under /api/* to control servos

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>

#include <SPIFFS.h>
#include "config.h"

static const uint8_t PCA_ADDR = 0x40;
static const uint8_t MODE1 = 0x00;
static const uint8_t PRESCALE = 0xFE;
static const uint8_t LED0_ON_L = 0x06;

WebServer server(80);

void pcaWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PCA_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t pcaRead(uint8_t reg) {
  Wire.beginTransmission(PCA_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((int)PCA_ADDR, 1);
  if (Wire.available()) return Wire.read();
  return 0;
}

void pcaSetPWM(uint8_t channel, uint16_t on, uint16_t off) {
  uint8_t reg = LED0_ON_L + 4 * channel;
  Wire.beginTransmission(PCA_ADDR);
  Wire.write(reg);
  Wire.write(on & 0xFF);
  Wire.write((on >> 8) & 0x0F);
  Wire.write(off & 0xFF);
  Wire.write((off >> 8) & 0x0F);
  Wire.endTransmission();
}

void pcaSetPWMFreq(float freq) {
  float prescaleval = 25000000.0; // 25MHz
  prescaleval /= 4096.0;
  prescaleval /= freq;
  prescaleval -= 1.0;
  uint8_t prescale = (uint8_t)(prescaleval + 0.5f);

  uint8_t oldmode = pcaRead(MODE1);
  uint8_t newmode = (oldmode & 0x7F) | 0x10; // sleep
  pcaWrite(MODE1, newmode);
  pcaWrite(PRESCALE, prescale);
  pcaWrite(MODE1, oldmode);
  delay(5);
  pcaWrite(MODE1, oldmode | 0xA1); // auto-increment on
}

void pcaInit() {
  Wire.begin();
  pcaWrite(MODE1, 0x00);
  pcaSetPWMFreq(50); // servo frequency
}

// Convert microseconds to PCA9685 ticks (0..4095)
uint16_t servoUsToTicks(uint16_t us) {
  float ticks = (us / 20000.0f) * 4096.0f;
  if (ticks < 0) ticks = 0;
  if (ticks > 4095) ticks = 4095;
  return (uint16_t)ticks;
}

// Map 0-180 degrees to ~1000-2000us (adjustable)
uint16_t angleToTicks(int angle) {
  const uint16_t minUs = 1000;
  const uint16_t maxUs = 2000;
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  uint16_t us = minUs + (uint32_t)angle * (maxUs - minUs) / 180;
  return servoUsToTicks(us);
}

// Map spin speed (-100..100) to microseconds around 1500us
uint16_t speedToTicks(int speedPercent) {
  // center 1500us is stop; +/- range
  const int center = 1500;
  // increase range so extremes map to near 500..2500us giving a fuller speed range
  const int range = 1000; // max deflection (maps -100..100 -> 500..2500)
  if (speedPercent > 100) speedPercent = 100;
  if (speedPercent < -100) speedPercent = -100;
  int us = center + (speedPercent * range) / 100;
  if (us < 500) us = 500;
  if (us > 2500) us = 2500;
  return servoUsToTicks((uint16_t)us);
}

enum ServoMode { POSITION = 0, SPIN = 1 };

struct SmoothServo {
  uint8_t channel;
  int currentAngle;
  int targetAngle;
  unsigned int stepDelayMs; // ms between steps
  int stepSize;             // degrees per step
  ServoMode mode;
  int spinSpeed; // -100..100
  unsigned long lastStepMs;
  int centerUs; // calibration center pulse in microseconds (default 1500)
  int rangeUs;  // pulse range for full speed ( +/- rangeUs )
};

SmoothServo servos[16];

// Serve files from SPIFFS
void handleRoot() {
  if (!SPIFFS.exists("/index.html")) {
    server.send(500, "text/plain", "index.html not found on SPIFFS");
    return;
  }
  File f = SPIFFS.open("/index.html", "r");
  server.streamFile(f, "text/html");
  f.close();
}

void handleJs() {
  if (!SPIFFS.exists("/app.js")) { server.send(404, "text/plain", "not found"); return; }
  File f = SPIFFS.open("/app.js", "r");
  server.streamFile(f, "application/javascript");
  f.close();
}

void handleCss() {
  if (!SPIFFS.exists("/style.css")) { server.send(404, "text/plain", "not found"); return; }
  File f = SPIFFS.open("/style.css", "r");
  server.streamFile(f, "text/css");
  f.close();
}

// forward declarations for handlers defined after setup
void handleSetCenter();
void handleSpinLeft();
void handleSpinRight();
void handleSetRange();

String jsonStatusForChannel(int ch) {
  if (ch < 0 || ch > 15) return "{}";
  SmoothServo &s = servos[ch];
  String out = "{";
  out += "\"ch\":" + String(ch) + ",";
  out += "\"mode\":" + String(s.mode == SPIN ? "\"spin\"" : "\"pos\"") + ",";
  out += "\"current\":" + String(s.currentAngle) + ",";
  out += "\"target\":" + String(s.targetAngle) + ",";
  out += "\"stepDelay\":" + String(s.stepDelayMs) + ",";
  out += "\"stepSize\":" + String(s.stepSize) + ",";
  out += "\"spinSpeed\":" + String(s.spinSpeed);
  out += "}";
  return out;
}

void handleStatus() {
  int ch = server.hasArg("ch") ? server.arg("ch").toInt() : -1;
  String out = "{";
  if (ch >= 0 && ch <= 15) {
    out += "\"channel\":" + String(ch) + ",\"data\":" + jsonStatusForChannel(ch);
  } else {
    out += "\"channels\": [";
    for (int i = 0; i < 16; ++i) {
      out += jsonStatusForChannel(i);
      if (i < 15) out += ",";
    }
    out += "]";
  }
  out += "}";
  server.send(200, "application/json", out);
}

void handleSetPos() {
  int ch = server.hasArg("ch") ? server.arg("ch").toInt() : 0;
  int angle = server.hasArg("angle") ? server.arg("angle").toInt() : 90;
  int delayMs = server.hasArg("delay") ? server.arg("delay").toInt() : 15;
  int step = server.hasArg("step") ? server.arg("step").toInt() : 1;
  if (ch < 0 || ch > 15) ch = 0;
  SmoothServo &s = servos[ch];
  s.mode = POSITION;
  s.targetAngle = constrain(angle, 0, 180);
  s.stepDelayMs = max(1, delayMs);
  s.stepSize = max(1, step);
  server.send(200, "text/plain", "ok");
}

void handleSpin() {
  int ch = server.hasArg("ch") ? server.arg("ch").toInt() : 0;
  float spf = server.hasArg("speed") ? server.arg("speed").toFloat() : 0.0f;
  if (ch < 0 || ch > 15) ch = 0;
  SmoothServo &s = servos[ch];
  // Accept either percentage (-100..100) or degree-like range (-180..180).
  // If user sent values outside [-100,100], assume they used -180..180 and scale.
  if (spf > 100.0f || spf < -100.0f) {
    spf = spf / 1.8f; // map -180..180 -> -100..100
  }
  int sp = (int)(spf > 0 ? spf + 0.5f : spf - 0.5f);
  sp = constrain(sp, -100, 100);
  s.mode = SPIN;
  s.spinSpeed = sp;
  server.send(200, "text/plain", "ok");
}

void handleStop() {
  int ch = server.hasArg("ch") ? server.arg("ch").toInt() : 0;
  if (ch < 0 || ch > 15) ch = 0;
  SmoothServo &s = servos[ch];
  // stop spin by setting speed 0 (center pulse)
  s.spinSpeed = 0;
  s.mode = SPIN;
  server.send(200, "text/plain", "ok");
}

// (removed duplicate handler definitions from inside loop)

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("PCA9685 Web Servo Controller");

  // init PCA9685
  pcaInit();

  // init servo state
  for (uint8_t i = 0; i < 16; ++i) {
    servos[i].channel = i;
    servos[i].currentAngle = 90;
    servos[i].targetAngle = 90;
    servos[i].stepDelayMs = 15;
    servos[i].stepSize = 1;
    servos[i].mode = POSITION;
    servos[i].spinSpeed = 0;
    servos[i].lastStepMs = 0;
    servos[i].centerUs = 1500;
    servos[i].rangeUs = 1000;
    // write center
    pcaSetPWM(i, 0, angleToTicks(90));
  }

  // Start WiFi: AP + STA (attempt to join local WiFi so it's reachable from your LAN)

  const char *apName = "ESP32-PCA9685";
  // Load STA credentials from config.h (which uses .env via PlatformIO env variables)
  const char *sta_ssid = STA_SSID;
  const char *sta_pass = STA_PASS;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName);
  IPAddress ap_ip = WiFi.softAPIP();
  Serial.printf("AP started: SSID='%s'  AP IP=%s\n", apName, ap_ip.toString().c_str());

  // Try to connect as station (will not disable the AP if it fails)
  Serial.printf("Attempting STA connect to '%s' ...\n", sta_ssid);
  WiFi.begin(sta_ssid, sta_pass);
  unsigned long start = millis();
  const unsigned long staTimeout = 15000; // ms
  while (millis() - start < staTimeout) {
    if (WiFi.status() == WL_CONNECTED) break;
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress st = WiFi.localIP();
    Serial.printf("STA connected: SSID='%s'  IP=%s\n", sta_ssid, st.toString().c_str());
  } else {
    Serial.println("STA connect failed or timed out — continuing with AP only");
  }

  // mount SPIFFS (files come from `data/` folder in project)
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS failed to mount");
  } else {
    Serial.println("SPIFFS mounted");
  }

  // HTTP handlers for static files
  server.on("/", HTTP_GET, handleRoot);
  server.on("/app.js", HTTP_GET, handleJs);
  server.on("/style.css", HTTP_GET, handleCss);
  // API
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/setpos", HTTP_GET, handleSetPos);
  server.on("/api/spin", HTTP_GET, handleSpin);
  server.on("/api/spinleft", HTTP_GET, handleSpinLeft);
  server.on("/api/spinright", HTTP_GET, handleSpinRight);
  server.on("/api/stop", HTTP_GET, handleStop);
  server.on("/api/setcenter", HTTP_GET, handleSetCenter);
  server.on("/api/setrange", HTTP_GET, handleSetRange);
  // Return IP info (AP and STA) useful for debugging
  server.on("/api/ip", HTTP_GET, [](){
    IPAddress ap = WiFi.softAPIP();
    IPAddress st = WiFi.localIP();
    String out = "{";
    out += "\"ap\": \"" + ap.toString() + "\",";
    out += "\"sta\": \"" + st.toString() + "\"";
    out += "}";
    server.send(200, "application/json", out);
  });
  server.begin();
  Serial.println("HTTP server started");
}

void handleSetCenter() {
  int ch = server.hasArg("ch") ? server.arg("ch").toInt() : 0;
  int us = server.hasArg("center") ? server.arg("center").toInt() : 1500;
  if (ch < 0 || ch > 15) ch = 0;
  servos[ch].centerUs = us;
  server.send(200, "text/plain", "ok");
}

// API: spin left/right endpoints (non-stop until /api/stop)
void handleSpinLeft() {
  int ch = server.hasArg("ch") ? server.arg("ch").toInt() : 0;
  if (ch < 0 || ch > 15) ch = 0;
  servos[ch].mode = SPIN;
  servos[ch].spinSpeed = -100;
  server.send(200, "text/plain", "ok");
}
void handleSpinRight() {
  int ch = server.hasArg("ch") ? server.arg("ch").toInt() : 0;
  if (ch < 0 || ch > 15) ch = 0;
  servos[ch].mode = SPIN;
  servos[ch].spinSpeed = 100;
  server.send(200, "text/plain", "ok");
}

void handleSetRange() {
  int ch = server.hasArg("ch") ? server.arg("ch").toInt() : 0;
  int r = server.hasArg("range") ? server.arg("range").toInt() : 1000;
  if (ch < 0 || ch > 15) ch = 0;
  servos[ch].rangeUs = r;
  server.send(200, "text/plain", "ok");
}

void loop() {
  server.handleClient();
  unsigned long now = millis();
  // update servos
  for (uint8_t i = 0; i < 16; ++i) {
    SmoothServo &s = servos[i];
    if (s.mode == POSITION) {
      if ((unsigned long)(now - s.lastStepMs) >= s.stepDelayMs) {
        s.lastStepMs = now;
        if (s.currentAngle < s.targetAngle) {
          s.currentAngle += s.stepSize;
          if (s.currentAngle > s.targetAngle) s.currentAngle = s.targetAngle;
        } else if (s.currentAngle > s.targetAngle) {
          s.currentAngle -= s.stepSize;
          if (s.currentAngle < s.targetAngle) s.currentAngle = s.targetAngle;
        }
        pcaSetPWM(s.channel, 0, angleToTicks(s.currentAngle));
      }
    } else { // SPIN
      // For continuous rotation servos, map speed to pulse width using per-channel calibration
      int sp = s.spinSpeed;
      if (sp > 100) sp = 100; if (sp < -100) sp = -100;
      int us = s.centerUs + (sp * s.rangeUs) / 100;
      if (us < 500) us = 500;
      if (us > 2500) us = 2500;
      uint16_t ticks = servoUsToTicks((uint16_t)us);
      pcaSetPWM(s.channel, 0, ticks);
    }
  }
}
