// ============================================================
// TRON IDENTITY DISC ESP32-C3 SOURCE CODE

// Created by EBxDESIGN

// Please refer to the provided wiring diagram. 
// Wiring diagram version 1.0
// ============================================================

#include <FastLED.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ============================================================
// LED CONFIGURATION
// ============================================================

#define INNER_RING_PIN    3
#define OUTER_RING_PIN    10

#define INNER_RING_LEDS   25
#define OUTER_RING_LEDS   40

#define MASTER_BRIGHTNESS 255

CRGB innerRing[INNER_RING_LEDS];
CRGB outerRing[OUTER_RING_LEDS];


// ============================================================
// BUTTON CONFIGURATION
// ============================================================

#define BUTTON_PIN 6

// External 10K pull-up resistor:
// Button released = HIGH
// Button pressed  = LOW

const unsigned long DEBOUNCE_TIME = 40;
const unsigned long LONG_PRESS_ON_TIME = 3000;
const unsigned long LONG_PRESS_OFF_TIME = 5000;
const unsigned long MULTI_PRESS_TIMEOUT = 500;


// ============================================================
// BLE CONFIGURATION
// ============================================================

#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_RX   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_TX   "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLECharacteristic *txCharacteristic;
bool deviceConnected = false;


// ============================================================
// CROSS-TASK STATE PROTECTION
// ============================================================
//
// processCommand() is invoked from the BLE stack's own FreeRTOS
// task (via onWrite()), while updateInnerRing()/updateOuterRing()/
// updateButton() run in the main loop() task. The variables below
// are written from BLE and read from loop(), so every access goes
// through this spinlock to prevent torn reads (e.g. a CRGB or mode
// caught half-updated), which is what was causing the inner-ring
// glitches.

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;


// ============================================================
// LED SETTINGS
// ============================================================

CRGB innerColor = CRGB(0, 255, 255);
CRGB outerColor = CRGB(0, 255, 255);

uint8_t innerBrightness = 255;  // 100%
uint8_t outerBrightness = 255;  // 100%


// ============================================================
// MODES
// ============================================================

enum InnerMode {
  INNER_OFF,
  INNER_SOLID,
  INNER_LOADING,
  INNER_CHASE
};

enum OuterMode {
  OUTER_OFF,
  OUTER_SOLID,
  OUTER_PULSE,
  OUTER_BLADE
};

InnerMode innerMode = INNER_LOADING;
OuterMode outerMode = OUTER_PULSE;


// ============================================================
// BUTTON POWER STATE
// ============================================================

bool buttonPoweredOn = true;


// ============================================================
// BUTTON EFFECTS
// ============================================================

enum ButtonEffect {

  EFFECT_OUTER_SOLID_INNER_SOLID,

  EFFECT_OUTER_SOLID_INNER_LOADING,

  EFFECT_OUTER_SOLID_INNER_CHASE,

  EFFECT_OUTER_PULSE_INNER_SOLID,

  EFFECT_OUTER_BLADE_INNER_SOLID,

  EFFECT_OUTER_OFF_INNER_SOLID,

  EFFECT_OUTER_OFF_INNER_LOADING,

  EFFECT_OUTER_OFF_INNER_CHASE,

  EFFECT_COUNT
};

uint8_t currentButtonEffect = 0;


// ============================================================
// BUTTON COLOURS
// ============================================================

CRGB buttonColors[] = {

  CRGB(0, 255, 255),   // Aqua
  CRGB(255, 0, 0),     // Red
  CRGB(255, 80, 0),    // Orange
  CRGB(255, 255, 255), // White
  CRGB(180, 0, 255),   // Purple
  CRGB(255, 20, 147)   // Pink

};

const char* buttonColorNames[] = {

  "Aqua",
  "Red",
  "Orange",
  "White",
  "Purple",
  "Pink"

};

uint8_t currentButtonColor = 0;


// ============================================================
// BUTTON BRIGHTNESS
// ============================================================

uint8_t buttonBrightnessLevels[] = {

  255,  // 100%
  128,  // 50%
  64    // 25%

};

