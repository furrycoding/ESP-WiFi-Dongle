#include <Arduino.h>

#include "LEDManager.h"

#define LED_PIN LED_BUILTIN
#define LED__ON LOW
#define LED__OFF HIGH


void LEDManager::setUp() {
    pinMode(LED_PIN, OUTPUT);
    
    blinkLength = 200;
    blinkCount = blinkActive = 0;

    activityTimer = 0;
    blinkTimer = 0;
    blinkCycleCounter = 0;

    lastState = 0;
    lastTime = millis();

    update();
}

void LEDManager::update() {
    auto curTime = millis();
    auto diff = curTime - lastTime;
    
    if (diff < LED_MANAGER_UPDATE_MS)
        return;
    
    lastTime = curTime;
    
    
    auto newState = updateImpl(diff);

    if (newState == lastState)
        return;
    
    lastState = newState;
    if (lastState)
        on();
    else
        off();
}

void LEDManager::on() {
    digitalWrite(LED_PIN, LED__ON);
}

void LEDManager::off() {
    digitalWrite(LED_PIN, LED__OFF);
}

void LEDManager::disablePattern() {
    setPattern(200, 0, 0);
}

void LEDManager::activity() {
    activityTimer = LED_ACTIVITY_MS;
}


// arg 1 - how long the LED stays on(and then the same amount of time stays off)
// arg 2 - how many total blink cycles are there in a pattern
// arg 3 - how many of those cycles *actually* blink
void LEDManager::setPattern(uint32_t blinkLengthMs, uint32_t blinkCycleCount, uint32_t blinkActiveCount) {
    blinkLength = blinkLengthMs;
    blinkCount = blinkCycleCount;
    blinkActive = blinkActiveCount;
}

bool LEDManager::updateImpl(int32_t diff) {
    if (activityTimer > 0) {
        activityTimer -= diff;
        return true;
    } else {
        activityTimer = 0;
    }

    if ((blinkCount < 1) || (blinkActive < 1)) {
        blinkTimer = 0;
        return false;
    }
    
    blinkTimer += diff;
    if (blinkTimer >= 2*blinkLength) {
        blinkCycleCounter++;
        blinkTimer = 0;
    }

    if (blinkCycleCounter >= blinkCount)
        blinkCycleCounter = 0;
    
    auto blink = (blinkCycleCounter < blinkActive) && (blinkTimer < blinkLength);
    return blink;
}
