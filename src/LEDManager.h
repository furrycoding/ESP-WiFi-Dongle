#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <stdint.h>

#define LED_ACTIVITY_MS 20
#define LED_MANAGER_UPDATE_MS 10


class LEDManager {

private:
    uint32_t lastTime;
    bool lastState;
    
    int32_t activityTimer, blinkTimer;
    uint32_t blinkCycleCounter;

    int32_t blinkLength;
    uint32_t blinkCount, blinkActive;

    bool updateImpl(int32_t diff);

public:
    void on();
    void off();
    void setUp();
    void update();

    void activity();

    // arg 1 - how long the LED stays on(and then the same amount of time stays off)
    // arg 2 - how many total blink cycles are there in a pattern
    // arg 3 - how many of those cycles *actually* blink
    void setPattern(uint32_t blinkLengthMs, uint32_t blinkCycleCount, uint32_t blinkActiveCount);
    void disablePattern();
};

#endif