const char* buttonBrightnessNames[] = {

  "100%",
  "50%",
  "25%"

};

uint8_t currentButtonBrightness = 0;  // starts at 100%


// ============================================================
// BUTTON STATE VARIABLES
// ============================================================

bool buttonStableState = HIGH;
bool buttonLastReading = HIGH;

unsigned long buttonLastDebounce = 0;
unsigned long buttonPressStart = 0;

// Separate flags for each long-press threshold. A single shared
// flag previously meant that once the 3s "ON" trigger fired, the
// 5s "OFF" trigger could never fire during the same hold.
bool buttonOnTriggered = false;
bool buttonOffTriggered = false;

uint8_t buttonPressCount = 0;
unsigned long lastButtonRelease = 0;


// ============================================================
// INNER ANIMATION VARIABLES
// ============================================================

unsigned long innerLastUpdate = 0;
int innerCurrentLED = 0;

const unsigned long loadingInterval = 80;


// ============================================================
// OUTER PULSE VARIABLES
// ============================================================

unsigned long outerLastUpdate = 0;

uint8_t pulseBrightness = 20;
int8_t pulseDirection = 1;

const unsigned long pulseInterval = 5;


// ============================================================
// OUTER BLADE VARIABLES
// ============================================================

unsigned long bladeLastUpdate = 0;

int bladePosition = 0;

const unsigned long bladeInterval = 20;
const uint8_t bladeLength = 8;
const uint8_t bladeBright = 255;
const uint8_t bladeDim = 12;


// ============================================================
// BLE CALLBACKS
// ============================================================

class MyServerCallbacks : public BLEServerCallbacks {

  void onConnect(BLEServer* pServer) {

    deviceConnected = true;

    Serial.println("BLE connected");
  }

  void onDisconnect(BLEServer* pServer) {

    deviceConnected = false;

    Serial.println("BLE disconnected");

    BLEDevice::startAdvertising();
  }
};


// ============================================================
// SEND MESSAGE BACK TO PHONE
// ============================================================

void sendBLEMessage(String message) {

  if (deviceConnected) {

    txCharacteristic->setValue(message.c_str());
    txCharacteristic->notify();
  }

  Serial.println(message);
}


// ============================================================
// APPLY BUTTON COLOUR
// ============================================================

void applyButtonColor() {

  CRGB c = buttonColors[currentButtonColor];

  portENTER_CRITICAL(&stateMux);
  innerColor = c;
  outerColor = c;
  portEXIT_CRITICAL(&stateMux);

  Serial.print("Button colour: ");
  Serial.println(buttonColorNames[currentButtonColor]);
}


// ============================================================
// APPLY BUTTON BRIGHTNESS
// ============================================================

void applyButtonBrightness() {

  uint8_t b = buttonBrightnessLevels[currentButtonBrightness];

  portENTER_CRITICAL(&stateMux);
  innerBrightness = b;
  outerBrightness = b;
  portEXIT_CRITICAL(&stateMux);

  Serial.print("Button brightness: ");
  Serial.println(buttonBrightnessNames[currentButtonBrightness]);
}


// ============================================================
// APPLY BUTTON EFFECT
// ============================================================

