#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>

// ============================================
// PIN DEFINITIONS
// ============================================
#define FINGERPRINT_RX 16
#define FINGERPRINT_TX 17
#define OLED_SDA       21
#define OLED_SCL       22
#define GREEN_LED      26
#define RED_LED        27
#define BUZZER         25

// ============================================
// OLED DISPLAY
// ============================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ============================================
// FINGERPRINT SENSOR
// ============================================
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// ============================================
// VARIABLES
// ============================================
int  nextSlot    = 1;
bool isEnrolling = false;

// ============================================
// BUZZER HELPERS
// Only beepWrong() makes sound — nothing else touches the buzzer
// ============================================

// Called ONCE at the top of loop() and after every action
// to guarantee the buzzer is always off between events
void silenceBuzzer() {
    noTone(BUZZER);
    digitalWrite(BUZZER, LOW);
}

// Wrong match / unknown fingerprint / error — buzzer + red LED
void beepWrong(int times) {
    for (int i = 0; i < times; i++) {
        digitalWrite(RED_LED, HIGH);
        tone(BUZZER, 800);
        delay(350);
        noTone(BUZZER);
        digitalWrite(BUZZER, LOW);
        digitalWrite(RED_LED, LOW);
        delay(150);
    }
    // Extra silence to prevent buzzer lingering
    noTone(BUZZER);
    digitalWrite(BUZZER, LOW);
}

// ============================================
// LED HELPERS — no buzzer involvement
// ============================================

void blinkGreen(int times) {
    for (int i = 0; i < times; i++) {
        digitalWrite(GREEN_LED, HIGH);
        delay(250);
        digitalWrite(GREEN_LED, LOW);
        delay(150);
    }
}

void blinkRed(int times) {
    for (int i = 0; i < times; i++) {
        digitalWrite(RED_LED, HIGH);
        delay(250);
        digitalWrite(RED_LED, LOW);
        delay(150);
    }
}

// ============================================
// OLED HELPER
// ============================================
void oledMsg(String line1, String line2 = "", String line3 = "") {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(line1);
    if (line2.length()) display.println(line2);
    if (line3.length()) display.println(line3);
    display.display();
}

// ============================================
// SETUP
// ============================================
void setup() {
    // Silence buzzer immediately — before anything else
    pinMode(BUZZER, OUTPUT);
    digitalWrite(BUZZER, LOW);
    noTone(BUZZER);

    Serial.begin(115200);
    delay(1000);

    Serial.println("========================================");
    Serial.println("Smart Fingerprint System");
    Serial.println("========================================");

    pinMode(GREEN_LED, OUTPUT);
    pinMode(RED_LED,   OUTPUT);
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED,   LOW);

    // LED test on boot — confirms wiring before anything else
    Serial.println("Testing LEDs...");
    digitalWrite(GREEN_LED, HIGH); delay(400); digitalWrite(GREEN_LED, LOW);
    delay(100);
    digitalWrite(RED_LED,   HIGH); delay(400); digitalWrite(RED_LED,   LOW);
    Serial.println("LED test done. If both blinked, wiring is correct.");

    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("OLED not found!");
        while (1);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    oledMsg("Fingerprint System", "Initializing...");
    delay(1000);

    mySerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX, FINGERPRINT_TX);
    delay(100);
    finger.begin(57600);

    if (finger.verifyPassword()) {
        Serial.println("Sensor found!");
        oledMsg("Sensor Ready");
        blinkGreen(2);
        delay(500);
    } else {
        Serial.println("Sensor not found! Check wiring.");
        oledMsg("Sensor Error!", "Check wiring");
        blinkRed(3);
        beepWrong(1);   // Only wrong events beep
        while (1);
    }

    findNextSlot();

    oledMsg("Place Finger");

    Serial.println("System Ready!");
    Serial.print("Next available slot: ");
    Serial.println(nextSlot);
    Serial.println("========================================");
    Serial.println("Commands: enroll | scan | delete | list | help");
    Serial.println("========================================");
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
    // Guarantee buzzer is off at the top of every loop iteration
    silenceBuzzer();

    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        input.toLowerCase();
        if (input.length() > 0) {
            processCommand(input);
        }
    }

    if (!isEnrolling) {
        int result = scanFingerprint();

        if (result == -2) {
            // No finger — do nothing, just keep looping
        } else if (result == -1) {
            // Sensor read error
            oledMsg("Read Error");
            blinkRed(2);
            // No beep — not a wrong match, just a bad read
            delay(500);
            oledMsg("Place Finger");
        } else if (result == 0) {
            // Finger detected but NOT in database — wrong match
            handleUnknownFingerprint();
        } else {
            // Finger found in database — correct match
            handleRegisteredFingerprint(result);
        }
    }

    delay(200);
}

