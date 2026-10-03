#define BLYNK_TEMPLATE_ID " "
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN " "

#define BLYNK_PRINT Serial

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <time.h>
#include <math.h>

// ====================================================
// WIFI
// ====================================================

char ssid[] = " "; //WIFI name
char pass[] = " "; //WIFI password

bool wifiWasConnected = false;

unsigned long lastWiFiAttempt = 0;
unsigned long lastBlynkAttempt = 0;

const unsigned long WIFI_RETRY_INTERVAL = 10000;
const unsigned long BLYNK_RETRY_INTERVAL = 10000;


// ====================================================
// LCD
// ====================================================

#define SDA_PIN 8
#define SCL_PIN 9

LiquidCrystal_I2C lcd(0x27, 16, 2);

const unsigned long LCD_UPDATE_INTERVAL = 250;
const unsigned long LCD_FORCE_UPDATE_INTERVAL = 5000;
const unsigned long LCD_RECOVERY_INTERVAL = 60000;

unsigned long previousLCDUpdate = 0;
unsigned long previousLCDForceUpdate = 0;
unsigned long previousLCDRecovery = 0;

char lastLCDLine0[17] = "";
char lastLCDLine1[17] = "";


// ====================================================
// BUTTONS
// ====================================================

#define BUTTON_1_PIN 16
#define BUTTON_2_PIN 15

#define BUTTON_PRESSED_STATE HIGH

const unsigned long DEBOUNCE_TIME = 30;
const unsigned long RESET_HOLD_TIME = 2000;

struct Button {
  int pin;
  int stableState;
  int lastRawState;
  unsigned long lastRawChange;
  bool pressEvent;
};

Button button1;
Button button2;

bool bothButtonsHolding = false;
unsigned long bothButtonsStart = 0;


// ====================================================
// 3W LIGHTS
// ====================================================

#define LIGHT_1_PIN 4
#define LIGHT_2_PIN 5

const int LIGHT_PWM_FREQUENCY = 1000;
const int LIGHT_PWM_RESOLUTION = 8;


// ====================================================
// FAN
// ====================================================

#define FAN_PIN 7

const int FAN_PWM_FREQUENCY = 20000;
const int FAN_PWM_RESOLUTION = 8;


// ====================================================
// BUZZER
// ====================================================

#define BUZZER_PIN 21

#define PASSIVE_BUZZER 1

const int BUZZER_FREQUENCY = 2200;
const int BUZZER_PWM_RESOLUTION = 8;


// ====================================================
// STATUS LEDS
// ====================================================

/*
  GPIO10 RED
    ON = alarm disabled

  GPIO11 GREEN
    ON = alarm enabled

  GPIO12
    heartbeat pulse = firmware alive

  GPIO13
    solid = WiFi connected
    blink = WiFi disconnected

  GPIO17 BLUE
    solid = Blynk connected
    blink = WiFi OK, Blynk disconnected
    off = no WiFi

  GPIO18 YELLOW
    solid = all Blynk data received
    fast blink = retrieving data
    double blink = sync timed out
    off = Blynk disconnected
*/

#define RED_LED_PIN       10
#define GREEN_LED_PIN     11
#define HEARTBEAT_LED_PIN 12
#define WIFI_LED_PIN      13
#define BLYNK_LED_PIN     17
#define DATA_LED_PIN      18

const int statusPatternLEDs[] = {
  HEARTBEAT_LED_PIN,
  WIFI_LED_PIN,
  BLYNK_LED_PIN,
  DATA_LED_PIN
};

const int statusPatternLEDCount =
  sizeof(statusPatternLEDs) /
  sizeof(statusPatternLEDs[0]);


// ====================================================
// BLYNK VALUES
// ====================================================

// V0
int alarmActive = 0;

// V1
int light1Active = 0;

// V2
int light2Active = 0;

// V3
// Additional repeats.
// 0 = only first alarm
// 2 = first alarm + 2 repeats
int repeat = 0;

// V4
// Duration of EACH alarm burst in seconds
int duration = 30;

// V5
// Currently unused
double intensityVib = 0.0;

// V6
// Maximum alarm light intensity
double intensityLgt = 1.0;

// V7
double intensityLgt1 = 0.0;

// V8
double intensityLgt2 = 0.0;

// V9
// Seconds from midnight as String
String alarmTime = "";


// ====================================================
// BLYNK SYNC
// ====================================================

bool receivedPins[10] = {false};
bool allDataReceived = false;
bool syncTimedOut = false;

unsigned long blynkSyncStart = 0;
unsigned long previousBlynkResync = 0;

const unsigned long BLYNK_SYNC_TIMEOUT = 8000;
const unsigned long BLYNK_RESYNC_INTERVAL = 10000;


// ====================================================
// BLYNK DATA LED ANIMATION
// ====================================================

bool dataPatternActive = false;
unsigned long dataPatternStart = 0;

const unsigned long DATA_PATTERN_STEP = 90;
const int DATA_PATTERN_STEPS = 8;


// ====================================================
// CLOCK
// ====================================================

const long GMT_OFFSET = 3 * 3600;

bool ntpConfigured = false;

const int ALARM_TRIGGER_WINDOW = 60;


// ====================================================
// LCD PAGE
// ====================================================

bool showClockPage = true;
int currentPage = -1;


// ====================================================
// ALARM STATE
// ====================================================

bool alarmSequenceActive = false;
bool alarmBurstOn = false;

int currentAlarmBurst = 0;

unsigned long alarmPhaseStart = 0;

const unsigned long ALARM_REPEAT_GAP = 10000;

int lastAlarmDateKey = -1;


// ====================================================
// SMOOTH LIGHT BREATHING STATE
// ====================================================

double lightBreathPhase = 0.0;
unsigned long previousLightBreathUpdate = 0;


// ====================================================
// GENERAL HELPERS
// ====================================================

