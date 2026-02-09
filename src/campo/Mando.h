#pragma once
class EntradaPulsador {
  int pin;
  bool state;
  bool prevRawState;
  bool longPress;
  unsigned long lastChangeTime;
  unsigned long debounceDelay;
  unsigned long pressStartTime;
  unsigned long longPressDelay;
public:
  EntradaPulsador(int buttonPin, unsigned long longPressDelay = 5000, unsigned long debounceTime = 50) : debounceDelay(debounceTime), longPressDelay(longPressDelay), pin(buttonPin)
  {
    state = prevRawState = longPress = 0;
    pressStartTime = 0;
  }
  void setup()
  {
    if (pin >= 0) pinMode(pin, INPUT_PULLUP);
  }

  int update()
  {
    if (pin < 0) return 0;
    bool rawState = !digitalRead(pin);
    unsigned long currentTime = millis();

    if (rawState != prevRawState) lastChangeTime = currentTime;
  
    prevRawState = rawState;

    if (currentTime - lastChangeTime > debounceDelay) {
      if (rawState && !state) {
        pressStartTime = lastChangeTime;
        state = true;
        return 1;
      } else if (!rawState && state) {
        state = false;
        if (longPress) {
          longPress = false;
          return -2;
        } else {
          return -1;
        }
      }
      if (state && !longPress && currentTime - pressStartTime > longPressDelay) {
        longPress = true;
        return 2;
      }
    }
    return 0;
  }
  bool down() { return state; }
  bool valid() { return pin >= 0; }
};
class LedRGB
{
  int pinR;
  int pinG;
  int pinB;
  public:
  void set(bool r, bool g, bool b, int blink=0)
  {
    if (((millis()/500)%2)+1 == blink) r = g = b = false;
    digitalWrite(pinR, r);
    digitalWrite(pinG, g);
    digitalWrite(pinB, b);
  }
};
