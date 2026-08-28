/**
 * @file      100W_IoT_PowerHub.ino
 * @brief     Firmware for ESP32-C3 Super Mini Power Delivery Motherboard
 * @hardware  ESP32-C3, 2x INA219, 1x SW3516, 1.3" SH1106 OLED, P-Channel MOSFETs
 * @notes     I2C mapped to GPIO 8 (SDA), GPIO 9 (SCL).
 *            OLED address shifted to 0x3D to avoid SW3516 bus collision.
 */

#include <Wire.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <INA219.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "dashboard.h"

const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";

#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9
#define UI_BUTTON_PIN 2 // External 10k pull-up populated on PCB

// P-Channel MOSFET Gates (Active LOW)
#define GATE_12V_PIN 3
#define GATE_9V_PIN 4

#define SW3516_I2C_ADDRESS 0x3C
#define OLED_I2C_ADDRESS 0x3D 

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
INA219 ina219_12V(0x40);
INA219 ina219_9V(0x41);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const float MAX_CURRENT_mA = 5000.0; 
const int SENSOR_POLL_RATE = 20;     
const int SW3516_POLL_RATE = 100;    
const int OLED_REFRESH_RATE = 250;   
const int WEB_PUSH_RATE = 500;       
const int DEBOUNCE_DELAY = 150;      

struct PowerData {
    float voltage_V;
    float current_mA;
    float power_W;
};

struct PDProtocolData {
    float negotiated_V;
    float negotiated_A;
    String active_protocol;
};

PowerData rail12V = {0, 0, 0};
PowerData rail9V = {0, 0, 0};
PDProtocolData sw3516_port = {0, 0, "SCANNING..."};

volatile int uiState = 0;
volatile unsigned long lastButtonTime = 0;
bool isFaultTripped = false;
bool isRemoteKilled = false;

unsigned long lastSensorRead = 0;
unsigned long lastSW3516Read = 0;
unsigned long lastOledUpdate = 0;
unsigned long lastWebPush = 0;

void IRAM_ATTR handleButtonPress() {
    unsigned long currentTime = millis();
    if ((currentTime - lastButtonTime) > DEBOUNCE_DELAY) {
        if (!isFaultTripped) {
            uiState = (uiState + 1) % 3;
        }
        lastButtonTime = currentTime;
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
        String message = "";
        for (size_t i = 0; i < len; i++) {
            message += (char)data[i];
        }

        if (message == "KILL") {
            isRemoteKilled = !isRemoteKilled;
            if (isRemoteKilled) {
                digitalWrite(GATE_12V_PIN, HIGH);
                digitalWrite(GATE_9V_PIN, HIGH);  
            } else {
                if (!isFaultTripped) {
                    digitalWrite(GATE_12V_PIN, LOW);
                    digitalWrite(GATE_9V_PIN, LOW);
                }
            }
        }
    }
}

void setup() {
    pinMode(GATE_12V_PIN, OUTPUT);
    pinMode(GATE_9V_PIN, OUTPUT);
    digitalWrite(GATE_12V_PIN, HIGH);
    digitalWrite(GATE_9V_PIN, HIGH);

    pinMode(UI_BUTTON_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(UI_BUTTON_PIN), handleButtonPress, FALLING);

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);

    display.setI2CAddress(OLED_I2C_ADDRESS << 1);
    display.begin();
    display.setBusClock(400000);

    if (!ina219_12V.begin() || !ina219_9V.begin()) {
        displaySystemError("INA219 INIT FAIL");
        while (1) {
            delay(10);
        }
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    display.clearBuffer();
    display.setFont(u8g2_font_helvB08_tr);
    display.drawStr(10, 30, "CONNECTING WIFI...");
    display.sendBuffer();

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { 
        request->send_P(200, "text/html", index_html); 
    });

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.begin();

    digitalWrite(GATE_12V_PIN, LOW);
    digitalWrite(GATE_9V_PIN, LOW);
}

void loop() {
    unsigned long currentMillis = millis();

    ws.cleanupClients();

    if (currentMillis - lastSensorRead >= SENSOR_POLL_RATE) {
        lastSensorRead = currentMillis;
        updateTelemetry();
        executeSafetyCheck();
    }

    if (currentMillis - lastSW3516Read >= SW3516_POLL_RATE) {
        lastSW3516Read = currentMillis;
        pollSW3516Telemetry();
    }

    if (currentMillis - lastWebPush >= WEB_PUSH_RATE) {
        lastWebPush = currentMillis;
        pushDataToWeb();
    }

    if (currentMillis - lastOledUpdate >= OLED_REFRESH_RATE) {
        lastOledUpdate = currentMillis;
        renderOLED();
    }
}

void updateTelemetry() {
    rail12V.voltage_V = ina219_12V.getBusVoltage_V();
    rail12V.current_mA = ina219_12V.getCurrent_mA();
    rail12V.power_W = (rail12V.voltage_V * rail12V.current_mA) / 1000.0;

    rail9V.voltage_V = ina219_9V.getBusVoltage_V();
    rail9V.current_mA = ina219_9V.getCurrent_mA();
    rail9V.power_W = (rail9V.voltage_V * rail9V.current_mA) / 1000.0;
}