double clamp01(double value)
{
  if (value < 0.0) return 0.0;
  if (value > 1.0) return 1.0;

  return value;
}


uint32_t intensityToPWM(double intensity)
{
  intensity = clamp01(intensity);

  return (uint32_t)(255.0 * intensity + 0.5);
}


// ====================================================
// LCD HELPERS
// ====================================================

void invalidateLCDCache()
{
  lastLCDLine0[0] = '\0';
  lastLCDLine1[0] = '\0';
}


void writeLCDLine(
  int row,
  const char *text,
  bool force = false
)
{
  char formatted[17];

  snprintf(
    formatted,
    sizeof(formatted),
    "%-16.16s",
    text
  );

  char *cache =
    row == 0
    ? lastLCDLine0
    : lastLCDLine1;

  if (
    force ||
    strcmp(formatted, cache) != 0
  )
  {
    lcd.setCursor(0, row);
    lcd.print(formatted);

    strcpy(cache, formatted);
  }
}


// ====================================================
// BLYNK STATE
// ====================================================

bool allBlynkDataReceived()
{
  for (int i = 0; i < 10; i++) {

    if (!receivedPins[i])
      return false;
  }

  return true;
}


bool alarmDataReady()
{
  return
    receivedPins[0] &&
    receivedPins[3] &&
    receivedPins[4] &&
    receivedPins[6] &&
    receivedPins[9];
}


void startDataPattern()
{
  dataPatternActive = true;
  dataPatternStart = millis();
}


// ====================================================
// BLYNK CALLBACK
// ====================================================

BLYNK_WRITE_DEFAULT()
{
  int pin = request.pin;

  if (pin < 0 || pin > 9)
    return;

  receivedPins[pin] = true;

  switch (pin) {

    case 0:
      alarmActive = param.asInt();

      Serial.printf(
        "V0 Alarm Active = %d\n",
        alarmActive
      );
      break;


    case 1:
      light1Active = param.asInt();

      Serial.printf(
        "V1 Light 1 Active = %d\n",
        light1Active
      );
      break;


    case 2:
      light2Active = param.asInt();

      Serial.printf(
        "V2 Light 2 Active = %d\n",
        light2Active
      );
      break;


    case 3:
      repeat = param.asInt();

      Serial.printf(
        "V3 Repeat = %d\n",
        repeat
      );
      break;


    case 4:
      duration = param.asInt();

      Serial.printf(
        "V4 Duration = %d sec\n",
        duration
      );
      break;


    case 5:
      intensityVib = param.asDouble();

      Serial.printf(
        "V5 Vib = %.2f\n",
        intensityVib
      );
      break;


    case 6:
      intensityLgt = param.asDouble();

      Serial.printf(
        "V6 Alarm Light = %.2f\n",
        intensityLgt
      );
      break;


    case 7:
      intensityLgt1 = param.asDouble();

      Serial.printf(
        "V7 Light1 Int = %.2f\n",
        intensityLgt1
      );
      break;


    case 8:
      intensityLgt2 = param.asDouble();

      Serial.printf(
        "V8 Light2 Int = %.2f\n",
        intensityLgt2
      );
      break;


    case 9:
      alarmTime = param.asStr();

      Serial.printf(
        "V9 Alarm Time = %s\n",
        alarmTime.c_str()
      );
      break;
  }


  bool previousAllData =
    allDataReceived;


  allDataReceived =
    allBlynkDataReceived();


  // Only do the chase after a complete valid sync.
  if (allDataReceived) {

    if (
      !previousAllData ||
      Blynk.connected()
    )
    {
      startDataPattern();
    }
  }
}


// ====================================================
// BLYNK CONNECTED
// ====================================================

BLYNK_CONNECTED()
{
  Serial.println();
  Serial.println("BLYNK CONNECTED");
  Serial.println("Retrieving V0-V9");


  for (int i = 0; i < 10; i++) {
    receivedPins[i] = false;
  }


  allDataReceived = false;
  syncTimedOut = false;

  dataPatternActive = false;

  blynkSyncStart = millis();
  previousBlynkResync = millis();


  Blynk.syncVirtual(
    V0,
    V1,
    V2,
    V3,
    V4,
    V5,
    V6,
    V7,
    V8,
    V9
  );
}


// ====================================================
// NTP
// ====================================================

void configureNTP()
{
  if (ntpConfigured)
    return;


  configTime(
    GMT_OFFSET,
    0,
    "pool.ntp.org",
    "time.google.com"
  );


  ntpConfigured = true;


  Serial.println(
    "NTP configured"
  );


  struct tm timeInfo;


  if (
    getLocalTime(
      &timeInfo,
      2000
    )
  )
  {
    Serial.printf(
      "Clock: %02d:%02d:%02d\n",
      timeInfo.tm_hour,
      timeInfo.tm_min,
      timeInfo.tm_sec
    );
  }

  else {

    Serial.println(
      "NTP not synchronized yet"
    );
  }
}


// ====================================================
// NETWORK MANAGEMENT
// ====================================================

void attemptBlynkConnection()
{
  if (
    WiFi.status() != WL_CONNECTED ||
    Blynk.connected()
  )
  {
    return;
  }


  Serial.println(
    "Trying Blynk..."
  );


  if (
    Blynk.connect(1000)
  )
  {
    Serial.println(
      "Blynk connected"
    );
  }

  else {

    Serial.println(
      "Blynk connection failed"
    );
  }
}


