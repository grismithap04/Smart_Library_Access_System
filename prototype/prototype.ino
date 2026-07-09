#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ===== NETWORK =====
const char* ssid     = "ACTFIBERNET";
const char* password = "act12345";
const char* baseUrl  = "http://192.168.0.142:5000";

// ===== PINS =====
#define FINGERPRINT_RX 16
#define FINGERPRINT_TX 17
#define OLED_SDA       21
#define OLED_SCL       22
#define GREEN_LED      26
#define RED_LED        27
#define BUZZER         25

// ===== OLED =====
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_ADDRESS  0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== FINGERPRINT =====
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// ===== STATE =====
int  nextSlot = 1;
bool isEnrolling = false;
unsigned long lastPollMs = 0;
#define POLL_INTERVAL_MS 5000

// ===== OLED HELPERS =====
void oled2Large(String l1, String l2 = "") {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(l1);
    if (l2.length() > 0) { display.setCursor(0, 20); display.println(l2); }
    display.display();
}
void oled3Msg(String l1, String l2 = "", String l3 = "") {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(l1);
    if (l2.length() > 0) { display.setTextSize(1); display.setCursor(0, 20); display.println(l2); }
    if (l3.length() > 0) { display.setTextSize(1); display.setCursor(0, 35); display.println(l3); }
    display.display();
}
void oledEntryExit(String action, String name, String status) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(action);
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println(name);
    display.setCursor(0, 35);
    display.println(status);
    display.display();
}
void oledMsg(String l1, String l2 = "") {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(l1);
    if (l2.length() > 0) { display.setCursor(0, 20); display.println(l2); }
    display.display();
}

// ===== BUZZER / LED =====
void silenceBuzzer() { noTone(BUZZER); digitalWrite(BUZZER, LOW); pinMode(BUZZER, OUTPUT); }
void beepWrong(int t) {
    for (int i = 0; i < t; i++) {
        digitalWrite(RED_LED, HIGH);
        tone(BUZZER, 2500, 400);
        delay(400);
        noTone(BUZZER);
        digitalWrite(BUZZER, LOW);
        digitalWrite(RED_LED, LOW);
        delay(150);
    }
    silenceBuzzer();
}
void beepBlocked() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(RED_LED, HIGH);
        tone(BUZZER, 2000, 600);
        delay(600);
        noTone(BUZZER);
        digitalWrite(BUZZER, LOW);
        digitalWrite(RED_LED, LOW);
        delay(200);
    }
    silenceBuzzer();
}
void beepSuccess() { tone(BUZZER, 3000, 150); delay(150); noTone(BUZZER); digitalWrite(BUZZER, LOW); }
void blinkGreen(int t) { for (int i = 0; i < t; i++) { digitalWrite(GREEN_LED, HIGH); delay(250); digitalWrite(GREEN_LED, LOW); delay(150); } }
void blinkRed(int t) { for (int i = 0; i < t; i++) { digitalWrite(RED_LED, HIGH); delay(250); digitalWrite(RED_LED, LOW); delay(150); } }

// ===== WIFI =====
void connectWiFi() {
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    oledMsg("Connecting", "WiFi...");
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        oledMsg("WiFi", "Connected!");
        blinkGreen(3);
        delay(1000);
    } else {
        Serial.println("\nWiFi FAILED. Running offline.");
        oledMsg("WiFi", "Failed!");
        blinkRed(3);
        delay(2000);
    }
}

// ===== HTTP HELPERS =====
String httpGet(String url) {
    if (WiFi.status() != WL_CONNECTED) return "";
    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    int code = http.GET();
    String body = "";
    if (code == 200) body = http.getString();
    else Serial.println("GET failed: " + String(code) + " → " + url);
    http.end();
    return body;
}
String httpPost(String url, String jsonBody) {
    if (WiFi.status() != WL_CONNECTED) return "";
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    int code = http.POST(jsonBody);
    String body = "";
    if (code == 200 || code == 403) body = http.getString();
    else { Serial.println("POST failed: " + String(code) + " → " + url); body = http.getString(); }
    http.end();
    return body;
}

