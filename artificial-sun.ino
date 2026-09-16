#include <Arduino.h>
#include <math.h>
#include <util/atomic.h>
#include "SevSeg.h"

#if !defined(ARDUINO_AVR_NANO_EVERY) || !defined(MILLIS_USE_TIMERB3)
#error "Select Arduino Nano Every with the official Arduino megaAVR Boards core."
#endif

SevSeg sevseg; // Instantiate a seven-segment object

// Timer variables
unsigned long setDuration = 7UL * 60 * 60 * 1000; // User-set duration
unsigned long remainingTime = setDuration;        // Remaining time in milliseconds
unsigned long lastTimerUpdateTime = 0;
unsigned long currentMillis = 0;
bool timerPaused = true;

// Display variables
unsigned long lastInteractionTime = 0;
bool displayOn = true;
unsigned long lastBlinkTime = 0;
bool displayBlinkState = true;

// Center button long press duration in milliseconds
const unsigned long LONG_PRESS_DURATION = 1000;

// LED brightness variables
float currentBrightness = 0.0f;
const unsigned long BRIGHTNESS_UPDATE_INTERVAL = 10; // milliseconds
const float BRIGHTNESS_SMOOTHING_TIME = 500.0f;       // milliseconds

// LED auto-off variables. Keep the light off until a new session starts.
unsigned long ledAutoOffStartTime = 0;
bool ledAutoOffArmed = false;
bool ledAutoOff = false;
const unsigned long LED_AUTO_OFF_DURATION = 60UL * 60 * 1000;

// Constants for acceleration effect
const unsigned long BASE_INTERVAL = 300; // Initial interval between increments (at start)
const unsigned long MIN_INTERVAL = 10;   // Minimum interval between increments (after pressing for long time)
const unsigned long MAX_PRESS_DURATION = 6000;

// pin assignments
const int buttonPin_l = 19;
const int buttonPin_c = 20;
const int buttonPin_r = 21;
const int ledPin = 6; // Nano Every D6 = PF4 = TCB0 output

// The core's TCA0 runs at CPU / 64 with a 256-count period. Leave that
// configuration alone: other Arduino timers use its clock for timekeeping.
// TCB0 runs at the full CPU clock, giving nominally 16384 pulse-width steps
// per period (about 977 Hz at 16 MHz), instead of analogWrite()'s 256 steps.
const uint16_t LED_PWM_PERIOD = 16384;
volatile uint16_t ledPulseTicks = 0;

void setupLedPwm() {
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  PORTMUX.TCBROUTEA |= PORTMUX_TCB0_bm; // Route TCB0 to PF4/D6.
  TCB0.CTRLA = 0;
  TCB0.CTRLB = TCB_CNTMODE_SINGLE_gc;
  TCB0.EVCTRL = 0;
  TCB0.INTCTRL = 0;
  TCB0.INTFLAGS = TCB_CAPT_bm;

  // Use the existing TCA0 overflow only as a pulse-start tick.
  // TCB3 (millis/micros), TCA0's clock, and the display pins are unchanged.
  TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
  TCA0.SINGLE.INTCTRL |= TCA_SINGLE_OVF_bm;
}

ISR(TCA0_OVF_vect) {
  TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
  const uint16_t ticks = ledPulseTicks;

  // Re-arm only at the start of a period. Never change CCMP mid-pulse:
  // lowering it below CNT could otherwise produce an unexpectedly long pulse.
  TCB0.CTRLA = 0;

  if (ticks >= LED_PWM_PERIOD) {
    PORTF.OUTSET = PIN4_bm;
    TCB0.CTRLB = TCB_CNTMODE_SINGLE_gc; // Steady HIGH, no timer output.
    return;
  }

  PORTF.OUTCLR = PIN4_bm;
  TCB0.CTRLB = TCB_CNTMODE_SINGLE_gc; // Steady LOW when ticks == 0.
  if (ticks == 0) return;

  TCB0.CCMP = ticks;
  TCB0.CNT = 0;
  TCB0.CTRLB = TCB_CNTMODE_SINGLE_gc | TCB_CCMPEN_bm;
  // Enabling single-shot mode with CNT < CCMP starts one pulse. Hardware
  // ends it. Interrupt latency can shift its start, not extend a low-duty pulse.
  TCB0.CTRLA = TCB_CLKSEL_CLKDIV1_gc | TCB_ENABLE_bm;
}