// ============================================
// SCAN FINGERPRINT
// ============================================
int scanFingerprint() {
    int p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) return -2;
    if (p != FINGERPRINT_OK)       return -1;

    p = finger.image2Tz();
    if (p != FINGERPRINT_OK) return -1;

    p = finger.fingerFastSearch();
    if (p == FINGERPRINT_OK)       return finger.fingerID;
    if (p == FINGERPRINT_NOTFOUND) return 0;
    return -1;
}

// ============================================
// CORRECT MATCH — green LED only, NO buzzer
// ============================================
void handleRegisteredFingerprint(int slot) {
    Serial.println("========================================");
    Serial.println("FINGERPRINT MATCH!");
    Serial.print("Slot: ");
    Serial.println(slot);
    Serial.println("========================================");

    oledMsg("FOUND!", "Slot: " + String(slot));

    // GREEN LED only — buzzer stays silent
    blinkGreen(2);
    delay(1500);

    oledMsg("Place Finger");
}

// ============================================
// WRONG MATCH / UNKNOWN — red LED + buzzer
// ============================================
void handleUnknownFingerprint() {
    Serial.println("========================================");
    Serial.println("FINGERPRINT NOT FOUND!");
    Serial.println("========================================");

    oledMsg("NOT FOUND", "See Librarian");

    // RED LED + buzzer — the only place buzzer fires in normal operation
    beepWrong(2);

    oledMsg("Place Finger");
}

// ============================================
// ENROLL FINGERPRINT
// ============================================
void enrollFingerprint() {
    isEnrolling = true;
    silenceBuzzer();

    Serial.println("========================================");
    Serial.println("ENROLLING NEW FINGERPRINT");
    Serial.println("========================================");

    findNextSlot();

    if (nextSlot > 127) {
        Serial.println("Sensor full! Cannot enroll more.");
        oledMsg("Sensor Full!");
        beepWrong(1);
        isEnrolling = false;
        oledMsg("Place Finger");
        return;
    }

    Serial.print("Enrolling to slot: ");
    Serial.println(nextSlot);

    // ---- Step 1: First finger placement ----
    oledMsg("ENROLLMENT", "Slot: " + String(nextSlot), "Place Finger (1)");
    Serial.println("Place finger (1st time)...");

    int p = FINGERPRINT_NOFINGER;
    while (p == FINGERPRINT_NOFINGER) { p = finger.getImage(); delay(100); }

    if (p != FINGERPRINT_OK) {
        Serial.println("Sensor error on 1st read.");
        oledMsg("Read Error"); blinkRed(2); beepWrong(1); delay(1000);
        isEnrolling = false; oledMsg("Place Finger"); return;
    }
    blinkGreen(1);

    p = finger.image2Tz(1);
    if (p != FINGERPRINT_OK) {
        Serial.println("Image conversion failed (1st).");
        oledMsg("Convert Error"); blinkRed(2); beepWrong(1); delay(1000);
        isEnrolling = false; oledMsg("Place Finger"); return;
    }
    Serial.println("Image 1 captured.");

    // ---- Step 2: Remove finger ----
    oledMsg("Remove Finger");
    Serial.println("Remove finger...");
    delay(2000);
    while (finger.getImage() != FINGERPRINT_NOFINGER) { delay(100); }
    delay(500);
    Serial.println("Finger removed.");

    // ---- Step 3: Second finger placement ----
    oledMsg("ENROLLMENT", "Slot: " + String(nextSlot), "Place Finger (2)");
    Serial.println("Place same finger again (2nd time)...");

    p = FINGERPRINT_NOFINGER;
    while (p == FINGERPRINT_NOFINGER) { p = finger.getImage(); delay(100); }

    if (p != FINGERPRINT_OK) {
        Serial.println("Sensor error on 2nd read.");
        oledMsg("Read Error"); blinkRed(2); beepWrong(1); delay(1000);
        isEnrolling = false; oledMsg("Place Finger"); return;
    }
    blinkGreen(1);

    p = finger.image2Tz(2);
    if (p != FINGERPRINT_OK) {
        Serial.println("Image conversion failed (2nd).");
        oledMsg("Convert Error"); blinkRed(2); beepWrong(1); delay(1000);
        isEnrolling = false; oledMsg("Place Finger"); return;
    }
    Serial.println("Image 2 captured.");

    // ---- Step 4: Create model ----
    oledMsg("Creating Model...");
    p = finger.createModel();
    if (p == FINGERPRINT_ENROLLMISMATCH) {
        Serial.println("Fingerprints did not match!");
        oledMsg("MISMATCH", "Try again"); beepWrong(2); delay(2000);
        isEnrolling = false; oledMsg("Place Finger"); return;
    } else if (p != FINGERPRINT_OK) {
        Serial.println("Model creation failed.");
        oledMsg("Model Error"); beepWrong(1); delay(1500);
        isEnrolling = false; oledMsg("Place Finger"); return;
    }
    Serial.println("Model created.");

    // ---- Step 5: Store model ----
    p = finger.storeModel(nextSlot);
    if (p == FINGERPRINT_OK) {
        Serial.println("========================================");
        Serial.println("ENROLLMENT SUCCESSFUL!");
        Serial.print("Fingerprint Slot: ");
        Serial.println(nextSlot);
        Serial.println("========================================");

        oledMsg("ENROLLED!", "Slot: " + String(nextSlot));
        blinkGreen(3);  // Green only — enrollment success has no buzzer
        delay(2000);

        nextSlot++;
        Serial.print("Next available slot: ");
        Serial.println(nextSlot);
    } else {
        Serial.println("Failed to store fingerprint.");
        oledMsg("Store Failed"); beepWrong(1); delay(1500);
    }

    silenceBuzzer();
    isEnrolling = false;
    oledMsg("Place Finger");
}