// ===== CORE: FINGERPRINT TAP =====
void handleFingerprintTap(int fp_slot) {
    Serial.println("========================================");
    Serial.print("Slot matched: ");
    Serial.println(fp_slot);
    String lookupUrl = String(baseUrl) + "/api/fingerprint-lookup?fp_slot=" + String(fp_slot);
    String lookupResponse = httpGet(lookupUrl);
    if (lookupResponse == "") {
        Serial.println("Lookup failed — no server response.");
        oledMsg("Server", "Error!");
        blinkRed(2);
        delay(1500);
        oledMsg("Place", "Finger");
        return;
    }
    StaticJsonDocument<256> lookupDoc;
    if (deserializeJson(lookupDoc, lookupResponse) || lookupDoc.containsKey("error")) {
        Serial.println("Fingerprint not registered in database.");
        oledMsg("NOT", "REGISTERED");
        beepWrong(2);
        delay(1500);
        oledMsg("Place", "Finger");
        return;
    }
    String reg_id = lookupDoc["reg_id"].as<String>();
    String name = lookupDoc["name"].as<String>();
    Serial.println("Student: " + name + " (" + reg_id + ")");
    String sessionResp = httpGet(String(baseUrl) + "/api/active");
    String tapType = "entry";
    if (sessionResp != "") {
        StaticJsonDocument<2048> sessionDoc;
        if (!deserializeJson(sessionDoc, sessionResp) && sessionDoc.is<JsonArray>()) {
            for (JsonObject session : sessionDoc.as<JsonArray>())
                if (session["reg_id"].as<String>() == reg_id) { tapType = "exit"; break; }
        }
    }
    String tapResponse = httpPost(String(baseUrl) + "/api/mock-fingerprint",
        "{\"reg_id\": \"" + reg_id + "\", \"type\": \"" + tapType + "\"}");
    if (tapResponse == "") {
        Serial.println("Tap POST failed.");
        oledMsg("Server", "Error!");
        blinkRed(2);
        delay(1500);
        oledMsg("Place", "Finger");
        return;
    }
    StaticJsonDocument<256> tapDoc;
    deserializeJson(tapDoc, tapResponse);
    if (tapDoc.containsKey("blocked") && tapDoc["blocked"] == true) {
        Serial.println("ENTRY BLOCKED: " + tapDoc["reason"].as<String>());
        oledMsg("ENTRY", "CLOSED");
        beepBlocked();
        blinkRed(3);
        delay(2000);
        oledMsg("Place", "Finger");
        return;
    }
    if (tapDoc["ok"] == true) {
        if (tapType == "entry") {
            Serial.println("ENTRY logged: " + name);
            oledEntryExit("ENTRY", name, "Logged at library");
            beepSuccess();
            blinkGreen(2);
        } else {
            Serial.println("EXIT logged: " + name);
            oledEntryExit("EXIT", name, "Safe journey!");
            beepSuccess();
            blinkGreen(1);
            delay(200);
            blinkGreen(1);
        }
    } else {
        Serial.println("Tap error: " + tapDoc["error"].as<String>());
        oledMsg("Log", "Error");
        blinkRed(2);
        beepWrong(1);
    }
    delay(2000);
    oledMsg("Place", "Finger");
    Serial.println("========================================");
}

// ===== ENROLLMENT =====
void pollEnrollmentQueue() {
    if (WiFi.status() != WL_CONNECTED) return;
    String response = httpGet(String(baseUrl) + "/api/enrollment-queue?esp_id=library1");
    if (response == "") return;
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, response)) return;
    String reg_id = doc["reg_id"].as<String>();
    if (reg_id == "" || reg_id == "null") return;
    Serial.println("Enrollment requested for: " + reg_id);
    enrollFromDashboard(reg_id);
}