void writeLedPwm(uint16_t ticks) {
  if (ticks > LED_PWM_PERIOD) ticks = LED_PWM_PERIOD;
  // A 16-bit write is not atomic on this 8-bit MCU.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    ledPulseTicks = ticks;
  }
}

uint16_t convertToPWM(float brightnessValue) {
  // Zero-anchored exponential curve: 0 -> off, 1 -> fully on.
  // Keep fractional precision until the final 14-bit rounding step.
  if (!(brightnessValue > 0.0f)) return 0;
  if (brightnessValue >= 1.0f) return LED_PWM_PERIOD;
  const float exponentialRange = 147.4131591f; // exp(5) - 1
  const float duty = (expf(5.0f * brightnessValue) - 1.0f) / exponentialRange;
  return (uint16_t)(duty * LED_PWM_PERIOD + 0.5f);
}

// Button class to handle debouncing and state
class Button {
public:
  int pin;
  bool state;
  bool lastState;
  unsigned long lastDebounceTime;
  bool lastButtonPushed;
  unsigned long pressStartTime;
  bool ignoreCurrentPress;
  unsigned long lastIncrementTime;

  Button(int pin) : pin(pin), state(HIGH), lastState(HIGH), lastDebounceTime(0),
                    lastButtonPushed(false), pressStartTime(0),
                    ignoreCurrentPress(false), lastIncrementTime(0) {}

  bool checkButton() {
    // check the button state, with debouncing
    // returns true at the moment the button is pressed down
    const unsigned long debounceDelay = 50;
    int reading = digitalRead(pin);

    if (reading != lastState) {
      lastDebounceTime = currentMillis;
    }

    if ((currentMillis - lastDebounceTime) > debounceDelay) {
      if (reading != state) {
        state = reading;
        if (state == LOW) {
          return true; // Button down event
        }
      }
    }

    lastState = reading;
    return false;
  }

  bool isPressed() {
    return state == LOW;
  }

  bool isReleased() {
    return (!isPressed() && lastButtonPushed);
  }

  void updateLastButtonPushed() {
    lastButtonPushed = isPressed();
  }
};

// Instantiate button objects
Button leftButton(buttonPin_l);
Button centerButton(buttonPin_c);
Button rightButton(buttonPin_r);

// Explicit declarations also keep Arduino's sketch preprocessor happy.
void handleTimeAdjustment(Button &button, bool isIncrement);
void handleCenterButton(Button &button);

void setup() {
  byte numDigits = 4;
  byte digitPins[] = {5, 9, 10, 15};
  byte segmentPins[] = {7, 14, 17, 3, 2, 8, 16, 4};
  bool resistorsOnSegments = false; // 'false' means resistors are on digit pins
  byte hardwareConfig = COMMON_ANODE;
  bool updateWithDelays = false;
  bool leadingZeros = false;
  bool disableDecPoint = false;
  sevseg.begin(hardwareConfig, numDigits, digitPins, segmentPins,
               resistorsOnSegments, updateWithDelays, leadingZeros,
               disableDecPoint);

  setupLedPwm(); // Do not use analogWrite/digitalWrite on D6 after this.
  pinMode(leftButton.pin, INPUT_PULLUP);
  pinMode(centerButton.pin, INPUT_PULLUP);
  pinMode(rightButton.pin, INPUT_PULLUP);

  lastInteractionTime = millis();

  // Serial print for debugging
  // don't use serial print on final uploaded code, since it delays the loop
  // Serial.begin(115200);
}