// ============================================
// FIND NEXT AVAILABLE SLOT
// ============================================
void findNextSlot() {
    for (int slot = 1; slot <= 127; slot++) {
        if (finger.loadModel(slot) != FINGERPRINT_OK) {
            nextSlot = slot;
            return;
        }
    }
    nextSlot = 128; // Sensor full
}

// ============================================
// SERIAL COMMANDS
// ============================================
void processCommand(String input) {
    Serial.println(">> " + input);
    if      (input == "enroll") enrollFingerprint();
    else if (input == "scan")   manualScan();
    else if (input == "delete") deleteFingerprint();
    else if (input == "list")   listEnrolledFingerprints();
    else if (input == "help")   showHelp();
    else Serial.println("Unknown command. Type 'help'.");
}

void showHelp() {
    Serial.println("========================================");
    Serial.println("Commands:");
    Serial.println("  enroll   - Enroll a fingerprint");
    Serial.println("  scan     - Manually scan");
    Serial.println("  delete   - Delete a slot");
    Serial.println("  list     - List enrolled fingerprints");
    Serial.println("  help     - Show this menu");
    Serial.println("========================================");
}

// ============================================
// MANUAL SCAN
// ============================================
void manualScan() {
    Serial.println("========================================");
    Serial.println("MANUAL SCAN — place finger on sensor");
    Serial.println("========================================");
    oledMsg("Scanning...");

    int result = scanFingerprint();

    if      (result == -2) { Serial.println("No finger detected."); oledMsg("No Finger"); }
    else if (result == -1) { Serial.println("Read error.");         oledMsg("Read Error"); blinkRed(2); delay(500); }
    else if (result ==  0) { Serial.println("Not found.");          oledMsg("NOT FOUND");  beepWrong(2); delay(1500); }
    else {
        Serial.println("Found! Slot: " + String(result));
        oledMsg("FOUND!", "Slot: " + String(result));
        blinkGreen(2);
        delay(1500);
    }

    oledMsg("Place Finger");
}

// ============================================
// DELETE FINGERPRINT
// ============================================
void deleteFingerprint() {
    Serial.println("========================================");
    Serial.println("DELETE — enter slot number (1-127):");
    while (!Serial.available()) { delay(100); }
    int slot = Serial.parseInt();
    Serial.println(slot);

    int p = finger.deleteModel(slot);
    if (p == FINGERPRINT_OK) {
        Serial.println("Slot " + String(slot) + " deleted.");
        oledMsg("Deleted", "Slot: " + String(slot));
        blinkGreen(1);
        delay(1500);
    } else {
        Serial.println("Delete failed.");
        oledMsg("Delete Failed");
        beepWrong(1);
        delay(1500);
    }
    oledMsg("Place Finger");
}

// ============================================
// LIST ENROLLED FINGERPRINTS
// ============================================
void listEnrolledFingerprints() {
    Serial.println("========================================");
    Serial.println("ENROLLED FINGERPRINTS:");
    int found = 0;
    for (int i = 1; i <= 127; i++) {
        if (finger.loadModel(i) == FINGERPRINT_OK) {
            Serial.println("  Slot " + String(i) + ": Enrolled");
            found++;
        }
    }
    if (found == 0) Serial.println("  None enrolled.");
    else Serial.println("Total: " + String(found));
    Serial.println("========================================");
}