void applyButtonEffect() {

  InnerMode newInnerMode = innerMode;
  OuterMode newOuterMode = outerMode;
  bool clearInner = false;
  bool resetBlade = false;
  bool resetPulse = false;

  switch (currentButtonEffect) {

    // --------------------------------------------------------
    // 1. OUTER SOLID + INNER SOLID
    // --------------------------------------------------------

    case EFFECT_OUTER_SOLID_INNER_SOLID:

      newOuterMode = OUTER_SOLID;
      newInnerMode = INNER_SOLID;

      break;


    // --------------------------------------------------------
    // 2. OUTER SOLID + INNER LOADING
    // --------------------------------------------------------

    case EFFECT_OUTER_SOLID_INNER_LOADING:

      newOuterMode = OUTER_SOLID;
      newInnerMode = INNER_LOADING;

      clearInner = true;

      break;


    // --------------------------------------------------------
    // 3. OUTER SOLID + INNER CHASE
    // --------------------------------------------------------

    case EFFECT_OUTER_SOLID_INNER_CHASE:

      newOuterMode = OUTER_SOLID;
      newInnerMode = INNER_CHASE;

      clearInner = true;

      break;


    // --------------------------------------------------------
    // 4. OUTER PULSE + INNER SOLID
    // --------------------------------------------------------

    case EFFECT_OUTER_PULSE_INNER_SOLID:

      newOuterMode = OUTER_PULSE;
      newInnerMode = INNER_SOLID;

      resetPulse = true;

      break;


    // --------------------------------------------------------
    // 5. OUTER BLADE + INNER SOLID
    // --------------------------------------------------------

    case EFFECT_OUTER_BLADE_INNER_SOLID:

      newOuterMode = OUTER_BLADE;
      newInnerMode = INNER_SOLID;

      resetBlade = true;

      break;


    // --------------------------------------------------------
    // 6. OUTER OFF + INNER SOLID
    // --------------------------------------------------------

    case EFFECT_OUTER_OFF_INNER_SOLID:

      newOuterMode = OUTER_OFF;
      newInnerMode = INNER_SOLID;

      break;


    // --------------------------------------------------------
    // 7. OUTER OFF + INNER LOADING
    // --------------------------------------------------------

    case EFFECT_OUTER_OFF_INNER_LOADING:

      newOuterMode = OUTER_OFF;
      newInnerMode = INNER_LOADING;

      clearInner = true;

      break;


    // --------------------------------------------------------
    // 8. OUTER OFF + INNER CHASE
    // --------------------------------------------------------

    case EFFECT_OUTER_OFF_INNER_CHASE:

      newOuterMode = OUTER_OFF;
      newInnerMode = INNER_CHASE;

      clearInner = true;

      break;
  }

  portENTER_CRITICAL(&stateMux);
  innerMode = newInnerMode;
  outerMode = newOuterMode;
  portEXIT_CRITICAL(&stateMux);

  innerCurrentLED = 0;

  if (clearInner) {

    fill_solid(
      innerRing,
      INNER_RING_LEDS,
      CRGB::Black
    );
  }

  if (resetPulse) {

    pulseBrightness = 20;
    pulseDirection = 1;
  }

  if (resetBlade) {

    bladePosition = 0;
    bladeLastUpdate = millis();
  }

  Serial.print("Button effect: ");
  Serial.println(currentButtonEffect + 1);
}


// ============================================================
// BUTTON TURN ON
// ============================================================

void buttonTurnOn() {

  buttonPoweredOn = true;

  applyButtonColor();
  applyButtonBrightness();
  applyButtonEffect();

  Serial.println("BUTTON: LEDs ON");
}


// ============================================================
// BUTTON TURN OFF
// ============================================================

void buttonTurnOff() {

  buttonPoweredOn = false;

  portENTER_CRITICAL(&stateMux);
  innerMode = INNER_OFF;
  outerMode = OUTER_OFF;
  portEXIT_CRITICAL(&stateMux);

  Serial.println("BUTTON: LEDs OFF");
}


// ============================================================
// BUTTON SINGLE PRESS
// ============================================================

void buttonSinglePress() {

  currentButtonEffect++;

  if (currentButtonEffect >= EFFECT_COUNT) {

    currentButtonEffect = 0;
  }

  // A quick press turns the LEDs on if they are currently off
  if (!buttonPoweredOn) {

    buttonPoweredOn = true;
  }

  applyButtonEffect();

  Serial.print("BUTTON SINGLE PRESS - EFFECT ");
  Serial.println(currentButtonEffect + 1);
}


// ============================================================
// BUTTON DOUBLE PRESS
// ============================================================

void buttonDoublePress() {

  currentButtonColor++;

  if (currentButtonColor >= 6) {

    currentButtonColor = 0;
  }

  applyButtonColor();

  // Keep current effect running
  if (buttonPoweredOn) {

    applyButtonEffect();
  }

  Serial.print("BUTTON DOUBLE PRESS - COLOR: ");
  Serial.println(buttonColorNames[currentButtonColor]);
}