void maintainConnections(
  unsigned long now
)
{
  bool wifiConnected =
    WiFi.status() ==
    WL_CONNECTED;


  if (
    wifiConnected &&
    !wifiWasConnected
  )
  {
    Serial.println();
    Serial.println(
      "WiFi connected"
    );


    Serial.print(
      "IP: "
    );

    Serial.println(
      WiFi.localIP()
    );


    configureNTP();


    lastBlynkAttempt = 0;
  }


  if (
    !wifiConnected &&
    wifiWasConnected
  )
  {
    Serial.println(
      "WiFi disconnected"
    );
  }


  wifiWasConnected =
    wifiConnected;


  if (!wifiConnected) {

    if (
      now - lastWiFiAttempt >=
      WIFI_RETRY_INTERVAL
    )
    {
      lastWiFiAttempt = now;

      Serial.println(
        "Retrying WiFi..."
      );


      WiFi.disconnect();


      WiFi.begin(
        ssid,
        pass
      );
    }

    return;
  }


  if (Blynk.connected()) {

    Blynk.run();
  }

  else {

    if (
      !alarmSequenceActive &&
      now - lastBlynkAttempt >=
      BLYNK_RETRY_INTERVAL
    )
    {
      lastBlynkAttempt = now;

      attemptBlynkConnection();
    }
  }
}


// ====================================================
// BLYNK RESYNC
// ====================================================

void maintainBlynkSync(
  unsigned long now
)
{
  if (!Blynk.connected())
    return;


  if (allDataReceived)
    return;


  if (
    !syncTimedOut &&
    now - blynkSyncStart >=
    BLYNK_SYNC_TIMEOUT
  )
  {
    syncTimedOut = true;

    Serial.println(
      "Blynk data sync timed out"
    );
  }


  if (
    now - previousBlynkResync >=
    BLYNK_RESYNC_INTERVAL
  )
  {
    previousBlynkResync = now;


    Serial.println(
      "Retrying Blynk data sync"
    );


    Blynk.syncVirtual(
      V0,
      V1,
      V2,
      V3,
      V4,
      V5,
      V6,
      V7,
      V8,
      V9
    );
  }
}


// ====================================================
// ALARM TIME
// ====================================================

bool getAlarmSeconds(
  int &secondsOut
)
{
  if (alarmTime.length() == 0)
    return false;


  for (
    unsigned int i = 0;
    i < alarmTime.length();
    i++
  )
  {
    if (
      alarmTime[i] < '0' ||
      alarmTime[i] > '9'
    )
    {
      return false;
    }
  }


  long value =
    alarmTime.toInt();


  if (
    value < 0 ||
    value >= 86400
  )
  {
    return false;
  }


  secondsOut =
    (int)value;


  return true;
}


void secondsToClock(
  int seconds,
  char *buffer,
  size_t bufferSize
)
{
  if (
    seconds < 0 ||
    seconds >= 86400
  )
  {
    snprintf(
      buffer,
      bufferSize,
      "--:--"
    );

    return;
  }


  int hour =
    seconds / 3600;


  int minute =
    (seconds % 3600)
    / 60;


  snprintf(
    buffer,
    bufferSize,
    "%02d:%02d",
    hour,
    minute
  );
}


// ====================================================
// BUTTONS
// ====================================================

void initializeButton(
  Button &button,
  int pin
)
{
  pinMode(
    pin,
    INPUT
  );


  int initial =
    digitalRead(pin);


  button.pin =
    pin;

  button.stableState =
    initial;

  button.lastRawState =
    initial;

  button.lastRawChange =
    millis();

  button.pressEvent =
    false;
}


void updateButton(
  Button &button,
  unsigned long now
)
{
  button.pressEvent = false;


  int raw =
    digitalRead(
      button.pin
    );


  if (
    raw !=
    button.lastRawState
  )
  {
    button.lastRawState =
      raw;

    button.lastRawChange =
      now;
  }


  if (
    now - button.lastRawChange >=
    DEBOUNCE_TIME
  )
  {
    if (
      raw !=
      button.stableState
    )
    {
      button.stableState =
        raw;


      if (
        button.stableState ==
        BUTTON_PRESSED_STATE
      )
      {
        button.pressEvent =
          true;
      }
    }
  }
}


bool buttonIsPressed(
  const Button &button
)
{
  return
    button.stableState ==
    BUTTON_PRESSED_STATE;
}


// ====================================================
// BUZZER
// ====================================================

void setBuzzerVolume(
  double volume
)
{
  volume =
    clamp01(volume);


#if PASSIVE_BUZZER

  uint32_t pwm =
    (uint32_t)(
      128.0 * volume
    );


  ledcWrite(
    BUZZER_PIN,
    pwm
  );

#else

  digitalWrite(
    BUZZER_PIN,
    volume > 0.05
      ? HIGH
      : LOW
  );

#endif
}


void silenceBuzzer()
{
  setBuzzerVolume(
    0.0
  );
}


// ====================================================
// ALARM PARAMETERS
// ====================================================

int getTotalAlarmBursts()
{
  return max(
    1,
    repeat + 1
  );
}


unsigned long getAlarmBurstDuration()
{
  return
    (unsigned long)
    max(1, duration)
    * 1000UL;
}


// ====================================================
// START / STOP ALARM
// ====================================================

void startAlarmSequence(
  unsigned long now,
  int dateKey
)
{
  alarmSequenceActive = true;
  alarmBurstOn = true;

  currentAlarmBurst = 0;

  alarmPhaseStart = now;

  lastAlarmDateKey =
    dateKey;


  // Reset smooth light waveform.
  lightBreathPhase = 0.0;
  previousLightBreathUpdate = now;


  Serial.println();
  Serial.println(
    "===================="
  );

  Serial.println(
    "ALARM STARTED"
  );

  Serial.printf(
    "Duration = %d sec\n",
    duration
  );

  Serial.printf(
    "Repeat = %d\n",
    repeat
  );

  Serial.printf(
    "Total bursts = %d\n",
    getTotalAlarmBursts()
  );

  Serial.println(
    "===================="
  );
}


void stopAlarmSequence(
  const char *reason
)
{
  if (!alarmSequenceActive)
    return;


  alarmSequenceActive = false;
  alarmBurstOn = false;


  ledcWrite(
    LIGHT_1_PIN,
    0
  );

  ledcWrite(
    LIGHT_2_PIN,
    0
  );

  ledcWrite(
    FAN_PIN,
    0
  );


  silenceBuzzer();


  lightBreathPhase = 0.0;


  Serial.print(
    "Alarm stopped: "
  );

  Serial.println(
    reason
  );
}