void enrollFromDashboard(String reg_id) {
    isEnrolling = true;
    silenceBuzzer();
    Serial.println("========================================");
    Serial.println("ENROLLING: " + reg_id);
    Serial.println("========================================");
    findNextSlot();
    if (nextSlot > 127) {
        Serial.println("Sensor full!");
        oledMsg("Sensor", "Full!");
        beepWrong(1);
        isEnrolling = false;
        oledMsg("Place", "Finger");
        return;
    }
    Serial.print("Using slot: ");
    Serial.println(nextSlot);
    oled3Msg("ENROLLING", reg_id.substring(0, 12), "Place Finger (1)");
    Serial.println("Place finger (1st time)...");
    int p = FINGERPRINT_NOFINGER;
    unsigned long start = millis();
    while (p == FINGERPRINT_NOFINGER && (millis() - start) < 30000) { p = finger.getImage(); delay(100); }
    if (p != FINGERPRINT_OK) { Serial.println("1st read failed."); oledMsg("Read", "Error"); beepWrong(1); delay(1000); notifyEnrollmentFailed(reg_id, "Sensor read failed"); isEnrolling = false; oledMsg("Place", "Finger"); return; }
    blinkGreen(1);
    if (finger.image2Tz(1) != FINGERPRINT_OK) { Serial.println("Image 1 conversion failed."); oledMsg("Convert", "Error"); beepWrong(1); delay(1000); notifyEnrollmentFailed(reg_id, "Image conversion failed"); isEnrolling = false; oledMsg("Place", "Finger"); return; }
    Serial.println("Image 1 captured.");
    oledMsg("Remove", "Finger");
    Serial.println("Remove finger...");
    delay(2000);
    while (finger.getImage() != FINGERPRINT_NOFINGER) delay(100);
    delay(500);
    oled3Msg("ENROLLING", reg_id.substring(0, 12), "Place Finger (2)");
    Serial.println("Place same finger (2nd time)...");
    p = FINGERPRINT_NOFINGER;
    start = millis();
    while (p == FINGERPRINT_NOFINGER && (millis() - start) < 30000) { p = finger.getImage(); delay(100); }
    if (p != FINGERPRINT_OK) { Serial.println("2nd read failed."); oledMsg("Read", "Error"); beepWrong(1); delay(1000); notifyEnrollmentFailed(reg_id, "2nd read failed"); isEnrolling = false; oledMsg("Place", "Finger"); return; }
    blinkGreen(1);
    if (finger.image2Tz(2) != FINGERPRINT_OK) { Serial.println("Image 2 conversion failed."); oledMsg("Convert", "Error"); beepWrong(1); delay(1000); notifyEnrollmentFailed(reg_id, "Image 2 conversion failed"); isEnrolling = false; oledMsg("Place", "Finger"); return; }
    Serial.println("Image 2 captured.");
    oledMsg("Creating", "Model...");
    p = finger.createModel();
    if (p == FINGERPRINT_ENROLLMISMATCH) { Serial.println("Fingerprints did not match!"); oledMsg("MISMATCH", "Try again"); beepWrong(2); delay(2000); notifyEnrollmentFailed(reg_id, "Fingerprints did not match"); isEnrolling = false; oledMsg("Place", "Finger"); return; }
    if (p != FINGERPRINT_OK) { Serial.println("Model creation failed."); oledMsg("Model", "Error"); beepWrong(1); delay(1500); notifyEnrollmentFailed(reg_id, "Model creation failed"); isEnrolling = false; oledMsg("Place", "Finger"); return; }
    if (finger.storeModel(nextSlot) != FINGERPRINT_OK) { Serial.println("Store failed."); oledMsg("Store", "Failed"); beepWrong(1); delay(1500); notifyEnrollmentFailed(reg_id, "Store failed"); isEnrolling = false; oledMsg("Place", "Finger"); return; }
    Serial.println("Enrollment successful! Slot: " + String(nextSlot));
    oledMsg("ENROLLED!", "Slot " + String(nextSlot));
    blinkGreen(3);
    beepSuccess();
    String completeResp = httpPost(String(baseUrl) + "/api/complete-enrollment",
        "{\"reg_id\": \"" + reg_id + "\", \"fp_slot\": " + String(nextSlot) + "}");
    if (completeResp != "") {
        StaticJsonDocument<128> doc;
        deserializeJson(doc, completeResp);
        if (doc["ok"] == true) Serial.println("Slot mapping saved to database.");
        else Serial.println("DB save warning: " + completeResp);
    }
    delay(2000);
    nextSlot++;
    silenceBuzzer();
    isEnrolling = false;
    oledMsg("Place", "Finger");
    Serial.println("========================================");
}
void notifyEnrollmentFailed(String reg_id, String reason) {
    httpPost(String(baseUrl) + "/api/enrollment-failed",
        "{\"reg_id\": \"" + reg_id + "\", \"reason\": \"" + reason + "\"}");
}

// ===== FINGERPRINT SCAN =====
int scanFingerprint() {
    int p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) return -2;
    if (p != FINGERPRINT_OK) return -1;
    if (finger.image2Tz() != FINGERPRINT_OK) return -1;
    p = finger.fingerFastSearch();
    if (p == FINGERPRINT_OK) return finger.fingerID;
    if (p == FINGERPRINT_NOTFOUND) return 0;
    return -1;
}

// ===== HELPERS =====
void findNextSlot() {
    for (int slot = 1; slot <= 127; slot++) {
        if (finger.loadModel(slot) != FINGERPRINT_OK) { nextSlot = slot; return; }
    }
    nextSlot = 128;
}