// ============================================================
// BUTTON TRIPLE PRESS
// ============================================================

void buttonTriplePress() {

  currentButtonBrightness++;

  if (currentButtonBrightness >= 3) {

    currentButtonBrightness = 0;
  }

  applyButtonBrightness();

  // Keep current effect running
  if (buttonPoweredOn) {

    applyButtonEffect();
  }

  Serial.print("BUTTON TRIPLE PRESS - BRIGHTNESS: ");
  Serial.println(buttonBrightnessNames[currentButtonBrightness]);
}


// ============================================================
// PROCESS BUTTON PRESS COUNT
// ============================================================

void processButtonPresses() {

  if (
    buttonPressCount > 0 &&
    millis() - lastButtonRelease >= MULTI_PRESS_TIMEOUT
  ) {

    if (buttonPressCount == 1) {

      buttonSinglePress();
    }

    else if (buttonPressCount == 2) {

      buttonDoublePress();
    }

    else if (buttonPressCount >= 3) {

      buttonTriplePress();
    }

    buttonPressCount = 0;
  }
}


// ============================================================
// BUTTON UPDATE
// ============================================================

void updateButton() {

  bool reading = digitalRead(BUTTON_PIN);


  // ----------------------------------------------------------
  // DEBOUNCE
  // ----------------------------------------------------------

  if (reading != buttonLastReading) {

    buttonLastDebounce = millis();
  }


  if (
    millis() - buttonLastDebounce > DEBOUNCE_TIME
  ) {

    if (reading != buttonStableState) {

      buttonStableState = reading;


      // ------------------------------------------------------
      // BUTTON PRESSED
      // ------------------------------------------------------

      if (buttonStableState == LOW) {

        buttonPressStart = millis();

        buttonOnTriggered = false;
        buttonOffTriggered = false;
      }


      // ------------------------------------------------------
      // BUTTON RELEASED
      // ------------------------------------------------------

      else {

        unsigned long pressDuration =
          millis() - buttonPressStart;


        // Only count short presses
        // Long presses have already performed their function

        if (
          pressDuration < LONG_PRESS_ON_TIME &&
          !buttonOnTriggered &&
          !buttonOffTriggered
        ) {

          buttonPressCount++;

          lastButtonRelease = millis();
        }
      }
    }
  }


  buttonLastReading = reading;


  // ----------------------------------------------------------
  // CHECK FOR LONG PRESS
  // ----------------------------------------------------------
  //
  // The 5-second (OFF) threshold is checked BEFORE the 3-second
  // (ON) threshold, and each has its own "already triggered"
  // flag. This way, if the button is held past 5 seconds, OFF
  // correctly fires even though ON already fired earlier in the
  // same hold - previously a single shared flag blocked this.

  if (buttonStableState == LOW) {

    unsigned long heldTime =
      millis() - buttonPressStart;


    // --------------------------------------------------------
    // 5 SECOND HOLD = OFF
    // --------------------------------------------------------

    if (
      heldTime >= LONG_PRESS_OFF_TIME &&
      !buttonOffTriggered
    ) {

      buttonOffTriggered = true;

      buttonPressCount = 0;

      buttonTurnOff();
    }


    // --------------------------------------------------------
    // 3 SECOND HOLD = ON
    // --------------------------------------------------------

    else if (
      heldTime >= LONG_PRESS_ON_TIME &&
      !buttonOnTriggered
    ) {

      buttonOnTriggered = true;

      buttonPressCount = 0;

      buttonTurnOn();
    }
  }


  // ----------------------------------------------------------
  // PROCESS SINGLE / DOUBLE / TRIPLE PRESS
  // ----------------------------------------------------------

  processButtonPresses();
}


// ============================================================
// OUTER RING - RAPID SPINNING BLADE
// ============================================================