// ====================================================
// ALARM TRIGGER
// ====================================================

void checkAlarmTrigger(
  unsigned long now
)
{
  static unsigned long previousCheck = 0;


  if (
    now - previousCheck <
    250
  )
  {
    return;
  }


  previousCheck = now;


  if (!alarmActive)
    return;


  if (!alarmDataReady())
    return;


  if (alarmSequenceActive)
    return;


  int targetSeconds;


  if (
    !getAlarmSeconds(
      targetSeconds
    )
  )
  {
    return;
  }


  struct tm timeInfo;


  if (
    !getLocalTime(
      &timeInfo,
      10
    )
  )
  {
    return;
  }


  int secondsFromMidnight =
    timeInfo.tm_hour * 3600 +
    timeInfo.tm_min * 60 +
    timeInfo.tm_sec;


  int dateKey =
    (timeInfo.tm_year + 1900)
    * 1000 +
    timeInfo.tm_yday;


  if (
    dateKey ==
    lastAlarmDateKey
  )
  {
    return;
  }


  if (
    secondsFromMidnight >=
      targetSeconds &&

    secondsFromMidnight <
      targetSeconds +
      ALARM_TRIGGER_WINDOW
  )
  {
    startAlarmSequence(
      now,
      dateKey
    );
  }
}


// ====================================================
// ALARM SEQUENCE
// ====================================================

void updateAlarmSequence(
  unsigned long now
)
{
  if (!alarmSequenceActive)
    return;


  if (!alarmActive) {

    stopAlarmSequence(
      "disabled from Blynk"
    );

    return;
  }


  unsigned long burstDuration =
    getAlarmBurstDuration();


  int totalBursts =
    getTotalAlarmBursts();


  // ==================================================
  // ACTIVE BURST
  // ==================================================

  if (alarmBurstOn) {

    if (
      now - alarmPhaseStart >=
      burstDuration
    )
    {
      if (
        currentAlarmBurst + 1 >=
        totalBursts
      )
      {
        stopAlarmSequence(
          "completed"
        );
      }

      else {

        alarmBurstOn =
          false;

        alarmPhaseStart =
          now;


        ledcWrite(
          LIGHT_1_PIN,
          0
        );

        ledcWrite(
          LIGHT_2_PIN,
          0
        );

        ledcWrite(
          FAN_PIN,
          0
        );


        silenceBuzzer();
      }
    }
  }


  // ==================================================
  // GAP BETWEEN REPEATS
  // ==================================================

  else {

    if (
      now - alarmPhaseStart >=
      ALARM_REPEAT_GAP
    )
    {
      currentAlarmBurst++;

      alarmBurstOn =
        true;

      alarmPhaseStart =
        now;


      // Restart breathing smoothly.
      lightBreathPhase =
        0.0;

      previousLightBreathUpdate =
        now;
    }
  }
}


// ====================================================
// ALARM SEVERITY
// ====================================================

double getAlarmSeverity(
  unsigned long now
)
{
  if (
    !alarmSequenceActive ||
    !alarmBurstOn
  )
  {
    return 0.0;
  }


  unsigned long durationMs =
    getAlarmBurstDuration();


  double progress =
    (double)(
      now - alarmPhaseStart
    )
    /
    (double)durationMs;


  progress =
    clamp01(progress);


  double repeatBoost =
    currentAlarmBurst *
    0.15;


  return clamp01(
    progress * 0.85 +
    repeatBoost
  );
}


// ====================================================
// LIGHT BREATH PERIOD
//
// 0%   -> 4.0 sec
// 40%  -> 1.0 sec
// 100% -> 0.7 sec
// ====================================================

double getLightBreathPeriod(
  double progress
)
{
  progress =
    clamp01(progress);


  if (progress <= 0.40) {

    double section =
      progress /
      0.40;


    return
      4.0 +
      (
        1.0 -
        4.0
      )
      *
      section;
  }


  double section =
    (
      progress -
      0.40
    )
    /
    0.60;


  return
    1.0 +
    (
      0.7 -
      1.0
    )
    *
    section;
}


// ====================================================
// SMOOTH 3W LED BREATHING
// ====================================================

void updateAlarmLights(
  unsigned long now,
  double severity
)
{
  if (
    !alarmSequenceActive ||
    !alarmBurstOn
  )
  {
    ledcWrite(
      LIGHT_1_PIN,
      0
    );

    ledcWrite(
      LIGHT_2_PIN,
      0
    );

    return;
  }


  unsigned long durationMs =
    getAlarmBurstDuration();


  unsigned long elapsed =
    now -
    alarmPhaseStart;


  double progress =
    (double)elapsed /
    (double)durationMs;


  progress =
    clamp01(progress);


  // ==================================================
  // BREATHING SPEED
  // ==================================================

  double periodSeconds =
    getLightBreathPeriod(
      progress
    );


  double periodMs =
    periodSeconds *
    1000.0;


  // ==================================================
  // CONTINUOUS PHASE ACCUMULATION
  //
  // This prevents jumps as the period changes.
  // ==================================================

  if (
    previousLightBreathUpdate == 0
  )
  {
    previousLightBreathUpdate =
      now;
  }


  unsigned long deltaTime =
    now -
    previousLightBreathUpdate;


  previousLightBreathUpdate =
    now;


  lightBreathPhase +=
    (double)deltaTime /
    periodMs;


  while (
    lightBreathPhase >= 1.0
  )
  {
    lightBreathPhase -= 1.0;
  }


  // ==================================================
  // LOW -> HIGH -> LOW
  //
  // Smooth cosine wave:
  //
  // phase 0.0 = LOW
  // phase 0.5 = HIGH
  // phase 1.0 = LOW
  // ==================================================

  double breathing =
    0.5 -
    0.5 *
    cos(
      2.0 *
      PI *
      lightBreathPhase
    );


  // ==================================================
  // PEAK INTENSITY ALSO INCREASES WITH TIME
  //
  // Start:
  //   30% of V6 maximum
  //
  // End:
  //   100% of V6 maximum
  // ==================================================

  double configuredMax =
    clamp01(
      intensityLgt
    );


  double peakFraction =
    0.30 +
    0.70 *
    progress;


  double peakIntensity =
    configuredMax *
    peakFraction;


  // Bottom of each breath:
  // 3% of configured maximum.
  double lowIntensity =
    configuredMax *
    0.03;


  if (
    lowIntensity >
    peakIntensity
  )
  {
    lowIntensity =
      peakIntensity;
  }


  double currentIntensity =
    lowIntensity +
    (
      peakIntensity -
      lowIntensity
    )
    *
    breathing;


  uint32_t pwm =
    intensityToPWM(
      currentIntensity
    );


  // Both 3W lights breathe together.
  ledcWrite(
    LIGHT_1_PIN,
    pwm
  );


  ledcWrite(
    LIGHT_2_PIN,
    pwm
  );
}