void handleTimeAdjustment(Button &button, bool isIncrement) {
  // isIncrement: true for the increment button, false for the decrement button
  if (button.checkButton()) {
    lastInteractionTime = currentMillis;
    if (!displayOn) {
      displayOn = true;
      button.ignoreCurrentPress = true;
    } else {
      button.pressStartTime = currentMillis;
      button.ignoreCurrentPress = false;
    }
  }

  if (displayOn && !button.ignoreCurrentPress && button.isPressed() &&
      timerPaused) {
    unsigned long pressDuration = currentMillis - button.pressStartTime;
    // shorten the interval time between each step, the longer the button is pressed
    unsigned long interval = BASE_INTERVAL -
                             (BASE_INTERVAL - MIN_INTERVAL) *
                                 constrain((float)pressDuration /
                                               MAX_PRESS_DURATION,
                                           0, 1);
    interval = constrain(interval, MIN_INTERVAL, BASE_INTERVAL);

    if ((currentMillis - button.lastIncrementTime) >= interval) {
      unsigned long adjustAmount = 1UL * 60 * 1000; // 1 minute

      if (isIncrement) {
        remainingTime += adjustAmount;
      } else {
        remainingTime = remainingTime >= adjustAmount
                            ? remainingTime - adjustAmount
                            : 0;
      }
      setDuration = remainingTime; // Update setDuration
      button.lastIncrementTime = currentMillis;
    }
    lastInteractionTime = currentMillis;
  }

  if (button.isReleased()) {
    button.pressStartTime = 0;
    button.ignoreCurrentPress = false;
  }

  button.updateLastButtonPushed();
}

void handleCenterButton(Button &button) {
  static bool centerButtonLongPressHandled = false;
  static unsigned long centerButtonPressTime = 0;

  if (button.checkButton()) {
    centerButtonPressTime = currentMillis;
    centerButtonLongPressHandled = false;
    lastInteractionTime = currentMillis;
    if (!displayOn) {
      displayOn = true;
      button.ignoreCurrentPress = true;
    } else {
      button.ignoreCurrentPress = false;
    }
  }

  if (displayOn && !button.ignoreCurrentPress && button.isPressed()) {
    if (!centerButtonLongPressHandled) {
      unsigned long pressDuration = currentMillis - centerButtonPressTime;
      if (pressDuration >= LONG_PRESS_DURATION) {
        // Long press detected
        centerButtonLongPressHandled = true;
        remainingTime = setDuration; // Reset to previously set duration
        timerPaused = true;
        ledAutoOffArmed = false;
        ledAutoOff = false;
        lastInteractionTime = currentMillis;
        displayOn = true;
      }
    }
  }

  if (button.isReleased()) {
    unsigned long pressDuration = currentMillis - centerButtonPressTime;
    if (displayOn && !centerButtonLongPressHandled &&
        pressDuration < LONG_PRESS_DURATION && !button.ignoreCurrentPress) {
      // Short press detected
      timerPaused = !timerPaused; // Toggle pause/restart
      if (!timerPaused) {
        ledAutoOffArmed = false;
        ledAutoOff = false;
      }
      lastInteractionTime = currentMillis;
    }
    centerButtonPressTime = 0;
    button.ignoreCurrentPress = false;
  }

  button.updateLastButtonPushed();
}

void updateTimer() {
  if (!timerPaused && (currentMillis - lastTimerUpdateTime >= 1000)) {
    remainingTime = remainingTime >= 1000 ? remainingTime - 1000 : 0;

    if (remainingTime == 0) {
      timerPaused = true; // Stop the timer when it reaches zero
      ledAutoOffStartTime = currentMillis;
      ledAutoOffArmed = true;
      ledAutoOff = false;
    }
    lastTimerUpdateTime = currentMillis;
  }
}