// ===== SERIAL COMMANDS =====
void processCommand(String input) {
    Serial.println(">> " + input);
    if (input == "scan") manualScan();
    else if (input == "delete") deleteFingerprint();
    else if (input == "list") listEnrolledFingerprints();
    else if (input == "help") showHelp();
    else if (input == "wifi") Serial.println("IP: " + WiFi.localIP().toString() + "  Status: " + (WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"));
    else Serial.println("Unknown command. Type 'help'.");
}
void showHelp() {
    Serial.println("========================================");
    Serial.println("Commands: scan | delete | list | wifi | help");
    Serial.println("Note: Enrollment is done from the dashboard.");
    Serial.println("========================================");
}
void manualScan() {
    Serial.println("MANUAL SCAN — place finger...");
    oledMsg("Scanning...");
    int result = scanFingerprint();
    if (result == -2) { Serial.println("No finger."); oledMsg("No", "Finger"); }
    else if (result == -1) { Serial.println("Read error."); oledMsg("Read", "Error"); blinkRed(2); delay(500); }
    else if (result == 0) { Serial.println("Not found."); oledMsg("NOT", "FOUND"); beepWrong(2); delay(1500); }
    else { handleFingerprintTap(result); return; }
    oledMsg("Place", "Finger");
}
void deleteFingerprint() {
    Serial.println("DELETE — enter slot number:");
    while (!Serial.available()) delay(100);
    int slot = Serial.parseInt();
    Serial.println(slot);
    if (finger.deleteModel(slot) == FINGERPRINT_OK) {
        Serial.println("Slot " + String(slot) + " deleted.");
        oledMsg("Deleted", "Slot " + String(slot));
        blinkGreen(1);
        delay(1500);
    } else {
        Serial.println("Delete failed.");
        oledMsg("Delete", "Failed");
        beepWrong(1);
        delay(1500);
    }
    oledMsg("Place", "Finger");
}
void listEnrolledFingerprints() {
    Serial.println("ENROLLED FINGERPRINTS:");
    int found = 0;
    for (int i = 1; i <= 127; i++) {
        if (finger.loadModel(i) == FINGERPRINT_OK) { Serial.println("  Slot " + String(i) + ": Enrolled"); found++; }
    }
    Serial.println(found == 0 ? "  None." : "Total: " + String(found));
}

// ===== SETUP =====
void setup() {
    pinMode(BUZZER, OUTPUT);
    digitalWrite(BUZZER, LOW);
    noTone(BUZZER);
    Serial.begin(115200);
    delay(1000);
    Serial.println("========================================");
    Serial.println("SASTRA Library Access System");
    Serial.println("========================================");
    pinMode(GREEN_LED, OUTPUT);
    pinMode(RED_LED, OUTPUT);
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH); delay(300); digitalWrite(GREEN_LED, LOW);
    delay(100);
    digitalWrite(RED_LED, HIGH); delay(300); digitalWrite(RED_LED, LOW);
    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) { Serial.println("OLED not found!"); while (1); }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    oledMsg("SASTRA", "Library");
    delay(500);
    mySerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX, FINGERPRINT_TX);
    delay(100);
    finger.begin(57600);
    if (finger.verifyPassword()) {
        Serial.println("Sensor found!");
        oledMsg("Sensor", "Ready");
        blinkGreen(2);
        delay(500);
    } else {
        Serial.println("Sensor not found! Check wiring.");
        oledMsg("Sensor", "Error!");
        blinkRed(3);
        beepWrong(1);
        while (1);
    }
    findNextSlot();
    connectWiFi();
    oledMsg("Place", "Finger");
    Serial.println("System Ready!");
    Serial.print("Next slot: ");
    Serial.println(nextSlot);
    Serial.println("Flask server: " + String(baseUrl));
    Serial.println("========================================");
    Serial.println("Commands: scan | delete | list | wifi | help");
    Serial.println("========================================");
}

// ===== MAIN LOOP =====
void loop() {
    silenceBuzzer();
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        input.toLowerCase();
        if (input.length() > 0) processCommand(input);
    }
    if (!isEnrolling && millis() - lastPollMs >= POLL_INTERVAL_MS) {
        lastPollMs = millis();
        pollEnrollmentQueue();
    }
    if (!isEnrolling) {
        int result = scanFingerprint();
        if (result == -1) {
            oledMsg("Read", "Error");
            blinkRed(2);
            delay(500);
            oledMsg("Place", "Finger");
        } else if (result == 0) {
            oledMsg("NOT", "FOUND");
            beepWrong(2);
            oledMsg("Place", "Finger");
        } else if (result > 0) {
            handleFingerprintTap(result);
        }
    }
    delay(200);
}