// ====================================================
// BUZZER
// ====================================================

void updateAlarmBuzzer(
  unsigned long now,
  double severity
)
{
  // First 10% is light only.
  if (severity < 0.10) {

    silenceBuzzer();

    return;
  }


  double soundSeverity =
    (
      severity -
      0.10
    )
    /
    0.90;


  soundSeverity =
    clamp01(
      soundSeverity
    );


  double volume =
    0.20 +
    0.80 *
    soundSeverity;


  const unsigned long BEEP_TIME =
    160;

  const unsigned long SMALL_GAP =
    120;


  unsigned long longGap =
    1400 -
    (unsigned long)(
      1000 *
      soundSeverity
    );


  unsigned long patternLength =
    3 * BEEP_TIME +
    2 * SMALL_GAP +
    longGap;


  unsigned long t =
    (
      now -
      alarmPhaseStart
    )
    %
    patternLength;


  bool beepOn =

    (
      t <
      BEEP_TIME
    )

    ||

    (
      t >=
        BEEP_TIME +
        SMALL_GAP

      &&

      t <
        2 * BEEP_TIME +
        SMALL_GAP
    )

    ||

    (
      t >=
        2 * BEEP_TIME +
        2 * SMALL_GAP

      &&

      t <
        3 * BEEP_TIME +
        2 * SMALL_GAP
    );


  if (beepOn) {

    setBuzzerVolume(
      volume
    );
  }

  else {

    silenceBuzzer();
  }
}


// ====================================================
// FAN
//
// Reaches FULL SPEED after 10% of alarm duration.
//
// Example:
//
// duration 60 sec
// fan reaches full at 6 sec
//
// duration 30 sec
// fan reaches full at 3 sec
// ====================================================

void updateAlarmFan(
  unsigned long now,
  double severity
)
{
  if (
    !alarmSequenceActive ||
    !alarmBurstOn
  )
  {
    ledcWrite(
      FAN_PIN,
      0
    );

    return;
  }


  unsigned long durationMs =
    getAlarmBurstDuration();


  unsigned long elapsed =
    now -
    alarmPhaseStart;


  double progress =
    (double)elapsed /
    (double)durationMs;


  progress =
    clamp01(progress);


  // 0 -> 1 over first 10% of duration.
  double fanProgress =
    progress /
    0.10;


  fanProgress =
    clamp01(
      fanProgress
    );


  // Smoothstep curve.
  double smoothFan =
    fanProgress *
    fanProgress *
    (
      3.0 -
      2.0 *
      fanProgress
    );


  uint32_t fanPWM =
    (uint32_t)(
      smoothFan *
      255.0
    );


  ledcWrite(
    FAN_PIN,
    fanPWM
  );
}


// ====================================================
// OUTPUT CONTROL
// ====================================================

void updateOutputs(
  unsigned long now
)
{
  // ==================================================
  // ACTIVE ALARM
  // ==================================================

  if (
    alarmSequenceActive &&
    alarmBurstOn
  )
  {
    double severity =
      getAlarmSeverity(
        now
      );


    updateAlarmLights(
      now,
      severity
    );


    updateAlarmBuzzer(
      now,
      severity
    );


    updateAlarmFan(
      now,
      severity
    );


    return;
  }


  // ==================================================
  // REPEAT GAP
  // ==================================================

  if (alarmSequenceActive) {

    ledcWrite(
      LIGHT_1_PIN,
      0
    );


    ledcWrite(
      LIGHT_2_PIN,
      0
    );


    ledcWrite(
      FAN_PIN,
      0
    );


    silenceBuzzer();


    return;
  }


  // ==================================================
  // NORMAL ROOM LIGHTING
  // ==================================================

  uint32_t light1PWM =
    0;


  uint32_t light2PWM =
    0;


  if (light1Active) {

    light1PWM =
      intensityToPWM(
        intensityLgt1
      );
  }


  if (light2Active) {

    light2PWM =
      intensityToPWM(
        intensityLgt2
      );
  }


  ledcWrite(
    LIGHT_1_PIN,
    light1PWM
  );


  ledcWrite(
    LIGHT_2_PIN,
    light2PWM
  );


  ledcWrite(
    FAN_PIN,
    0
  );


  silenceBuzzer();
}


// ====================================================
// STATUS LEDS
// ====================================================

