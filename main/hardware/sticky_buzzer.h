#pragma once

#include <cstdint>

class StickyBuzzer {
public:
    bool init();
    void beep(uint32_t frequency_hz = 2200, uint32_t duration_ms = 70);

private:
    bool ready_ = false;
};