void updateOuterBlade(const CRGB &color, uint8_t brightness) {

  if (millis() - bladeLastUpdate >= bladeInterval) {

    bladeLastUpdate = millis();

    fill_solid(
      outerRing,
      OUTER_RING_LEDS,
      CRGB::Black
    );


    for (int i = 0; i < OUTER_RING_LEDS; i++) {

      int distance =
        (i - bladePosition + OUTER_RING_LEDS)
        % OUTER_RING_LEDS;


      // ------------------------------------------------------
      // MAIN BLADE
      // ------------------------------------------------------

      if (distance < bladeLength) {

        uint8_t bladeFade =
          map(
            distance,
            0,
            bladeLength - 1,
            bladeBright,
            30
          );

        outerRing[i] = color;

        uint16_t finalBrightness =
          ((uint16_t)bladeFade *
           brightness) / 255;

        outerRing[i].nscale8(
          finalBrightness
        );
      }


      // ------------------------------------------------------
      // BACKGROUND GLOW
      // ------------------------------------------------------

      else {

        outerRing[i] = color;

        uint16_t finalBrightness =
          ((uint16_t)bladeDim *
           brightness) / 255;

        outerRing[i].nscale8(
          finalBrightness
        );
      }
    }


    // Move blade

    bladePosition++;

    if (bladePosition >= OUTER_RING_LEDS) {

      bladePosition = 0;
    }
  }
}


// ============================================================
// PARSE BLE COMMAND
// ============================================================
//
// NOTE: this runs on the BLE stack's task, not the loop() task.
// All shared-state writes below are wrapped in the stateMux
// critical section so loop() never reads a half-updated value.