void updateBoardLEDs(
  unsigned long now
)
{
  // ==================================================
  // RED / GREEN
  // ALARM ENABLED STATE
  // ==================================================

  digitalWrite(
    RED_LED_PIN,
    alarmActive
      ? LOW
      : HIGH
  );


  digitalWrite(
    GREEN_LED_PIN,
    alarmActive
      ? HIGH
      : LOW
  );


  // ==================================================
  // FRESH BLYNK DATA CHASE
  // ==================================================

  if (dataPatternActive) {

    unsigned long elapsed =
      now -
      dataPatternStart;


    int step =
      elapsed /
      DATA_PATTERN_STEP;


    if (
      step >=
      DATA_PATTERN_STEPS
    )
    {
      dataPatternActive =
        false;
    }

    else {

      int activeLED =
        step %
        statusPatternLEDCount;


      for (
        int i = 0;
        i <
        statusPatternLEDCount;
        i++
      )
      {
        digitalWrite(
          statusPatternLEDs[i],
          i == activeLED
            ? HIGH
            : LOW
        );
      }


      return;
    }
  }


  // ==================================================
  // GPIO12 HEARTBEAT
  // ==================================================

  bool heartbeat =
    (
      now %
      2000UL
    )
    <
    100UL;


  digitalWrite(
    HEARTBEAT_LED_PIN,
    heartbeat
      ? HIGH
      : LOW
  );


  // ==================================================
  // GPIO13 WIFI
  // ==================================================

  bool wifiConnected =
    WiFi.status() ==
    WL_CONNECTED;


  if (wifiConnected) {

    digitalWrite(
      WIFI_LED_PIN,
      HIGH
    );
  }

  else {

    bool blink =
      (
        now /
        300
      )
      %
      2;


    digitalWrite(
      WIFI_LED_PIN,
      blink
        ? HIGH
        : LOW
    );
  }


  // ==================================================
  // GPIO17 BLYNK
  // ==================================================

  if (!wifiConnected) {

    digitalWrite(
      BLYNK_LED_PIN,
      LOW
    );
  }

  else if (
    Blynk.connected()
  )
  {
    digitalWrite(
      BLYNK_LED_PIN,
      HIGH
    );
  }

  else {

    bool blink =
      (
        now /
        500
      )
      %
      2;


    digitalWrite(
      BLYNK_LED_PIN,
      blink
        ? HIGH
        : LOW
    );
  }


  // ==================================================
  // GPIO18 DATA STATE
  // ==================================================

  if (!Blynk.connected()) {

    digitalWrite(
      DATA_LED_PIN,
      LOW
    );

    return;
  }


  if (allDataReceived) {

    digitalWrite(
      DATA_LED_PIN,
      HIGH
    );

    return;
  }


  if (!syncTimedOut) {

    bool fastBlink =
      (
        now /
        150
      )
      %
      2;


    digitalWrite(
      DATA_LED_PIN,
      fastBlink
        ? HIGH
        : LOW
    );


    return;
  }


  // Sync timed out -> double blink
  unsigned long phase =
    now %
    1500UL;


  bool doubleBlink =
    (
      phase < 120
    )

    ||

    (
      phase >= 250 &&
      phase < 370
    );


  digitalWrite(
    DATA_LED_PIN,
    doubleBlink
      ? HIGH
      : LOW
  );
}


// ====================================================
// LCD
// ====================================================

