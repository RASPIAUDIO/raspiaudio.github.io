#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#include "esphome/components/i2c/i2c.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome::paj7620u2 {

enum class Gesture : uint16_t {
  RIGHT = 0x01,
  LEFT = 0x02,
  UP = 0x04,
  DOWN = 0x08,
  BACKWARD = 0x10,
  FORWARD = 0x20,
  CLOCKWISE = 0x40,
  ANTI_CLOCKWISE = 0x80,
  WAVE = 0x100,
};

class PAJ7620U2 : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  bool is_ready() const { return this->ready_; }
  uint32_t successful_reads() const { return this->successful_reads_; }
  uint32_t communication_errors() const { return this->communication_errors_; }
  float last_response_age_seconds() const {
    return this->successful_reads_ == 0 ? NAN :
        static_cast<float>(millis() - this->last_response_ms_) / 1000.0f;
  }
  std::string communication_summary() const;

  void set_last_gesture_sensor(text_sensor::TextSensor *sensor) { this->last_gesture_sensor_ = sensor; }

  void add_on_right_callback(std::function<void()> &&callback) { this->right_callbacks_.add(std::move(callback)); }
  void add_on_left_callback(std::function<void()> &&callback) { this->left_callbacks_.add(std::move(callback)); }
  void add_on_up_callback(std::function<void()> &&callback) { this->up_callbacks_.add(std::move(callback)); }
  void add_on_down_callback(std::function<void()> &&callback) { this->down_callbacks_.add(std::move(callback)); }
  void add_on_forward_callback(std::function<void()> &&callback) { this->forward_callbacks_.add(std::move(callback)); }
  void add_on_backward_callback(std::function<void()> &&callback) { this->backward_callbacks_.add(std::move(callback)); }

 protected:
  bool initialize_();
  void emit_gesture_(uint16_t gesture_code, uint32_t now);
  std::string gesture_name_(uint16_t gesture_code) const;

  text_sensor::TextSensor *last_gesture_sensor_{nullptr};
  CallbackManager<void()> right_callbacks_;
  CallbackManager<void()> left_callbacks_;
  CallbackManager<void()> up_callbacks_;
  CallbackManager<void()> down_callbacks_;
  CallbackManager<void()> forward_callbacks_;
  CallbackManager<void()> backward_callbacks_;

  bool ready_{false};
  uint16_t part_id_{0};
  uint32_t successful_reads_{0};
  uint32_t communication_errors_{0};
  uint32_t last_response_ms_{0};
  uint8_t latest_gesture_flags_{0};
  uint8_t latest_wave_flags_{0};
  uint8_t consecutive_errors_{0};
  uint16_t last_raw_gesture_{0};
  uint32_t last_event_ms_{0};
  uint32_t last_init_attempt_ms_{0};
};

class RightGestureTrigger : public Trigger<> {
 public:
  explicit RightGestureTrigger(PAJ7620U2 *parent) {
    parent->add_on_right_callback([this]() { this->trigger(); });
  }
};

class LeftGestureTrigger : public Trigger<> {
 public:
  explicit LeftGestureTrigger(PAJ7620U2 *parent) {
    parent->add_on_left_callback([this]() { this->trigger(); });
  }
};

class UpGestureTrigger : public Trigger<> {
 public:
  explicit UpGestureTrigger(PAJ7620U2 *parent) {
    parent->add_on_up_callback([this]() { this->trigger(); });
  }
};

class DownGestureTrigger : public Trigger<> {
 public:
  explicit DownGestureTrigger(PAJ7620U2 *parent) {
    parent->add_on_down_callback([this]() { this->trigger(); });
  }
};

class ForwardGestureTrigger : public Trigger<> {
 public:
  explicit ForwardGestureTrigger(PAJ7620U2 *parent) {
    parent->add_on_forward_callback([this]() { this->trigger(); });
  }
};

class BackwardGestureTrigger : public Trigger<> {
 public:
  explicit BackwardGestureTrigger(PAJ7620U2 *parent) {
    parent->add_on_backward_callback([this]() { this->trigger(); });
  }
};

}  // namespace esphome::paj7620u2