void processCommand(String command) {

  command.trim();
  command.toUpperCase();

  Serial.print("Command received: ");
  Serial.println(command);


  // ==========================================================
  // ALL OFF
  // ==========================================================

  if (command == "ALL OFF") {

    portENTER_CRITICAL(&stateMux);
    innerMode = INNER_OFF;
    outerMode = OUTER_OFF;
    portEXIT_CRITICAL(&stateMux);

    buttonPoweredOn = false;

    sendBLEMessage("OK ALL OFF");

    return;
  }


  // ==========================================================
  // ALL ON
  // ==========================================================

  if (command == "ALL ON") {

    portENTER_CRITICAL(&stateMux);
    innerMode = INNER_SOLID;
    outerMode = OUTER_SOLID;
    portEXIT_CRITICAL(&stateMux);

    buttonPoweredOn = true;

    sendBLEMessage("OK ALL ON");

    return;
  }


  // ==========================================================
  // INNER COLOR
  // ==========================================================

  if (command.startsWith("INNER COLOR")) {

    int r, g, b;

    if (
      sscanf(
        command.c_str(),
        "INNER COLOR %d %d %d",
        &r,
        &g,
        &b
      ) == 3
    ) {

      r = constrain(r, 0, 255);
      g = constrain(g, 0, 255);
      b = constrain(b, 0, 255);

      portENTER_CRITICAL(&stateMux);
      innerColor = CRGB(r, g, b);
      portEXIT_CRITICAL(&stateMux);

      sendBLEMessage("OK INNER COLOR");

    }

    else {

      sendBLEMessage("ERROR INNER COLOR");
    }

    return;
  }


  // ==========================================================
  // OUTER COLOR
  // ==========================================================

  if (command.startsWith("OUTER COLOR")) {

    int r, g, b;

    if (
      sscanf(
        command.c_str(),
        "OUTER COLOR %d %d %d",
        &r,
        &g,
        &b
      ) == 3
    ) {

      r = constrain(r, 0, 255);
      g = constrain(g, 0, 255);
      b = constrain(b, 0, 255);

      portENTER_CRITICAL(&stateMux);
      outerColor = CRGB(r, g, b);
      portEXIT_CRITICAL(&stateMux);

      sendBLEMessage("OK OUTER COLOR");

    }

    else {

      sendBLEMessage("ERROR OUTER COLOR");
    }

    return;
  }


  // ==========================================================
  // INNER BRIGHTNESS
  // ==========================================================

  if (command.startsWith("INNER BRIGHT")) {

    int value;

    if (
      sscanf(
        command.c_str(),
        "INNER BRIGHT %d",
        &value
      ) == 1
    ) {

      portENTER_CRITICAL(&stateMux);
      innerBrightness =
        constrain(value, 0, 255);
      portEXIT_CRITICAL(&stateMux);

      sendBLEMessage("OK INNER BRIGHT");

    }

    else {

      sendBLEMessage("ERROR INNER BRIGHT");
    }

    return;
  }


  // ==========================================================
  // OUTER BRIGHTNESS
  // ==========================================================

  if (command.startsWith("OUTER BRIGHT")) {

    int value;

    if (
      sscanf(
        command.c_str(),
        "OUTER BRIGHT %d",
        &value
      ) == 1
    ) {

      portENTER_CRITICAL(&stateMux);
      outerBrightness =
        constrain(value, 0, 255);
      portEXIT_CRITICAL(&stateMux);

      sendBLEMessage("OK OUTER BRIGHT");

    }

    else {

      sendBLEMessage("ERROR OUTER BRIGHT");
    }

    return;
  }


  // ==========================================================
  // INNER MODE - OFF
  // ==========================================================

  if (command == "INNER MODE OFF") {

    portENTER_CRITICAL(&stateMux);
    innerMode = INNER_OFF;
    portEXIT_CRITICAL(&stateMux);

    sendBLEMessage("OK INNER OFF");

    return;
  }


  // ==========================================================
  // INNER MODE - SOLID
  // ==========================================================

  if (command == "INNER MODE SOLID") {

    portENTER_CRITICAL(&stateMux);
    innerMode = INNER_SOLID;
    portEXIT_CRITICAL(&stateMux);

    sendBLEMessage("OK INNER SOLID");

    return;
  }


  // ==========================================================
  // INNER MODE - LOADING
  // ==========================================================

  if (command == "INNER MODE LOADING") {

    portENTER_CRITICAL(&stateMux);
    innerMode = INNER_LOADING;
    portEXIT_CRITICAL(&stateMux);

    innerCurrentLED = 0;

    fill_solid(
      innerRing,
      INNER_RING_LEDS,
      CRGB::Black
    );

    sendBLEMessage("OK INNER LOADING");

    return;
  }


  // ==========================================================
  // INNER MODE - CHASE
  // ==========================================================

  if (command == "INNER MODE CHASE") {

    portENTER_CRITICAL(&stateMux);
    innerMode = INNER_CHASE;
    portEXIT_CRITICAL(&stateMux);

    innerCurrentLED = 0;

    fill_solid(
      innerRing,
      INNER_RING_LEDS,
      CRGB::Black
    );

    sendBLEMessage("OK INNER CHASE");

    return;
  }


  // ==========================================================
  // OUTER MODE - OFF
  // ==========================================================

  if (command == "OUTER MODE OFF") {

    portENTER_CRITICAL(&stateMux);
    outerMode = OUTER_OFF;
    portEXIT_CRITICAL(&stateMux);

    sendBLEMessage("OK OUTER OFF");

    return;
  }


  // ==========================================================
  // OUTER MODE - SOLID
  // ==========================================================

  if (command == "OUTER MODE SOLID") {

    portENTER_CRITICAL(&stateMux);
    outerMode = OUTER_SOLID;
    portEXIT_CRITICAL(&stateMux);

    sendBLEMessage("OK OUTER SOLID");

    return;
  }


  // ==========================================================
  // OUTER MODE - PULSE
  // ==========================================================

  if (command == "OUTER MODE PULSE") {

    portENTER_CRITICAL(&stateMux);
    outerMode = OUTER_PULSE;
    portEXIT_CRITICAL(&stateMux);

    pulseBrightness = 20;
    pulseDirection = 1;

    sendBLEMessage("OK OUTER PULSE");

    return;
  }


  // ==========================================================
  // OUTER MODE - BLADE
  // ==========================================================

  if (command == "OUTER MODE BLADE") {

    portENTER_CRITICAL(&stateMux);
    outerMode = OUTER_BLADE;
    portEXIT_CRITICAL(&stateMux);

    bladePosition = 0;
    bladeLastUpdate = millis();

    sendBLEMessage("OK OUTER BLADE");

    return;
  }


  // ==========================================================
  // UNKNOWN COMMAND
  // ==========================================================

  sendBLEMessage("ERROR UNKNOWN COMMAND");
}