void renderLCD(
  unsigned long now,
  bool force
)
{
  char line0[32];
  char line1[32];


  // ==================================================
  // ALARM
  // ==================================================

  if (alarmSequenceActive) {

    if (alarmBurstOn) {

      unsigned long total =
        getAlarmBurstDuration();


      unsigned long elapsed =
        now -
        alarmPhaseStart;


      unsigned long remaining =

        elapsed < total

        ? (
          total -
          elapsed +
          999
        )
        / 1000

        : 0;


      snprintf(
        line0,
        sizeof(line0),
        "ALARM %d/%d",
        currentAlarmBurst + 1,
        getTotalAlarmBursts()
      );


      snprintf(
        line1,
        sizeof(line1),
        "B2 STOP %lus",
        remaining
      );
    }

    else {

      unsigned long elapsed =
        now -
        alarmPhaseStart;


      unsigned long remaining =

        elapsed <
        ALARM_REPEAT_GAP

        ? (
          ALARM_REPEAT_GAP -
          elapsed +
          999
        )
        / 1000

        : 0;


      snprintf(
        line0,
        sizeof(line0),
        "Repeat %d/%d",
        currentAlarmBurst + 2,
        getTotalAlarmBursts()
      );


      snprintf(
        line1,
        sizeof(line1),
        "Next in %lus",
        remaining
      );
    }


    writeLCDLine(
      0,
      line0,
      force
    );


    writeLCDLine(
      1,
      line1,
      force
    );


    return;
  }


  // ==================================================
  // BLYNK SYNC
  // ==================================================

  if (
    Blynk.connected() &&
    !allDataReceived &&
    !syncTimedOut
  )
  {
    int count = 0;


    for (int i = 0; i < 10; i++) {

      if (receivedPins[i])
        count++;
    }


    snprintf(
      line0,
      sizeof(line0),
      "Blynk syncing..."
    );


    snprintf(
      line1,
      sizeof(line1),
      "%d/10 received",
      count
    );


    writeLCDLine(
      0,
      line0,
      force
    );


    writeLCDLine(
      1,
      line1,
      force
    );


    return;
  }


  // ==================================================
  // HOME
  // ==================================================

  if (showClockPage) {

    struct tm timeInfo;


    char currentTime[10] =
      "--:--:--";


    if (
      getLocalTime(
        &timeInfo,
        10
      )
    )
    {
      snprintf(
        currentTime,
        sizeof(currentTime),
        "%02d:%02d:%02d",
        timeInfo.tm_hour,
        timeInfo.tm_min,
        timeInfo.tm_sec
      );
    }


    bool wifi =
      WiFi.status() ==
      WL_CONNECTED;


    bool blynk =
      Blynk.connected();


    snprintf(
      line0,
      sizeof(line0),
      "%s W%dB%dD%d",
      currentTime,
      wifi ? 1 : 0,
      blynk ? 1 : 0,
      allDataReceived ? 1 : 0
    );


    int alarmSeconds;


    char alarmText[8] =
      "--:--";


    if (
      getAlarmSeconds(
        alarmSeconds
      )
    )
    {
      secondsToClock(
        alarmSeconds,
        alarmText,
        sizeof(alarmText)
      );
    }


    snprintf(
      line1,
      sizeof(line1),
      "A %s %s",
      alarmText,
      alarmActive
        ? "ON"
        : "OFF"
    );


    writeLCDLine(
      0,
      line0,
      force
    );


    writeLCDLine(
      1,
      line1,
      force
    );


    return;
  }


  // ==================================================
  // BLYNK DATA PAGES
  // ==================================================

  switch (currentPage) {

    case 0:

      snprintf(
        line0,
        sizeof(line0),
        "V0 Alarm Active"
      );

      snprintf(
        line1,
        sizeof(line1),
        "%s",
        alarmActive
          ? "ON"
          : "OFF"
      );

      break;


    case 1:

      snprintf(
        line0,
        sizeof(line0),
        "V1 Light 1"
      );

      snprintf(
        line1,
        sizeof(line1),
        "%s",
        light1Active
          ? "ON"
          : "OFF"
      );

      break;


    case 2:

      snprintf(
        line0,
        sizeof(line0),
        "V2 Light 2"
      );

      snprintf(
        line1,
        sizeof(line1),
        "%s",
        light2Active
          ? "ON"
          : "OFF"
      );

      break;


    case 3:

      snprintf(
        line0,
        sizeof(line0),
        "V3 Repeat"
      );

      snprintf(
        line1,
        sizeof(line1),
        "%d (+first)",
        repeat
      );

      break;


    case 4:

      snprintf(
        line0,
        sizeof(line0),
        "V4 Duration"
      );

      snprintf(
        line1,
        sizeof(line1),
        "%d seconds",
        duration
      );

      break;


    case 5:

      snprintf(
        line0,
        sizeof(line0),
        "V5 Vib Intens"
      );

      snprintf(
        line1,
        sizeof(line1),
        "%.2f",
        intensityVib
      );

      break;


    case 6:

      snprintf(
        line0,
        sizeof(line0),
        "V6 Alarm Light"
      );

      snprintf(
        line1,
        sizeof(line1),
        "%.2f",
        intensityLgt
      );

      break;


    case 7:

      snprintf(
        line0,
        sizeof(line0),
        "V7 Light1 Int"
      );

      snprintf(
        line1,
        sizeof(line1),
        "%.2f",
        intensityLgt1
      );

      break;


    case 8:

      snprintf(
        line0,
        sizeof(line0),
        "V8 Light2 Int"
      );

      snprintf(
        line1,
        sizeof(line1),
        "%.2f",
        intensityLgt2
      );

      break;


    case 9:
    {
      snprintf(
        line0,
        sizeof(line0),
        "V9 Alarm Time"
      );


      int seconds;


      if (
        getAlarmSeconds(
          seconds
        )
      )
      {
        char timeText[8];


        secondsToClock(
          seconds,
          timeText,
          sizeof(timeText)
        );


        snprintf(
          line1,
          sizeof(line1),
          "%s",
          timeText
        );
      }

      else {

        snprintf(
          line1,
          sizeof(line1),
          "INVALID"
        );
      }

      break;
    }


    default:

      snprintf(
        line0,
        sizeof(line0),
        "Data"
      );


      snprintf(
        line1,
        sizeof(line1),
        "Press Button 1"
      );

      break;
  }


  writeLCDLine(
    0,
    line0,
    force
  );


  writeLCDLine(
    1,
    line1,
    force
  );
}


// ====================================================
// LCD UPDATE
// ====================================================

void updateLCD(
  unsigned long now
)
{
  if (
    now - previousLCDUpdate <
    LCD_UPDATE_INTERVAL
  )
  {
    return;
  }


  previousLCDUpdate =
    now;


  bool force =
    false;


  if (
    now - previousLCDForceUpdate >=
    LCD_FORCE_UPDATE_INTERVAL
  )
  {
    previousLCDForceUpdate =
      now;

    force =
      true;
  }


  renderLCD(
    now,
    force
  );
}


// ====================================================
// LCD RECOVERY
// ====================================================

void maintainLCD(
  unsigned long now
)
{
  if (
    now - previousLCDRecovery <
    LCD_RECOVERY_INTERVAL
  )
  {
    return;
  }


  previousLCDRecovery =
    now;


  lcd.init();

  Wire.setClock(
    100000
  );

  lcd.backlight();

  invalidateLCDCache();

  previousLCDForceUpdate =
    0;
}


// ====================================================
// BUTTON ACTIONS
// ====================================================

void processButtons(
  unsigned long now
)
{
  updateButton(
    button1,
    now
  );


  updateButton(
    button2,
    now
  );


  bool bothPressed =
    buttonIsPressed(button1)
    &&
    buttonIsPressed(button2);


  // ==================================================
  // BOTH HELD 2 SEC -> RESET
  // ==================================================

  if (bothPressed) {

    if (!bothButtonsHolding) {

      bothButtonsHolding =
        true;

      bothButtonsStart =
        now;
    }


    if (
      now - bothButtonsStart >=
      RESET_HOLD_TIME
    )
    {
      writeLCDLine(
        0,
        "Restarting...",
        true
      );


      writeLCDLine(
        1,
        "Please wait",
        true
      );


      ledcWrite(
        LIGHT_1_PIN,
        0
      );


      ledcWrite(
        LIGHT_2_PIN,
        0
      );


      ledcWrite(
        FAN_PIN,
        0
      );


      silenceBuzzer();


      delay(200);


      ESP.restart();
    }


    return;
  }


  bothButtonsHolding =
    false;


  // ==================================================
  // BUTTON 1
  // CYCLE V0-V9
  // ==================================================

  if (button1.pressEvent) {

    showClockPage =
      false;


    currentPage++;


    if (currentPage > 9)
      currentPage = 0;


    invalidateLCDCache();
  }


  // ==================================================
  // BUTTON 2
  // DISMISS / HOME
  // ==================================================

  if (button2.pressEvent) {

    if (alarmSequenceActive) {

      stopAlarmSequence(
        "Button 2"
      );
    }

    else {

      showClockPage =
        !showClockPage;


      invalidateLCDCache();
    }
  }
}


