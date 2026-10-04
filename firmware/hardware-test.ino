#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// =========================
// I2C LCD
// =========================
#define SDA_PIN 8
#define SCL_PIN 9

LiquidCrystal_I2C lcd(0x27, 16, 2);

// =========================
// DIGITAL OUTPUT TEST PINS
// =========================
const int outputPins[] = {
  12,
  13,
  17,
  18,
  10,
  11
};

const int outputCount =
  sizeof(outputPins) / sizeof(outputPins[0]);

// =========================
// INPUT PINS
// =========================
#define INPUT_1 16
#define INPUT_2 15

// =========================
// FAN MOSFET
// =========================
#define FAN_PIN 7

// =========================
// 3W LED DRIVER INPUTS
// =========================
#define LED_1_PIN 4
#define LED_2_PIN 5

// =========================
// BUZZER
// =========================
#define BUZZER_PIN 21

// This code assumes an ACTIVE buzzer.
// HIGH = buzzer sounds
// LOW  = buzzer silent

// =========================
// PWM
// =========================
const int PWM_FREQUENCY = 1000;
const int PWM_RESOLUTION = 8;  // 0-255

// =========================
// TIMING
// =========================

// Output test
unsigned long previousOutputTime = 0;
const unsigned long OUTPUT_INTERVAL = 2000;
int currentOutput = 0;

// Fan test
unsigned long previousFanTime = 0;
const unsigned long FAN_INTERVAL = 1000;
bool fanState = false;

// PWM fade
unsigned long previousPWMTime = 0;
const unsigned long PWM_INTERVAL = 10;

int pwmValue = 0;
int pwmDirection = 1;

// Serial
unsigned long previousSerialTime = 0;
const unsigned long SERIAL_INTERVAL = 200;

// LCD
unsigned long previousLCDTime = 0;
const unsigned long LCD_INTERVAL = 200;


// ====================================================
// SETUP
// ====================================================

void setup() {

  Serial.begin(115200);

  // ==================================================
  // LCD
  // ==================================================

  Wire.begin(SDA_PIN, SCL_PIN);

  // Slower I2C is more tolerant of noise
  Wire.setClock(100000);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("ESP32 TEST");

  lcd.setCursor(0, 1);
  lcd.print("Starting...");


  // ==================================================
  // DIGITAL OUTPUTS
  // ==================================================

  for (int i = 0; i < outputCount; i++) {

    pinMode(
      outputPins[i],
      OUTPUT
    );

    digitalWrite(
      outputPins[i],
      LOW
    );
  }

  // Start with first output ON
  digitalWrite(
    outputPins[0],
    HIGH
  );


  // ==================================================
  // INPUTS
  // ==================================================
  //
  // Assumes buttons connect GPIO -> 3.3V when pressed.
  //
  // Therefore:
  //
  // released = LOW
  // pressed  = HIGH
  //
  // Internal pulldown prevents floating inputs.
  // ==================================================

  pinMode(
    INPUT_1,
    INPUT_PULLDOWN
  );

  pinMode(
    INPUT_2,
    INPUT_PULLDOWN
  );


  // ==================================================
  // FAN
  // ==================================================

  pinMode(
    FAN_PIN,
    OUTPUT
  );

  digitalWrite(
    FAN_PIN,
    LOW
  );


  // ==================================================
  // BUZZER
  // ==================================================

  pinMode(
    BUZZER_PIN,
    OUTPUT
  );

  digitalWrite(
    BUZZER_PIN,
    LOW
  );


  // ==================================================
  // 3W LED PWM
  // ESP32 Arduino Core 3.x
  // ==================================================

  ledcAttach(
    LED_1_PIN,
    PWM_FREQUENCY,
    PWM_RESOLUTION
  );

  ledcAttach(
    LED_2_PIN,
    PWM_FREQUENCY,
    PWM_RESOLUTION
  );

  ledcWrite(
    LED_1_PIN,
    0
  );

  ledcWrite(
    LED_2_PIN,
    0
  );


  delay(1000);

  lcd.clear();


  Serial.println();
  Serial.println("=========================");
  Serial.println("ESP32 hardware test start");
  Serial.println("=========================");
}


// ====================================================
// LOOP
// ====================================================