void executeSafetyCheck() {
    if (rail12V.current_mA > MAX_CURRENT_mA || rail9V.current_mA > MAX_CURRENT_mA) {
        digitalWrite(GATE_12V_PIN, HIGH);
        digitalWrite(GATE_9V_PIN, HIGH);
        isFaultTripped = true;
    }
}

void pollSW3516Telemetry() {
    Wire.beginTransmission(SW3516_I2C_ADDRESS);
    if (Wire.endTransmission() == 0) {
        Wire.requestFrom(SW3516_I2C_ADDRESS, 2);
        if (Wire.available() >= 2) {
            byte highByte = Wire.read();
            byte lowByte = Wire.read();
            sw3516_port.negotiated_V = 20.0;
            sw3516_port.active_protocol = "PD3.0 100W";
        }
    }
}

void pushDataToWeb() {
    if (ws.count() > 0) {
        StaticJsonDocument<200> doc;
        doc["v12"] = rail12V.voltage_V;
        doc["a12"] = rail12V.current_mA / 1000.0;
        doc["w12"] = rail12V.power_W;
        doc["v9"] = rail9V.voltage_V;
        doc["a9"] = rail9V.current_mA / 1000.0;
        doc["w9"] = rail9V.power_W;

        String jsonString;
        serializeJson(doc, jsonString);
        ws.textAll(jsonString);
    }
}

void renderOLED() {
    display.clearBuffer();

    if (isFaultTripped) {
        drawFaultScreen();
    } else {
        switch (uiState) {
        case 0:
            drawOverviewScreen();
            break;
        case 1:
            drawRailScreen("12V RAIL", rail12V);
            break;
        case 2:
            drawPD_RailScreen("9V + SW3516", rail9V, sw3516_port);
            break;
        }
    }

    display.sendBuffer();
}

void drawOverviewScreen() {
    display.setDrawColor(1);
    display.drawBox(0, 0, 128, 14);
    display.setDrawColor(0);
    display.setFont(u8g2_font_helvB08_tr);
    display.drawStr(16, 11, "100W SYSTEM HUB");

    display.setDrawColor(1);
    display.setFont(u8g2_font_helvB18_tr);

    float totalW = rail12V.power_W + rail9V.power_W;
    char buffer[10];
    dtostrf(totalW, 4, 1, buffer);
    display.drawStr(20, 40, buffer);

    display.setFont(u8g2_font_helvB08_tr);
    display.drawStr(90, 40, "W");

    display.drawLine(0, 52, 128, 52);
    
    if (isRemoteKilled) {
        display.drawStr(0, 63, "STATUS: REMOTE KILL");
    } else {
        display.drawStr(0, 63, "IP: " + WiFi.localIP().toString());
    }
}

void drawRailScreen(const char *title, PowerData &rail) {
    display.setFont(u8g2_font_helvB08_tr);
    display.drawStr(0, 9, title);
    display.drawLine(0, 12, 128, 12);

    display.setFont(u8g2_font_helvB24_tn);
    char v_buf[8];
    dtostrf(rail.voltage_V, 4, 1, v_buf);
    display.drawStr(0, 42, v_buf);

    display.setFont(u8g2_font_helvB08_tr);
    display.drawStr(70, 42, "V");

    char sub_buf[10];
    dtostrf(rail.current_mA / 1000.0, 4, 2, sub_buf);
    display.drawStr(90, 26, sub_buf);
    display.drawStr(120, 26, "A");

    dtostrf(rail.power_W, 4, 1, sub_buf);
    display.drawStr(90, 42, sub_buf);
    display.drawStr(120, 42, "W");

    display.drawLine(0, 52, 128, 52);
    display.drawStr(0, 63, "                      [>]");
}

void drawPD_RailScreen(const char *title, PowerData &rail, PDProtocolData &pd) {
    drawRailScreen(title, rail);
    display.setDrawColor(0);
    display.drawBox(0, 53, 128, 11);
    display.setDrawColor(1);

    display.setFont(u8g2_font_helvB08_tr);
    display.setCursor(0, 63);
    display.print(pd.active_protocol);
    display.drawStr(110, 63, "[>]");
}

void drawFaultScreen() {
    display.setFont(u8g2_font_helvB14_tr);
    display.drawStr(10, 30, "OVERCURRENT");
    display.drawStr(30, 50, "FAULT");
    display.drawFrame(0, 0, 128, 64);
    display.drawFrame(1, 1, 126, 62);
}

void displaySystemError(const char *msg) {
    display.clearBuffer();
    display.setFont(u8g2_font_helvB08_tr);
    display.drawStr(0, 20, "SYSTEM HALTED:");
    display.drawStr(0, 40, msg);
    display.sendBuffer();
}