// ====================================================
// INITIAL NETWORK
// ====================================================

void initializeNetwork()
{
  writeLCDLine(
    0,
    "Connecting WiFi",
    true
  );


  writeLCDLine(
    1,
    "Please wait...",
    true
  );


  WiFi.mode(
    WIFI_STA
  );


  WiFi.setAutoReconnect(
    true
  );


  Blynk.config(
    BLYNK_AUTH_TOKEN
  );


  WiFi.begin(
    ssid,
    pass
  );


  unsigned long start =
    millis();


  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 10000
  )
  {
    updateBoardLEDs(
      millis()
    );

    delay(20);
  }


  if (
    WiFi.status() ==
    WL_CONNECTED
  )
  {
    wifiWasConnected =
      true;


    Serial.println(
      "Initial WiFi connected"
    );


    writeLCDLine(
      0,
      "WiFi connected",
      true
    );


    writeLCDLine(
      1,
      "Connecting Blynk",
      true
    );


    configureNTP();


    if (
      Blynk.connect(3000)
    )
    {
      Serial.println(
        "Initial Blynk connected"
      );
    }

    else {

      Serial.println(
        "Initial Blynk failed"
      );
    }
  }

  else {

    Serial.println(
      "Initial WiFi failed"
    );


    Serial.println(
      "Continuing offline"
    );
  }
}


// ====================================================
// SETUP
// ====================================================

void setup()
{
  Serial.begin(
    115200
  );


  // ==================================================
  // STATUS LEDS FIRST
  // ==================================================

  pinMode(
    RED_LED_PIN,
    OUTPUT
  );

  pinMode(
    GREEN_LED_PIN,
    OUTPUT
  );

  pinMode(
    HEARTBEAT_LED_PIN,
    OUTPUT
  );

  pinMode(
    WIFI_LED_PIN,
    OUTPUT
  );

  pinMode(
    BLYNK_LED_PIN,
    OUTPUT
  );

  pinMode(
    DATA_LED_PIN,
    OUTPUT
  );


  digitalWrite(
    RED_LED_PIN,
    HIGH
  );

  digitalWrite(
    GREEN_LED_PIN,
    LOW
  );

  digitalWrite(
    HEARTBEAT_LED_PIN,
    LOW
  );

  digitalWrite(
    WIFI_LED_PIN,
    LOW
  );

  digitalWrite(
    BLYNK_LED_PIN,
    LOW
  );

  digitalWrite(
    DATA_LED_PIN,
    LOW
  );


  // ==================================================
  // LCD
  // ==================================================

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );


  Wire.setClock(
    100000
  );


  lcd.init();

  lcd.backlight();


  invalidateLCDCache();


  writeLCDLine(
    0,
    "Alarm system",
    true
  );


  writeLCDLine(
    1,
    "Starting...",
    true
  );


  previousLCDRecovery =
    millis();


  // ==================================================
  // BUTTONS
  // ==================================================

  initializeButton(
    button1,
    BUTTON_1_PIN
  );


  initializeButton(
    button2,
    BUTTON_2_PIN
  );


  // ==================================================
  // 3W LIGHT PWM
  // ==================================================

  ledcAttach(
    LIGHT_1_PIN,
    LIGHT_PWM_FREQUENCY,
    LIGHT_PWM_RESOLUTION
  );


  ledcAttach(
    LIGHT_2_PIN,
    LIGHT_PWM_FREQUENCY,
    LIGHT_PWM_RESOLUTION
  );


  ledcWrite(
    LIGHT_1_PIN,
    0
  );


  ledcWrite(
    LIGHT_2_PIN,
    0
  );


  // ==================================================
  // FAN PWM
  // ==================================================

  ledcAttach(
    FAN_PIN,
    FAN_PWM_FREQUENCY,
    FAN_PWM_RESOLUTION
  );


  ledcWrite(
    FAN_PIN,
    0
  );


  // ==================================================
  // BUZZER
  // ==================================================

#if PASSIVE_BUZZER

  ledcAttach(
    BUZZER_PIN,
    BUZZER_FREQUENCY,
    BUZZER_PWM_RESOLUTION
  );


  ledcWrite(
    BUZZER_PIN,
    0
  );

#else

  pinMode(
    BUZZER_PIN,
    OUTPUT
  );


  digitalWrite(
    BUZZER_PIN,
    LOW
  );

#endif


  // ==================================================
  // NETWORK
  // ==================================================

  initializeNetwork();


  // ==================================================
  // READY
  // ==================================================

  writeLCDLine(
    0,
    "System ready",
    true
  );


  if (
    WiFi.status() ==
    WL_CONNECTED
  )
  {
    writeLCDLine(
      1,
      Blynk.connected()
        ? "WiFi+Blynk OK"
        : "WiFi only",
      true
    );
  }

  else {

    writeLCDLine(
      1,
      "Offline",
      true
    );
  }


  delay(500);


  invalidateLCDCache();


  Serial.println();
  Serial.println(
    "===================="
  );

  Serial.println(
    "ALARM SYSTEM READY"
  );

  Serial.println(
    "===================="
  );
}


// ====================================================
// LOOP
// ====================================================

void loop()
{
  unsigned long now =
    millis();


  maintainConnections(
    now
  );


  maintainBlynkSync(
    now
  );


  processButtons(
    now
  );


  checkAlarmTrigger(
    now
  );


  updateAlarmSequence(
    now
  );


  updateOutputs(
    now
  );


  updateBoardLEDs(
    now
  );


  updateLCD(
    now
  );


  maintainLCD(
    now
  );
}