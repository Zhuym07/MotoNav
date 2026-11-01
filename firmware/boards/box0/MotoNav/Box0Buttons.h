#pragma once
#include <array>
#include <cstdint>

// BOX0 三键：左=1、中=2、右=4。启动时按住的键先释放，之后才接收动作。
class Box0Buttons {
public:
  enum Event : std::uint16_t {
    Left = 1,
    Middle = 2,
    Right = 4,
    LeftLong = 8,
    RightLong = 16,
    MiddleLong = 32
  };

  std::uint16_t poll(std::uint8_t mask, std::uint64_t now) {
    std::uint16_t events = 0;
    for (unsigned i = 0; i < keys_.size(); ++i) {
      auto &k = keys_[i];
      const bool raw = (mask & (1U << i)) != 0;
      if (!k.initialized) {
        k.initialized = true;
        k.raw = k.stable = raw;
        k.armed = !raw;
        k.changed = now;
      }
      if (raw != k.raw) {
        k.raw = raw;
        k.changed = now;
      }
      if (k.stable != k.raw && now - k.changed >= 30) {
        k.stable = k.raw;
        if (k.stable) {
          k.pressed = now;
          k.longSent = false;
        } else {
          if (k.armed && !k.longSent) events |= static_cast<std::uint16_t>(1U << i);
          k.armed = true;
        }
      }
      if (k.stable && k.armed && !k.longSent &&
          now - k.pressed >= (i == 1 ? 3000U : 800U)) {
        k.longSent = true;
        events |= i == 1 ? MiddleLong : (i == 0 ? LeftLong : RightLong);
      }
    }
    return events;
  }

private:
  struct Key {
    bool initialized = false;
    bool raw = false;
    bool stable = false;
    bool armed = false;
    bool longSent = false;
    std::uint64_t changed = 0;
    std::uint64_t pressed = 0;
  };
  std::array<Key, 3> keys_{};
};