void updateDisplay() {
  // Handle display blinking when paused
  // don't blink if the time is being adjusted (i.e. left or right button is pressed)
  if (timerPaused && !(leftButton.isPressed() || rightButton.isPressed())) {
    if (currentMillis - lastBlinkTime >= 300) {
      displayBlinkState = !displayBlinkState;
      lastBlinkTime = currentMillis;
    }
  } else {
    displayBlinkState = true;
  }

  if (displayOn) {
    if (displayBlinkState) {
      // e.g. 8 hours 10 mins -> show as "08.10" on the display
      unsigned long totalMinutes = remainingTime / 60000UL;
      unsigned int hours = totalMinutes / 60;
      unsigned int minutes = totalMinutes % 60;
      int displayNumber = hours * 100 + minutes;
      sevseg.setNumber(displayNumber, 2);
    } else {
      sevseg.blank();
    }
  } else {
    sevseg.blank();
  }
}

void updateBrightness() {
  // Limit the expensive math to 100 Hz; pulse widths are timed in hardware.
  static unsigned long lastBrightnessUpdateTime = 0;
  unsigned long elapsed = currentMillis - lastBrightnessUpdateTime;
  if (elapsed < BRIGHTNESS_UPDATE_INTERVAL) return;
  lastBrightnessUpdateTime = currentMillis;

  const unsigned long rampdown_dur = 10UL * 60 * 1000; // "sunset"
  const unsigned long rampup_dur = 20UL * 60 * 1000;   // "sunrise"
  float targetMaxBrightness = 0.0f;

  if (remainingTime <= rampup_dur) {
    targetMaxBrightness = (rampup_dur - remainingTime) / (float)rampup_dur;
    targetMaxBrightness = constrain(targetMaxBrightness, 0.0f, 1.0f);
  }
  else if ((setDuration - remainingTime) <= rampdown_dur && !timerPaused) {
    targetMaxBrightness = 1.0f - (setDuration - remainingTime) / (float)rampdown_dur;
    targetMaxBrightness *= 0.2f; // Sunset is dimmer, but now fades all the way to 0.
  }

  // Preserve the gentle 10-second pulse between 50% and 100% brightness.
  // Reduce the time before converting to float to keep phase precision after days.
  float phase = (currentMillis % 10000UL) / 10000.0f;
  float sineFactor = (sin(2.0f * PI * phase) + 1.0f) / 2.0f;
  float targetBrightness = targetMaxBrightness * (0.5f + 0.5f * sineFactor);

  if (displayOn)
    targetBrightness = constrain(targetBrightness, 0.0f, 0.03f);

  // Elapsed-time subtraction handles millis() rollover. Latch the off state;
  // clearing only the timer would let the sunrise logic relight the next loop.
  if (ledAutoOffArmed &&
      currentMillis - ledAutoOffStartTime >= LED_AUTO_OFF_DURATION) {
    ledAutoOffArmed = false;
    ledAutoOff = true;
  }
  if (ledAutoOff) targetBrightness = 0.0f;

  // Time-based smoothing, independent of display refresh speed / loop rate.
  float alpha = elapsed / (BRIGHTNESS_SMOOTHING_TIME + elapsed);
  currentBrightness += alpha * (targetBrightness - currentBrightness);

  uint16_t pwmValue = convertToPWM(currentBrightness);
  if (targetBrightness == 0.0f && pwmValue == 0) currentBrightness = 0.0f;
  writeLedPwm(pwmValue);
}

void checkInactivity() {
  // Turn off the display after 10 seconds of no interaction
  displayOn = (currentMillis - lastInteractionTime < 10000);
}

void loop() {
  currentMillis = millis();
  sevseg.refreshDisplay();

  // Handle buttons
  handleTimeAdjustment(leftButton, false); // Decrement time
  handleTimeAdjustment(rightButton, true); // Increment time
  handleCenterButton(centerButton);

  // Update timer and display
  updateTimer();
  updateDisplay();
  updateBrightness();
  checkInactivity();
}