void loop() {

  unsigned long now =
    millis();


  // ==================================================
  // READ BUTTONS
  // ==================================================

  int input1State =
    digitalRead(INPUT_1);

  int input2State =
    digitalRead(INPUT_2);


  // ==================================================
  // BUZZER
  //
  // ON only while BOTH buttons are pressed
  // ==================================================

  bool buzzerState =
    input1State == HIGH &&
    input2State == HIGH;


  digitalWrite(
    BUZZER_PIN,
    buzzerState
      ? HIGH
      : LOW
  );


  // ==================================================
  // CYCLE DIGITAL OUTPUTS
  // ==================================================

  if (
    now - previousOutputTime >=
    OUTPUT_INTERVAL
  )
  {
    previousOutputTime =
      now;


    // Current output OFF
    digitalWrite(
      outputPins[currentOutput],
      LOW
    );


    // Next output
    currentOutput++;


    if (
      currentOutput >=
      outputCount
    )
    {
      currentOutput = 0;
    }


    // Next output ON
    digitalWrite(
      outputPins[currentOutput],
      HIGH
    );


    Serial.print(
      "Active output changed to GPIO "
    );

    Serial.println(
      outputPins[currentOutput]
    );
  }


  // ==================================================
  // FAN TEST
  //
  // Toggles ON/OFF every second
  // ==================================================

  if (
    now - previousFanTime >=
    FAN_INTERVAL
  )
  {
    previousFanTime =
      now;


    fanState =
      !fanState;


    digitalWrite(
      FAN_PIN,
      fanState
        ? HIGH
        : LOW
    );


    Serial.print(
      "Fan: "
    );

    Serial.println(
      fanState
        ? "ON"
        : "OFF"
    );
  }


  // ==================================================
  // 3W LED PWM FADE
  //
  // 0 -> 255 -> 0 continuously
  // ==================================================

  if (
    now - previousPWMTime >=
    PWM_INTERVAL
  )
  {
    previousPWMTime =
      now;


    pwmValue +=
      pwmDirection;


    if (pwmValue >= 255) {

      pwmValue = 255;
      pwmDirection = -1;
    }


    if (pwmValue <= 0) {

      pwmValue = 0;
      pwmDirection = 1;
    }


    ledcWrite(
      LED_1_PIN,
      pwmValue
    );


    ledcWrite(
      LED_2_PIN,
      pwmValue
    );
  }


  // ==================================================
  // SERIAL MONITOR
  // ==================================================

  if (
    now - previousSerialTime >=
    SERIAL_INTERVAL
  )
  {
    previousSerialTime =
      now;


    Serial.print(
      "OUT GPIO "
    );

    Serial.print(
      outputPins[currentOutput]
    );


    Serial.print(
      " | IN16="
    );

    Serial.print(
      input1State
    );


    Serial.print(
      " | IN15="
    );

    Serial.print(
      input2State
    );


    Serial.print(
      " | FAN="
    );

    Serial.print(
      fanState
        ? "ON"
        : "OFF"
    );


    Serial.print(
      " | PWM="
    );

    Serial.print(
      pwmValue
    );


    Serial.print(
      " | BUZZER="
    );

    Serial.println(
      buzzerState
        ? "ON"
        : "OFF"
    );
  }


  // ==================================================
  // LCD
  // ==================================================

  if (
    now - previousLCDTime >=
    LCD_INTERVAL
  )
  {
    previousLCDTime =
      now;


    // -------------------------
    // ROW 1
    // -------------------------

    lcd.setCursor(
      0,
      0
    );


    lcd.print(
      "OUT:"
    );

    lcd.print(
      outputPins[currentOutput]
    );


    lcd.print(
      " FAN:"
    );

    lcd.print(
      fanState
        ? "1"
        : "0"
    );


    // Clear remaining characters
    lcd.print(
      "     "
    );


    // -------------------------
    // ROW 2
    // -------------------------

    lcd.setCursor(
      0,
      1
    );


    lcd.print(
      "16:"
    );

    lcd.print(
      input1State
    );


    lcd.print(
      " 15:"
    );

    lcd.print(
      input2State
    );


    lcd.print(
      " B:"
    );

    lcd.print(
      buzzerState
        ? "1"
        : "0"
    );


    // Clear remaining characters
    lcd.print(
      "   "
    );
  }
}
