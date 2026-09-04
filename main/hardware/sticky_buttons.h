#pragma once

#include <atomic>
#include <cstdint>

#include "iot_button.h"

enum class StickyButtonId : uint8_t {
    Ai = 0,
    Up,
    Down,
};

enum class StickyButtonEventType : uint8_t {
    None = 0,
    ShortPress,
    LongPress,
};

struct StickyButtonEvent {
    StickyButtonId button = StickyButtonId::Ai;
    StickyButtonEventType type = StickyButtonEventType::None;

    bool valid() const
    {
        return type != StickyButtonEventType::None;
    }
};

class StickyButtons {
public:
    bool init();
    StickyButtonEvent poll();
    bool is_pressed(StickyButtonId button) const;

private:
    struct CallbackContext {
        StickyButtons *owner;
        StickyButtonId button;
    };

    static void on_short_press(void *button_handle, void *user_data);
    static void on_long_press(void *button_handle, void *user_data);
    void publish(StickyButtonId button, StickyButtonEventType type);

    button_handle_t handles_[3] = {};
    CallbackContext contexts_[3] = {};
    std::atomic<uint8_t> pending_event_ = 0;
};