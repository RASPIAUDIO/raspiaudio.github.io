#include "paj7620u2.h"

#include "esphome/core/log.h"

namespace esphome::paj7620u2 {

static const char *const TAG = "paj7620u2";
static constexpr uint8_t BANK_SELECT_REGISTER = 0xEF;
static constexpr uint8_t PART_ID_LOW_REGISTER = 0x00;
static constexpr uint8_t GESTURE_FLAGS_REGISTER = 0x43;
static constexpr uint8_t WAVE_FLAGS_REGISTER = 0x44;
static constexpr uint32_t EVENT_COOLDOWN_MS = 650;
// A failed shared bus must not be hammered: PCAL, codec and XMOS use it too.
static constexpr uint32_t INIT_RETRY_MS = 10000;

// PAJ7620U2 initialization sequence from DFRobot_PAJ7620U2, MIT licensed.
static constexpr uint8_t INIT_REGISTERS[][2] = {
    {0xEF, 0x00}, {0x32, 0x29}, {0x33, 0x01}, {0x34, 0x00}, {0x35, 0x01}, {0x36, 0x00},
    {0x37, 0x07}, {0x38, 0x17}, {0x39, 0x06}, {0x3A, 0x12}, {0x3F, 0x00}, {0x40, 0x02},
    {0x41, 0xFF}, {0x42, 0x01}, {0x46, 0x2D}, {0x47, 0x0F}, {0x48, 0x3C}, {0x49, 0x00},
    {0x4A, 0x1E}, {0x4B, 0x00}, {0x4C, 0x20}, {0x4D, 0x00}, {0x4E, 0x1A}, {0x4F, 0x14},
    {0x50, 0x00}, {0x51, 0x10}, {0x52, 0x00}, {0x5C, 0x02}, {0x5D, 0x00}, {0x5E, 0x10},
    {0x5F, 0x3F}, {0x60, 0x27}, {0x61, 0x28}, {0x62, 0x00}, {0x63, 0x03}, {0x64, 0xF7},
    {0x65, 0x03}, {0x66, 0xD9}, {0x67, 0x03}, {0x68, 0x01}, {0x69, 0xC8}, {0x6A, 0x40},
    {0x6D, 0x04}, {0x6E, 0x00}, {0x6F, 0x00}, {0x70, 0x80}, {0x71, 0x00}, {0x72, 0x00},
    {0x73, 0x00}, {0x74, 0xF0}, {0x75, 0x00}, {0x80, 0x42}, {0x81, 0x44}, {0x82, 0x04},
    {0x83, 0x20}, {0x84, 0x20}, {0x85, 0x00}, {0x86, 0x10}, {0x87, 0x00}, {0x88, 0x05},
    {0x89, 0x18}, {0x8A, 0x10}, {0x8B, 0x01}, {0x8C, 0x37}, {0x8D, 0x00}, {0x8E, 0xF0},
    {0x8F, 0x81}, {0x90, 0x06}, {0x91, 0x06}, {0x92, 0x1E}, {0x93, 0x0D}, {0x94, 0x0A},
    {0x95, 0x0A}, {0x96, 0x0C}, {0x97, 0x05}, {0x98, 0x0A}, {0x99, 0x41}, {0x9A, 0x14},
    {0x9B, 0x0A}, {0x9C, 0x3F}, {0x9D, 0x33}, {0x9E, 0xAE}, {0x9F, 0xF9}, {0xA0, 0x48},
    {0xA1, 0x13}, {0xA2, 0x10}, {0xA3, 0x08}, {0xA4, 0x30}, {0xA5, 0x19}, {0xA6, 0x10},
    {0xA7, 0x08}, {0xA8, 0x24}, {0xA9, 0x04}, {0xAA, 0x1E}, {0xAB, 0x1E}, {0xCC, 0x19},
    {0xCD, 0x0B}, {0xCE, 0x13}, {0xCF, 0x64}, {0xD0, 0x21}, {0xD1, 0x0F}, {0xD2, 0x88},
    {0xE0, 0x01}, {0xE1, 0x04}, {0xE2, 0x41}, {0xE3, 0xD6}, {0xE4, 0x00}, {0xE5, 0x0C},
    {0xE6, 0x0A}, {0xE7, 0x00}, {0xE8, 0x00}, {0xE9, 0x00}, {0xEE, 0x07}, {0xEF, 0x01},
    {0x00, 0x1E}, {0x01, 0x1E}, {0x02, 0x0F}, {0x03, 0x10}, {0x04, 0x02}, {0x05, 0x00},
    {0x06, 0xB0}, {0x07, 0x04}, {0x08, 0x0D}, {0x09, 0x0E}, {0x0A, 0x9C}, {0x0B, 0x04},
    {0x0C, 0x05}, {0x0D, 0x0F}, {0x0E, 0x02}, {0x0F, 0x12}, {0x10, 0x02}, {0x11, 0x02},
    {0x12, 0x00}, {0x13, 0x01}, {0x14, 0x05}, {0x15, 0x07}, {0x16, 0x05}, {0x17, 0x07},
    {0x18, 0x01}, {0x19, 0x04}, {0x1A, 0x05}, {0x1B, 0x0C}, {0x1C, 0x2A}, {0x1D, 0x01},
    {0x1E, 0x00}, {0x21, 0x00}, {0x22, 0x00}, {0x23, 0x00}, {0x25, 0x01}, {0x26, 0x00},
    {0x27, 0x39}, {0x28, 0x7F}, {0x29, 0x08}, {0x30, 0x03}, {0x31, 0x00}, {0x32, 0x1A},
    {0x33, 0x1A}, {0x34, 0x07}, {0x35, 0x07}, {0x36, 0x01}, {0x37, 0xFF}, {0x38, 0x36},
    {0x39, 0x07}, {0x3A, 0x00}, {0x3E, 0xFF}, {0x3F, 0x00}, {0x40, 0x77}, {0x41, 0x40},
    {0x42, 0x00}, {0x43, 0x30}, {0x44, 0xA0}, {0x45, 0x5C}, {0x46, 0x00}, {0x47, 0x00},
    {0x48, 0x58}, {0x4A, 0x1E}, {0x4B, 0x1E}, {0x4C, 0x00}, {0x4D, 0x00}, {0x4E, 0xA0},
    {0x4F, 0x80}, {0x50, 0x00}, {0x51, 0x00}, {0x52, 0x00}, {0x53, 0x00}, {0x54, 0x00},
    {0x57, 0x80}, {0x59, 0x10}, {0x5A, 0x08}, {0x5B, 0x94}, {0x5C, 0xE8}, {0x5D, 0x08},
    {0x5E, 0x3D}, {0x5F, 0x99}, {0x60, 0x45}, {0x61, 0x40}, {0x63, 0x2D}, {0x64, 0x02},
    {0x65, 0x96}, {0x66, 0x00}, {0x67, 0x97}, {0x68, 0x01}, {0x69, 0xCD}, {0x6A, 0x01},
    {0x6B, 0xB0}, {0x6C, 0x04}, {0x6D, 0x2C}, {0x6E, 0x01}, {0x6F, 0x32}, {0x71, 0x00},
    {0x72, 0x01}, {0x73, 0x35}, {0x74, 0x00}, {0x75, 0x33}, {0x76, 0x31}, {0x77, 0x01},
    {0x7C, 0x84}, {0x7D, 0x03}, {0x7E, 0x01},
};

void PAJ7620U2::setup() {
  this->last_init_attempt_ms_ = millis();
  this->ready_ = this->initialize_();
}

bool PAJ7620U2::initialize_() {
  uint8_t part_id[2] = {0, 0};
  if (!this->write_byte(BANK_SELECT_REGISTER, 0x00) ||
      this->read_register(PART_ID_LOW_REGISTER, part_id, sizeof(part_id)) != i2c::ERROR_OK) {
    this->communication_errors_++;
    ESP_LOGW(TAG, "PAJ7620U2 not responding at 0x%02X", this->address_);
    this->status_set_warning();
    return false;
  }

  const uint16_t part = static_cast<uint16_t>(part_id[0]) |
                        (static_cast<uint16_t>(part_id[1]) << 8U);
  this->part_id_ = part;
  if (part != 0x7620) {
    ESP_LOGW(TAG, "Unexpected PAJ7620U2 part ID 0x%04X", part);
    this->status_set_warning();
    return false;
  }

  for (const auto &entry : INIT_REGISTERS) {
    if (!this->write_byte(entry[0], entry[1])) {
      this->communication_errors_++;
      ESP_LOGW(TAG, "Initialization failed at register 0x%02X", entry[0]);
      this->status_set_warning();
      return false;
    }
  }
  if (!this->write_byte(BANK_SELECT_REGISTER, 0x00)) {
    this->communication_errors_++;
    this->status_set_warning();
    return false;
  }

  this->consecutive_errors_ = 0;
  this->last_raw_gesture_ = 0;
  this->status_clear_warning();
  if (this->last_gesture_sensor_ != nullptr)
    this->last_gesture_sensor_->publish_state("Ready");
  ESP_LOGI(TAG, "PAJ7620U2 ready; unfiltered gesture debug enabled");
  return true;
}

void PAJ7620U2::update() {
  const uint32_t now = millis();
  if (!this->ready_) {
    if (now - this->last_init_attempt_ms_ >= INIT_RETRY_MS) {
      this->last_init_attempt_ms_ = now;
      this->ready_ = this->initialize_();
    }
    return;
  }

  uint8_t gesture_flags = 0;
  uint8_t wave_flags = 0;
  if (!this->write_byte(BANK_SELECT_REGISTER, 0x00) ||
      !this->read_byte(WAVE_FLAGS_REGISTER, &wave_flags) ||
      !this->read_byte(GESTURE_FLAGS_REGISTER, &gesture_flags)) {
    this->communication_errors_++;
    if (++this->consecutive_errors_ >= 3) {
      this->ready_ = false;
      this->last_init_attempt_ms_ = now;
      this->status_set_warning();
      if (this->last_gesture_sensor_ != nullptr)
        this->last_gesture_sensor_->publish_state("Unavailable: I2C read failed");
      ESP_LOGW(TAG, "Gesture reads failed; retrying sensor initialization");
    }
    return;
  }
  this->consecutive_errors_ = 0;
  // Count completed hardware read cycles, never timer ticks or HA publications.
  this->successful_reads_++;
  this->last_response_ms_ = millis();
  this->latest_gesture_flags_ = gesture_flags;
  this->latest_wave_flags_ = wave_flags;

  const uint16_t raw = static_cast<uint16_t>(gesture_flags) |
                       (static_cast<uint16_t>(wave_flags & 0x01U) << 8U);
  if (raw == 0) {
    this->last_raw_gesture_ = 0;
    return;
  }
  if (raw == this->last_raw_gesture_)
    return;

  this->last_raw_gesture_ = raw;
  this->emit_gesture_(raw, now);
}

std::string PAJ7620U2::communication_summary() const {
  if (this->successful_reads_ == 0)
    return str_sprintf("No complete read yet; init ID=0x%04X errors=%lu",
                      static_cast<unsigned>(this->part_id_),
                      static_cast<unsigned long>(this->communication_errors_));
  return str_sprintf("%s; init ID=0x%04X; reads=%lu errors=%lu age=%lums; R43=0x%02X R44=0x%02X",
                    this->ready_ ? "Responding" : "Disconnected",
                    static_cast<unsigned>(this->part_id_),
                    static_cast<unsigned long>(this->successful_reads_),
                    static_cast<unsigned long>(this->communication_errors_),
                    static_cast<unsigned long>(millis() - this->last_response_ms_),
                    static_cast<unsigned>(this->latest_gesture_flags_),
                    static_cast<unsigned>(this->latest_wave_flags_));
}

std::string PAJ7620U2::gesture_name_(uint16_t gesture_code) const {
  struct GestureBit {
    uint16_t mask;
    const char *name;
  };
  static constexpr GestureBit GESTURE_BITS[] = {
      {static_cast<uint16_t>(Gesture::RIGHT), "Right"},
      {static_cast<uint16_t>(Gesture::LEFT), "Left"},
      {static_cast<uint16_t>(Gesture::UP), "Up"},
      {static_cast<uint16_t>(Gesture::DOWN), "Down"},
      {static_cast<uint16_t>(Gesture::BACKWARD), "Backward"},
      {static_cast<uint16_t>(Gesture::FORWARD), "Forward"},
      {static_cast<uint16_t>(Gesture::CLOCKWISE), "Clockwise"},
      {static_cast<uint16_t>(Gesture::ANTI_CLOCKWISE), "Anti-clockwise"},
      {static_cast<uint16_t>(Gesture::WAVE), "Wave"},
  };

  std::string name;
  for (const auto &entry : GESTURE_BITS) {
    if ((gesture_code & entry.mask) == 0)
      continue;
    if (!name.empty())
      name += '|';
    name += entry.name;
  }
  return name.empty() ? "Unknown" : name;
}

void PAJ7620U2::emit_gesture_(uint16_t gesture_code, uint32_t now) {
  const std::string name = this->gesture_name_(gesture_code);
  ESP_LOGI(TAG, "Gesture raw=0x%04X flags=%s", static_cast<unsigned>(gesture_code), name.c_str());
  if (this->last_gesture_sensor_ != nullptr)
    this->last_gesture_sensor_->publish_state(
        str_sprintf("%s [0x%04X]", name.c_str(), static_cast<unsigned>(gesture_code)));

  // Every raw result is visible above. Product actions remain restricted to an
  // exact single-axis gesture so a combined debug event cannot fire two actions.
  if (now - this->last_event_ms_ < EVENT_COOLDOWN_MS)
    return;

  switch (gesture_code) {
    case static_cast<uint16_t>(Gesture::RIGHT):
      this->last_event_ms_ = now;
      this->right_callbacks_.call();
      break;
    case static_cast<uint16_t>(Gesture::LEFT):
      this->last_event_ms_ = now;
      this->left_callbacks_.call();
      break;
    case static_cast<uint16_t>(Gesture::UP):
      this->last_event_ms_ = now;
      this->up_callbacks_.call();
      break;
    case static_cast<uint16_t>(Gesture::DOWN):
      this->last_event_ms_ = now;
      this->down_callbacks_.call();
      break;
    case static_cast<uint16_t>(Gesture::BACKWARD):
      this->last_event_ms_ = now;
      this->backward_callbacks_.call();
      break;
    case static_cast<uint16_t>(Gesture::FORWARD):
      this->last_event_ms_ = now;
      this->forward_callbacks_.call();
      break;
    default:
      break;
  }
}

void PAJ7620U2::dump_config() {
  ESP_LOGCONFIG(TAG, "PAJ7620U2 gesture controller:");
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  Debug gestures: all flags and combinations, including circle and wave");
  ESP_LOGCONFIG(TAG, "  Product action cooldown: %lu ms", static_cast<unsigned long>(EVENT_COOLDOWN_MS));
}

}  // namespace esphome::paj7620u2