// ============================================================
// BLE RX CALLBACK
// ============================================================

class MyCallbacks : public BLECharacteristicCallbacks {

  void onWrite(
    BLECharacteristic *pCharacteristic
  ) {

    String command =
      pCharacteristic->getValue().c_str();

    if (command.length() > 0) {

      processCommand(command);
    }
  }
};


// ============================================================
// INNER RING
// ============================================================

void updateInnerRing() {

  // Snapshot the shared state atomically so the whole function
  // works off one consistent view, instead of re-reading globals
  // that BLE could be mutating mid-frame.

  InnerMode mode;
  CRGB color;
  uint8_t brightness;

  portENTER_CRITICAL(&stateMux);
  mode = innerMode;
  color = innerColor;
  brightness = innerBrightness;
  portEXIT_CRITICAL(&stateMux);


  // ----------------------------------------------------------
  // OFF
  // ----------------------------------------------------------

  if (mode == INNER_OFF) {

    fill_solid(
      innerRing,
      INNER_RING_LEDS,
      CRGB::Black
    );

    return;
  }


  // ----------------------------------------------------------
  // SOLID
  // ----------------------------------------------------------

  if (mode == INNER_SOLID) {

    fill_solid(
      innerRing,
      INNER_RING_LEDS,
      color
    );

    for (int i = 0; i < INNER_RING_LEDS; i++) {

      innerRing[i].nscale8(
        brightness
      );
    }

    return;
  }


  // ----------------------------------------------------------
  // LOADING
  // ----------------------------------------------------------

  if (mode == INNER_LOADING) {

    if (
      millis() - innerLastUpdate >=
      loadingInterval
    ) {

      innerLastUpdate = millis();

      innerRing[innerCurrentLED] =
        color;

      innerRing[innerCurrentLED].nscale8(
        brightness
      );

      innerCurrentLED++;

      if (
        innerCurrentLED >= INNER_RING_LEDS
      ) {

        innerCurrentLED = 0;

        fill_solid(
          innerRing,
          INNER_RING_LEDS,
          CRGB::Black
        );
      }
    }

    return;
  }


  // ----------------------------------------------------------
  // CHASE
  // ----------------------------------------------------------

  if (mode == INNER_CHASE) {

    static unsigned long lastChase = 0;

    if (
      millis() - lastChase >= 50
    ) {

      lastChase = millis();

      fill_solid(
        innerRing,
        INNER_RING_LEDS,
        CRGB::Black
      );

      innerRing[innerCurrentLED] =
        color;

      innerRing[innerCurrentLED].nscale8(
        brightness
      );

      innerCurrentLED++;

      if (
        innerCurrentLED >= INNER_RING_LEDS
      ) {

        innerCurrentLED = 0;
      }
    }

    return;
  }
}


// ============================================================
// OUTER RING
// ============================================================

void updateOuterRing() {

  // Snapshot the shared state atomically, same reasoning as
  // updateInnerRing() above.

  OuterMode mode;
  CRGB color;
  uint8_t brightness;

  portENTER_CRITICAL(&stateMux);
  mode = outerMode;
  color = outerColor;
  brightness = outerBrightness;
  portEXIT_CRITICAL(&stateMux);


  // ----------------------------------------------------------
  // OFF
  // ----------------------------------------------------------

  if (mode == OUTER_OFF) {

    fill_solid(
      outerRing,
      OUTER_RING_LEDS,
      CRGB::Black
    );

    return;
  }


  // ----------------------------------------------------------
  // SOLID
  // ----------------------------------------------------------

  if (mode == OUTER_SOLID) {

    fill_solid(
      outerRing,
      OUTER_RING_LEDS,
      color
    );

    for (int i = 0; i < OUTER_RING_LEDS; i++) {

      outerRing[i].nscale8(
        brightness
      );
    }

    return;
  }


  // ----------------------------------------------------------
  // PULSE
  // ----------------------------------------------------------

  if (mode == OUTER_PULSE) {

    if (
      millis() - outerLastUpdate >=
      pulseInterval
    ) {

      outerLastUpdate = millis();

      pulseBrightness += pulseDirection;


      if (pulseBrightness >= 255) {

        pulseBrightness = 255;
        pulseDirection = -1;
      }


      if (pulseBrightness <= 20) {

        pulseBrightness = 20;
        pulseDirection = 1;
      }


      fill_solid(
        outerRing,
        OUTER_RING_LEDS,
        color
      );


      uint16_t finalBrightness =
        (
          (uint16_t)pulseBrightness *
          brightness
        ) / 255;


      for (int i = 0; i < OUTER_RING_LEDS; i++) {

        outerRing[i].nscale8(
          finalBrightness
        );
      }
    }

    return;
  }


  // ----------------------------------------------------------
  // BLADE
  // ----------------------------------------------------------

  if (mode == OUTER_BLADE) {

    updateOuterBlade(color, brightness);

    return;
  }
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);


  // ----------------------------------------------------------
  // BUTTON
  // ----------------------------------------------------------

  // External 10K pull-up resistor is being used.
  pinMode(
    BUTTON_PIN,
    INPUT
  );


  // ----------------------------------------------------------
  // LED SETUP
  // ----------------------------------------------------------

  FastLED.addLeds<WS2812B, INNER_RING_PIN, GRB>(
    innerRing,
    INNER_RING_LEDS
  );

  FastLED.addLeds<WS2812B, OUTER_RING_PIN, GRB>(
    outerRing,
    OUTER_RING_LEDS
  );

  FastLED.setBrightness(
    MASTER_BRIGHTNESS
  );


  fill_solid(
    innerRing,
    INNER_RING_LEDS,
    CRGB::Black
  );

  fill_solid(
    outerRing,
    OUTER_RING_LEDS,
    CRGB::Black
  );

  FastLED.show();


  // ----------------------------------------------------------
  // BLE SETUP
  // ----------------------------------------------------------

  BLEDevice::init(
    "TRON IDENTITY DISC V1.0"
  );

  BLEServer *server =
    BLEDevice::createServer();

  server->setCallbacks(
    new MyServerCallbacks()
  );


  BLEService *service =
    server->createService(
      SERVICE_UUID
    );


  // ----------------------------------------------------------
  // TX - ESP32 -> PHONE
  // ----------------------------------------------------------

  txCharacteristic =
    service->createCharacteristic(
      CHARACTERISTIC_TX,
      BLECharacteristic::PROPERTY_NOTIFY
    );

  txCharacteristic->addDescriptor(
    new BLE2902()
  );


  // ----------------------------------------------------------
  // RX - PHONE -> ESP32
  // ----------------------------------------------------------

  BLECharacteristic *rxCharacteristic =
    service->createCharacteristic(
      CHARACTERISTIC_RX,
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_WRITE_NR
    );

  rxCharacteristic->setCallbacks(
    new MyCallbacks()
  );


  service->start();


  // ----------------------------------------------------------
  // START BLE ADVERTISING
  // ----------------------------------------------------------

  BLEAdvertising *advertising =
    BLEDevice::getAdvertising();

  advertising->addServiceUUID(
    SERVICE_UUID
  );

  advertising->setScanResponse(true);

  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();


  // ----------------------------------------------------------
  // STARTUP
  // ----------------------------------------------------------

  Serial.println(
    "================================="
  );

  Serial.println(
    "ESP32 LED Controller Ready"
  );

  Serial.println(
    "BLE name: ESP32 LED Controller"
  );

  Serial.println(
    "Button: GPIO 6"
  );

  Serial.println(
    "================================="
  );
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop() {

  // Button control
  updateButton();

  // LED animations
  updateInnerRing();
  updateOuterRing();

  // Display LEDs
  FastLED.show();

  delay(1);
}
