#include "voice_dsp_plus.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace voice_dsp_plus {

static const char *const TAG = "voice_dsp_plus";

namespace {

constexpr float EQ_SAMPLE_RATE = 48000.0f;
constexpr float PI = 3.14159265358979323846f;
constexpr float EQ_BASS_FREQUENCY = 120.0f;
constexpr float EQ_TREBLE_FREQUENCY = 6000.0f;
constexpr float EQ_MAX_DB = 6.0f;
constexpr float Q15_MAX = 32767.0f / 32768.0f;

struct BiquadCoefficients {
  float n0;
  float n1;
  float n2;
  float d1;
  float d2;
  float numerator_scale;
};

BiquadCoefficients identity_biquad() { return {Q15_MAX, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}; }

BiquadCoefficients shelf_biquad(float gain_db, float frequency, bool high_shelf) {
  if (std::fabs(gain_db) < 0.01f)
    return identity_biquad();

  const float a = std::pow(10.0f, gain_db / 40.0f);
  const float omega = 2.0f * PI * frequency / EQ_SAMPLE_RATE;
  const float cosine = std::cos(omega);
  const float alpha = std::sin(omega) * std::sqrt(2.0f) / 2.0f;
  const float two_sqrt_a_alpha = 2.0f * std::sqrt(a) * alpha;

  float b0;
  float b1;
  float b2;
  float a0;
  float a1;
  float a2;
  if (high_shelf) {
    b0 = a * ((a + 1.0f) + (a - 1.0f) * cosine + two_sqrt_a_alpha);
    b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine);
    b2 = a * ((a + 1.0f) + (a - 1.0f) * cosine - two_sqrt_a_alpha);
    a0 = (a + 1.0f) - (a - 1.0f) * cosine + two_sqrt_a_alpha;
    a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosine);
    a2 = (a + 1.0f) - (a - 1.0f) * cosine - two_sqrt_a_alpha;
  } else {
    b0 = a * ((a + 1.0f) - (a - 1.0f) * cosine + two_sqrt_a_alpha);
    b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosine);
    b2 = a * ((a + 1.0f) - (a - 1.0f) * cosine - two_sqrt_a_alpha);
    a0 = (a + 1.0f) + (a - 1.0f) * cosine + two_sqrt_a_alpha;
    a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cosine);
    a2 = (a + 1.0f) + (a - 1.0f) * cosine - two_sqrt_a_alpha;
  }

  // TI stores N1 and D1 divided by two. D1/D2 use the opposite sign
  // from the conventional denominator 1 + a1*z^-1 + a2*z^-2.
  BiquadCoefficients result = {b0 / a0, b1 / (2.0f * a0), b2 / a0,
                                -a1 / (2.0f * a0), -a2 / a0, 1.0f};

  // The coefficient RAM is signed Q1.15. Preserve the shelf shape by
  // scaling only its numerator; the DAC digital volume restores this common
  // attenuation while retaining explicit headroom for positive EQ gain.
  const float max_numerator =
      std::max({std::fabs(result.n0), std::fabs(result.n1), std::fabs(result.n2)});
  if (max_numerator > Q15_MAX) {
    result.numerator_scale = Q15_MAX / max_numerator;
    result.n0 *= result.numerator_scale;
    result.n1 *= result.numerator_scale;
    result.n2 *= result.numerator_scale;
  }
  return result;
}

int16_t q15_coefficient(float coefficient) {
  const float clipped = std::clamp(coefficient, -1.0f, Q15_MAX);
  return static_cast<int16_t>(std::lround(clipped * 32768.0f));
}

void encode_biquad(const BiquadCoefficients &coefficients, uint8_t *output) {
  const int16_t encoded[5] = {
      q15_coefficient(coefficients.n0), q15_coefficient(coefficients.n1),
      q15_coefficient(coefficients.n2), q15_coefficient(coefficients.d1),
      q15_coefficient(coefficients.d2)};
  for (size_t index = 0; index < 5; ++index) {
    output[index * 2] = static_cast<uint8_t>((static_cast<uint16_t>(encoded[index]) >> 8) & 0xFF);
    output[index * 2 + 1] = static_cast<uint8_t>(static_cast<uint16_t>(encoded[index]) & 0xFF);
  }
}

}  // namespace

bool VoiceDSPPlus::read_pcal_(uint8_t reg, uint8_t &value) {
  return this->read_register(reg, &value, 1) == i2c::ERROR_OK;
}

bool VoiceDSPPlus::write_pcal_(uint8_t reg, uint8_t value) {
  return this->write_register(reg, &value, 1) == i2c::ERROR_OK;
}

bool VoiceDSPPlus::update_pcal_bits_(uint8_t mask, bool high) {
  uint8_t output = 0;
  if (!this->read_pcal_(PCAL_OUTPUT, output))
    return false;
  output = high ? (output | mask) : (output & ~mask);
  return this->write_pcal_(PCAL_OUTPUT, output);
}

bool VoiceDSPPlus::read_dac_(uint8_t page, uint8_t reg, uint8_t &value) {
  uint8_t page_packet[2] = {0x00, page};
  if (this->bus_->write(this->dac_address_, page_packet, sizeof(page_packet)) != i2c::ERROR_OK)
    return false;
  if (this->bus_->write(this->dac_address_, &reg, 1) != i2c::ERROR_OK)
    return false;
  return this->bus_->read(this->dac_address_, &value, 1) == i2c::ERROR_OK;
}

bool VoiceDSPPlus::write_dac_(uint8_t page, uint8_t reg, uint8_t value) {
  uint8_t page_packet[2] = {0x00, page};
  uint8_t reg_packet[2] = {reg, value};
  if (this->bus_->write(this->dac_address_, page_packet, sizeof(page_packet)) != i2c::ERROR_OK)
    return false;
  return this->bus_->write(this->dac_address_, reg_packet, sizeof(reg_packet)) == i2c::ERROR_OK;
}

bool VoiceDSPPlus::write_dac_block_(uint8_t page, uint8_t reg, const uint8_t *data,
                                    size_t data_len) {
  if (data_len > 16)
    return false;
  uint8_t page_packet[2] = {0x00, page};
  uint8_t reg_packet[17] = {reg};
  memcpy(&reg_packet[1], data, data_len);
  if (this->bus_->write(this->dac_address_, page_packet, sizeof(page_packet)) != i2c::ERROR_OK)
    return false;
  return this->bus_->write(this->dac_address_, reg_packet, data_len + 1) == i2c::ERROR_OK;
}

bool VoiceDSPPlus::write_dac_volume_db_(float gain_db) {
  const int gain_steps = std::clamp(static_cast<int>(std::lround(gain_db * 2.0f)), -127, 48);
  const uint8_t encoded = static_cast<uint8_t>(static_cast<int8_t>(gain_steps));
  return this->write_dac_(0, 0x41, encoded) && this->write_dac_(0, 0x42, encoded);
}

bool VoiceDSPPlus::apply_output_equalizer_() {
  const BiquadCoefficients filters[3] = {
      shelf_biquad(this->output_eq_bass_db_, EQ_BASS_FREQUENCY, false),
      shelf_biquad(this->output_eq_treble_db_, EQ_TREBLE_FREQUENCY, true),
      identity_biquad()};
  const uint8_t left_registers[3] = {0x02, 0x0C, 0x16};
  const uint8_t right_registers[3] = {0x42, 0x4C, 0x56};

  const float coefficient_attenuation_db =
      20.0f * std::log10(filters[0].numerator_scale * filters[1].numerator_scale);
  const float boost_headroom_db =
      std::max({0.0f, this->output_eq_bass_db_, this->output_eq_treble_db_});
  const float requested_digital_gain_db =
      std::clamp(-coefficient_attenuation_db - boost_headroom_db, -24.0f, 12.0f);
  // Round toward attenuation so the codec's 0.5 dB volume granularity never
  // turns a mathematically safe curve into a small positive peak.
  const float digital_gain_db = std::floor(requested_digital_gain_db * 2.0f) / 2.0f;

  // Attenuate before the filter switch. Positive compensation is applied
  // only after the new bank becomes active at a sample-frame boundary.
  bool ok = this->write_dac_volume_db_(std::min(0.0f, digital_gain_db));
  ok &= this->write_dac_(8, 0x01, 0x04);  // Adaptive mode, write inactive bank.
  for (size_t filter = 0; filter < 3; ++filter) {
    uint8_t encoded[10];
    encode_biquad(filters[filter], encoded);
    ok &= this->write_dac_block_(8, left_registers[filter], encoded, sizeof(encoded));
    ok &= this->write_dac_block_(8, right_registers[filter], encoded, sizeof(encoded));
  }
  ok &= this->write_dac_(8, 0x01, 0x05);  // Activate inactive bank next frame.
  delay(2);
  ok &= this->write_dac_volume_db_(digital_gain_db);
  if (ok) {
    this->output_eq_digital_gain_db_ = digital_gain_db;
    ESP_LOGI(TAG, "Output EQ bass=%.1f dB treble=%.1f dB codec_gain=%.1f dB",
             this->output_eq_bass_db_, this->output_eq_treble_db_, digital_gain_db);
  }
  return ok;
}

bool VoiceDSPPlus::apply_output_equalizer(float bass_db, float treble_db) {
  this->output_eq_bass_db_ = std::clamp(bass_db, -EQ_MAX_DB, EQ_MAX_DB);
  this->output_eq_treble_db_ = std::clamp(treble_db, -EQ_MAX_DB, EQ_MAX_DB);
  const bool ok = this->apply_output_equalizer_();
  if (!ok)
    ESP_LOGE(TAG, "Failed to update TLV320DAC3101 output equalizer");
  return ok;
}

std::string VoiceDSPPlus::audio_output_status() {
  uint8_t output = 0;
  uint8_t config = 0;
  uint8_t dac_path = 0;
  uint8_t dac_mute = 0;
  uint8_t dac_left = 0;
  uint8_t dac_right = 0;
  if (!this->read_pcal_(PCAL_OUTPUT, output) || !this->read_pcal_(PCAL_CONFIG, config))
    return "PCAL read failed";
  if (!this->read_dac_(0, 0x3F, dac_path) || !this->read_dac_(0, 0x40, dac_mute) ||
      !this->read_dac_(0, 0x41, dac_left) || !this->read_dac_(0, 0x42, dac_right))
    return "DAC read failed";

  const bool amp_ctrl1 = (output & 0x10) != 0;
  const bool amp_ctrl2 = (output & 0x80) != 0;
  const char *amp_mode = "shutdown";
  if (amp_ctrl1 && !amp_ctrl2)
    amp_mode = "Class-AB boost off";
  else if (!amp_ctrl1 && amp_ctrl2)
    amp_mode = "Class-D boost ACF";
  else if (amp_ctrl1 && amp_ctrl2)
    amp_mode = "Class-D full";

  const float left_db = static_cast<int8_t>(dac_left) * 0.5f;
  const float right_db = static_cast<int8_t>(dac_right) * 0.5f;
  char status[192];
  std::snprintf(status, sizeof(status),
                "P4=%u P7=%u %s; PCAL out=0x%02X cfg=0x%02X; DAC path=0x%02X mute=0x%02X L=%.1f R=%.1f dB",
                amp_ctrl1, amp_ctrl2, amp_mode, output, config, dac_path, dac_mute, left_db, right_db);
  return status;
}

bool VoiceDSPPlus::send_xmos_gpo_(uint8_t pin, bool state) {
  const uint8_t packet[6] = {0x14, 0x01, 0x03, 0x00, pin, static_cast<uint8_t>(state)};
  uint8_t status = 0xFF;
  if (this->bus_->write(this->xmos_address_, packet, sizeof(packet)) != i2c::ERROR_OK)
    return false;
  delay(2);
  if (this->bus_->read(this->xmos_address_, &status, 1) != i2c::ERROR_OK)
    return false;
  return status == 0;
}

bool VoiceDSPPlus::publish_xmos_leds_(bool jack, bool square) {
  const bool was_responding = this->xmos_control_responding_;
  bool red_ok = this->send_xmos_gpo_(6, jack);
  bool green_ok = this->send_xmos_gpo_(7, square);
  this->xmos_control_responding_ = red_ok && green_ok;
  if (this->xmos_status_sensor_ != nullptr && this->xmos_control_responding_ != was_responding) {
    this->xmos_status_sensor_->publish_state(this->xmos_status_string());
  }
  return this->xmos_control_responding_;
}

bool VoiceDSPPlus::configure_headphone_() {
  bool ok = this->write_pcal_(PCAL_OUTPUT, OUTPUT_HEADPHONE);
  ok &= this->write_pcal_(PCAL_CONFIG, PCAL_CONFIG_VOICE_DSP);
  ok &= this->write_dac_(1, 0x23, 0x44);
  ok &= this->write_dac_(1, 0x24, 0x80);
  ok &= this->write_dac_(1, 0x25, 0x80);
  ok &= this->write_dac_(1, 0x26, 0x80);
  ok &= this->write_dac_(1, 0x27, 0x80);
  // Left DAC -> left output and right DAC -> right output.
  ok &= this->write_dac_(0, 0x3F, 0xD4);
  ok &= this->write_dac_(0, 0x40, 0x00);
  ok &= this->write_dac_(0, 0x41, 0x00);
  ok &= this->write_dac_(0, 0x42, 0x00);
  return ok;
}

bool VoiceDSPPlus::configure_speaker_() {
  // Keep the amp disabled while the full 48 kHz DAC sequence is restored.
  bool ok = this->write_pcal_(PCAL_OUTPUT, OUTPUT_HEADPHONE);
  ok &= this->write_pcal_(PCAL_CONFIG, PCAL_CONFIG_VOICE_DSP);
  ok &= this->write_dac_(0, 0x01, 0x01);
  delay(100);
  ok &= this->write_dac_(0, 0x06, 0x08);
  ok &= this->write_dac_(0, 0x08, 0x00);
  ok &= this->write_dac_(0, 0x07, 0x00);
  ok &= this->write_dac_(0, 0x1E, 0x81);
  delay(100);
  const uint8_t page0[][2] = {
      {0x04, 0x07}, {0x05, 0x94}, {0x0B, 0x84}, {0x0C, 0x84}, {0x0E, 0x80},
      {0x0D, 0x00}, {0x19, 0x04}, {0x1A, 0x81}, {0x33, 0x10}, {0x1B, 0x20}, {0x3C, 0x01}};
  for (const auto &entry : page0)
    ok &= this->write_dac_(0, entry[0], entry[1]);
  const uint8_t page1[][2] = {
      {0x1F, 0x14}, {0x21, 0x4E}, {0x23, 0x41}, {0x24, 0x80}, {0x25, 0x80},
      {0x26, 0xA8}, {0x27, 0xA8}, {0x28, 0x06}, {0x29, 0x06}, {0x2A, 0x04},
      {0x2B, 0x04}, {0x1F, 0xD4}, {0x20, 0xC6}};
  for (const auto &entry : page1)
    ok &= this->write_dac_(1, entry[0], entry[1]);
  ok &= this->write_dac_(0, 0x40, 0x0C);
  ok &= this->write_dac_(0, 0x41, 0x00);
  ok &= this->write_dac_(0, 0x42, 0x00);
  ok &= this->write_dac_(0, 0x3F, 0x90);
  ok &= this->write_pcal_(PCAL_OUTPUT, OUTPUT_SPEAKER);
  ok &= this->write_dac_(0, 0x40, 0x00);
  return ok;
}

void VoiceDSPPlus::apply_route_(bool jack) {
  bool ok = jack ? this->configure_headphone_() : this->configure_speaker_();
  if (ok)
    ok = this->apply_output_equalizer_();
  if (!ok) {
    ESP_LOGE(TAG, "Failed to configure %s route", jack ? "headphone" : "speaker");
    this->status_set_error(LOG_STR("Audio route I2C failure"));
  } else {
    ESP_LOGI(TAG, "Audio route: %s", jack ? "headphone stereo, amp muted" : "speaker differential mono");
    this->status_clear_error();
  }
}

void VoiceDSPPlus::setup() {
  uint8_t input = 0;
  uint8_t output = 0;
  bool pcal_ready = false;
  // Setup must not spend seconds retrying a physically stuck shared bus. The
  // regular update loop keeps retrying after boot without tripping the WDT.
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    if (this->read_pcal_(PCAL_OUTPUT, output)) {
      pcal_ready = true;
      break;
    }
    delay(10);
  }
  if (!pcal_ready) {
    ESP_LOGE(TAG, "PCAL6408A not found at 0x%02X", this->address_);
    this->status_set_warning("PCAL unavailable during setup; polling will retry");
    return;
  }
  // Normal QSPI boot: BOOT_SEL low, reset released. Preserve DAC reset high.
  output &= ~P3_BOOT_SEL;
  output |= P0_XVF_RESET_N;
  if (!this->write_pcal_(PCAL_OUTPUT, output) ||
      !this->write_pcal_(PCAL_CONFIG, PCAL_CONFIG_VOICE_DSP) ||
      !this->read_pcal_(PCAL_INPUT, input)) {
    ESP_LOGE(TAG, "PCAL6408A initialization failed");
    this->status_set_warning("PCAL initialization incomplete; polling will retry");
    return;
  }
  this->last_jack_ = (input & P5_JACK) != 0;
  this->last_square_ = (input & P6_SQUARE) != 0;
  this->state_valid_ = true;
  if (this->jack_sensor_ != nullptr)
    this->jack_sensor_->publish_state(this->last_jack_);
  if (this->square_sensor_ != nullptr)
    this->square_sensor_->publish_state(this->last_square_);
  this->apply_route_(this->last_jack_);
  delay(20);
  this->publish_xmos_leds_(this->last_jack_, this->last_square_);
}

void VoiceDSPPlus::update() {
  if (this->polling_suspended_)
    return;

  // Raw-microphone diagnostics talk directly to the XMOS endpoint. Keep them
  // independent from PCAL jack/geometry polling so a peripheral fault cannot
  // hide the health of the four physical microphones.
  const uint32_t now = millis();
  if (this->mic_diagnostic_active_ && now - this->last_mic_level_poll_ms_ >= 200) {
    this->last_mic_level_poll_ms_ = now;
    if (!this->refresh_mic_levels_()) {
      // Never keep hammering a shared bus after a failed diagnostic read.
      this->mic_diagnostic_active_ = false;
      if (this->mic_diagnostic_status_sensor_ != nullptr)
        this->mic_diagnostic_status_sensor_->publish_state("ERROR: microphone telemetry unavailable; test stopped");
    }
  }
  if (this->mic_diagnostic_active_ && now - this->mic_diagnostic_started_ms_ >= 5000)
    this->finish_microphone_diagnostic_();

  uint8_t input = 0;
  if (!this->read_pcal_(PCAL_INPUT, input)) {
    this->status_set_warning("PCAL input read failed");
    return;
  }
  this->status_clear_warning();
  const bool jack = (input & P5_JACK) != 0;
  const bool square = (input & P6_SQUARE) != 0;
  bool leds_dirty = !this->state_valid_ || !this->xmos_control_responding_;
  if (!this->state_valid_ || jack != this->last_jack_) {
    leds_dirty = true;
    this->last_jack_ = jack;
    if (this->jack_sensor_ != nullptr)
      this->jack_sensor_->publish_state(jack);
    this->apply_route_(jack);
  }
  if (!this->state_valid_ || square != this->last_square_) {
    leds_dirty = true;
    this->last_square_ = square;
    if (this->square_sensor_ != nullptr)
      this->square_sensor_->publish_state(square);
    ESP_LOGW(TAG, "Microphone geometry changed to %s; XMOS applies it on the next cold boot",
             square ? "SQUARE" : "LINE");
  }
  this->state_valid_ = true;
  if (leds_dirty)
    this->publish_xmos_leds_(jack, square);

  if (now - this->last_doa_poll_ms_ >= 1000) {
    this->last_doa_poll_ms_ = now;
    this->refresh_doa_();
  }
}

bool VoiceDSPPlus::xmos_control_write_(uint8_t resid, uint8_t command, const uint8_t *payload,
                                       size_t payload_len) {
  if (payload_len > 252)
    return false;

  uint8_t packet[255] = {resid, static_cast<uint8_t>(command & ~CONTROL_READ),
                         static_cast<uint8_t>(payload_len)};
  if (payload_len != 0)
    memcpy(&packet[3], payload, payload_len);

  for (unsigned attempt = 0; attempt < 100; ++attempt) {
    uint8_t status = 0xFF;
    if (this->bus_->write(this->xmos_address_, packet, payload_len + 3) != i2c::ERROR_OK)
      return false;
    delay(1);
    if (this->bus_->read(this->xmos_address_, &status, 1) != i2c::ERROR_OK)
      return false;
    if (status == CONTROL_SUCCESS) {
      delay(5);
      return true;
    }
    if (status != CONTROL_RETRY && status != CONTROL_QUEUE_FULL) {
      ESP_LOGE(TAG, "XMOS write resid=0x%02X cmd=%u failed, status=%u", resid, command, status);
      return false;
    }
    delay(status == CONTROL_QUEUE_FULL ? 5 : 1);
  }
  ESP_LOGE(TAG, "XMOS write resid=0x%02X cmd=%u remained busy", resid, command);
  return false;
}

bool VoiceDSPPlus::xmos_control_read_(uint8_t resid, uint8_t command, uint8_t *payload,
                                      size_t payload_len, unsigned max_attempts) {
  if (payload_len > 254)
    return false;

  const uint8_t packet[3] = {resid, static_cast<uint8_t>(command | CONTROL_READ),
                             static_cast<uint8_t>(payload_len + 1)};
  uint8_t response[255] = {};
  // Some BeClear resources complete asynchronously. XMOS' host utility allows
  // up to 101 SERVICER_COMMAND_RETRY responses, spaced by 10 ms.
  for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
    if (this->bus_->write_readv(this->xmos_address_, packet, sizeof(packet), response, payload_len + 1) !=
        i2c::ERROR_OK)
      return false;
    if (response[0] == CONTROL_SUCCESS) {
      if (payload_len != 0)
        memcpy(payload, &response[1], payload_len);
      return true;
    }
    if (response[0] != CONTROL_RETRY) {
      ESP_LOGE(TAG, "XMOS read resid=0x%02X cmd=%u failed, status=%u", resid, command, response[0]);
      return false;
    }
    delay(10);
  }
  ESP_LOGE(TAG, "XMOS read resid=0x%02X cmd=%u remained busy", resid, command);
  return false;
}

void VoiceDSPPlus::start_doa_diagnostic() {
  if (this->polling_suspended_ || !this->xmos_control_responding_) {
    ESP_LOGW(TAG, "DOA_DIAGNOSTIC refused: XMOS control not ready");
    return;
  }
  // Bounded, read-only probes. P6 live state is not proof of boot geometry.
  int32_t array_type = 0;
  float coordinates[12] = {};
  if (!this->xmos_control_read_(AEC_RESID, 73, reinterpret_cast<uint8_t *>(&array_type),
                                sizeof(array_type), 3) ||
      !this->xmos_control_read_(AEC_RESID, 74, reinterpret_cast<uint8_t *>(coordinates),
                                sizeof(coordinates), 3)) {
    ESP_LOGW(TAG, "DOA_DIAGNOSTIC boot geometry read failed");
    return;
  }
  ESP_LOGI(TAG, "DOA_DIAGNOSTIC boot_array_type=%ld live_P6_square=%d duration_s=120",
           static_cast<long>(array_type), this->last_square_);
  for (unsigned mic = 0; mic < 4; mic++) {
    ESP_LOGI(TAG, "DOA_DIAGNOSTIC mic=%u xyz_m=%.4f,%.4f,%.4f", mic,
             coordinates[3 * mic], coordinates[3 * mic + 1], coordinates[3 * mic + 2]);
  }
  this->doa_diagnostic_started_ms_ = millis();
  this->doa_diagnostic_active_ = true;
}

bool VoiceDSPPlus::refresh_doa_() {
  float azimuths[4] = {};
  float energies[4] = {};
  // DOA is diagnostic telemetry, not an audio-critical control. Bound each
  // asynchronous read to 20 ms so a busy AEC servicer cannot stall the
  // ESPHome loop and starve microphone/wake-word processing during playback.
  constexpr unsigned DOA_MAX_ATTEMPTS = 3;
  if (!this->xmos_control_read_(AEC_RESID, AEC_AZIMUTH_VALUES,
                                reinterpret_cast<uint8_t *>(azimuths), sizeof(azimuths), DOA_MAX_ATTEMPTS) ||
      !this->xmos_control_read_(AEC_RESID, AEC_SPENERGY_VALUES,
                                reinterpret_cast<uint8_t *>(energies), sizeof(energies), DOA_MAX_ATTEMPTS)) {
    return false;
  }

  const float radians = azimuths[AEC_AUTO_SELECTED_INDEX];
  const float energy = energies[AEC_AUTO_SELECTED_INDEX];
  if (!std::isfinite(radians) || !std::isfinite(energy))
    return false;

  float degrees = std::fmod(radians * 180.0f / 3.14159265358979323846f, 360.0f);
  if (degrees < 0.0f)
    degrees += 360.0f;
  if (this->doa_sensor_ != nullptr)
    this->doa_sensor_->publish_state(degrees);
  if (this->doa_energy_sensor_ != nullptr)
    this->doa_energy_sensor_->publish_state(energy);
  if (this->doa_diagnostic_active_) {
    if (millis() - this->doa_diagnostic_started_ms_ >= 120000U) {
      this->doa_diagnostic_active_ = false;
      ESP_LOGI(TAG, "DOA_DIAGNOSTIC finished");
    } else {
      uint8_t ui_state = 255;
      const bool ui_ok = this->xmos_control_read_(APPLICATION_RESID, 9, &ui_state, 1, 3);
      ESP_LOGI(TAG, "DOA_DIAGNOSTIC ui=%d az_rad=%.5f,%.5f,%.5f,%.5f energy=%.6g,%.6g,%.6g,%.6g",
               ui_ok ? static_cast<int>(ui_state) : -1,
               azimuths[0], azimuths[1], azimuths[2], azimuths[3],
               energies[0], energies[1], energies[2], energies[3]);
      float selected[2] = {};
      if (this->xmos_control_read_(0x23, 11, reinterpret_cast<uint8_t *>(selected), sizeof(selected), 3)) {
        ESP_LOGI(TAG, "DOA_RING processed_rad=%.6f auto_rad=%.6f", selected[0], selected[1]);
      } else {
        ESP_LOGW(TAG, "DOA_RING read unavailable");
      }
    }
  }
  return true;
}

bool VoiceDSPPlus::refresh_mic_levels_() {
  float raw_levels[4] = {};
  constexpr unsigned MIC_LEVEL_MAX_ATTEMPTS = 3;
  if (!this->xmos_control_read_(APPLICATION_RESID, APPLICATION_MIC_RAW_LEVELS,
                                reinterpret_cast<uint8_t *>(raw_levels), sizeof(raw_levels),
                                MIC_LEVEL_MAX_ATTEMPTS)) {
    return false;
  }

  // XMOS 0.2.11 reports BeClear input RMS in signed-16-bit sample units,
  // not normalized +/-1. Use that full scale for display and silence gating.
  constexpr float BECLEAR_INPUT_FULL_SCALE = 32768.0f;
  for (float &level : raw_levels) {
    if (!std::isfinite(level) || level < 0.0f)
      return false;
    level /= BECLEAR_INPUT_FULL_SCALE;
  }
  for (unsigned mic = 0; mic < 4; ++mic) {
    const float level_db = 20.0f * std::log10(std::max(std::fabs(raw_levels[mic]), 1.0e-6f));
    if (this->mic_level_sensors_[mic] != nullptr)
      this->mic_level_sensors_[mic]->publish_state(level_db);
    if (this->mic_diagnostic_active_)
      this->mic_diagnostic_sums_[mic] += raw_levels[mic] * raw_levels[mic];
  }
  if (this->mic_diagnostic_active_ && this->mic_diagnostic_samples_ < 255)
    ++this->mic_diagnostic_samples_;
  return true;
}

void VoiceDSPPlus::start_microphone_diagnostic() {
  this->mic_diagnostic_active_ = false;
  // The v0.2.9 implementation could trap on an unaligned float store. Do not
  // issue MIC_RAW_LEVELS to that firmware, including during upgrade/fallback.
  uint8_t version[3] = {};
  if (this->polling_suspended_ ||
      !this->xmos_control_read_(APPLICATION_RESID, 0, version, sizeof(version), 3) ||
      version[0] != 0 || version[1] != 2 || version[2] < 11) {
    if (this->mic_diagnostic_status_sensor_ != nullptr)
      this->mic_diagnostic_status_sensor_->publish_state("ERROR: requires responsive XMOS 0.2.11+; no mic test sent");
    return;
  }
  for (auto *sensor : this->mic_level_sensors_) {
    if (sensor != nullptr)
      sensor->publish_state(NAN);
  }
  this->mic_diagnostic_active_ = true;
  this->mic_diagnostic_started_ms_ = millis();
  this->mic_diagnostic_samples_ = 0;
  for (float &sum : this->mic_diagnostic_sums_)
    sum = 0.0f;
  if (this->mic_diagnostic_status_sensor_ != nullptr)
    this->mic_diagnostic_status_sensor_->publish_state("LISTENING: speak or clap around the four microphones");
}

void VoiceDSPPlus::finish_microphone_diagnostic_() {
  this->mic_diagnostic_active_ = false;
  if (this->mic_diagnostic_status_sensor_ == nullptr)
    return;
  if (this->mic_diagnostic_samples_ < 3) {
    this->mic_diagnostic_status_sensor_->publish_state("ERROR: XMOS microphone telemetry unavailable");
    return;
  }

  float average_db[4] = {};
  float maximum_db = -120.0f;
  for (unsigned mic = 0; mic < 4; ++mic) {
    average_db[mic] = 10.0f * std::log10(std::max(
        this->mic_diagnostic_sums_[mic] / this->mic_diagnostic_samples_, 1.0e-12f));
    if (this->mic_level_sensors_[mic] != nullptr)
      this->mic_level_sensors_[mic]->publish_state(average_db[mic]);
    maximum_db = std::max(maximum_db, average_db[mic]);
  }
  if (maximum_db < -75.0f) {
    this->mic_diagnostic_status_sensor_->publish_state("INCONCLUSIVE: no useful sound detected");
    return;
  }

  char failed_mics[32] = {};
  size_t used = 0;
  float minimum_db = maximum_db;
  for (unsigned mic = 0; mic < 4; ++mic) {
    minimum_db = std::min(minimum_db, average_db[mic]);
    if (average_db[mic] < maximum_db - 12.0f) {
      used += std::snprintf(failed_mics + used, sizeof(failed_mics) - used, "%sD%u",
                            used == 0 ? "" : ", ", mic);
      if (used >= sizeof(failed_mics))
        break;
    }
  }

  char result[96] = {};
  if (used == 0) {
    std::snprintf(result, sizeof(result), "ACTIVITY: D0-D3 respond, spread %.1f dB (not a calibration)", maximum_db - minimum_db);
  } else {
    std::snprintf(result, sizeof(result), "CHECK: low signal on %s; repeat with sound near each mic", failed_mics);
  }
  this->mic_diagnostic_status_sensor_->publish_state(result);
}

bool VoiceDSPPlus::write_pp_int_(uint8_t command, int32_t value) {
  uint8_t payload[sizeof(value)];
  memcpy(payload, &value, sizeof(value));
  return this->xmos_control_write_(PP_RESID, command, payload, sizeof(payload));
}

bool VoiceDSPPlus::write_pp_float_(uint8_t command, float value) {
  static_assert(sizeof(float) == 4, "XMOS PP protocol requires IEEE-754 float32");
  uint8_t payload[sizeof(value)];
  memcpy(payload, &value, sizeof(value));
  return this->xmos_control_write_(PP_RESID, command, payload, sizeof(payload));
}

bool VoiceDSPPlus::read_pp_int_(uint8_t command, int32_t &value) {
  uint8_t payload[sizeof(value)] = {};
  if (!this->xmos_control_read_(PP_RESID, command, payload, sizeof(payload)))
    return false;
  memcpy(&value, payload, sizeof(value));
  return true;
}

bool VoiceDSPPlus::read_pp_float_(uint8_t command, float &value) {
  uint8_t payload[sizeof(value)] = {};
  if (!this->xmos_control_read_(PP_RESID, command, payload, sizeof(payload)))
    return false;
  memcpy(&value, payload, sizeof(value));
  return true;
}

bool VoiceDSPPlus::write_resource_float_(uint8_t resid, uint8_t command, float value) {
  static_assert(sizeof(float) == 4, "XMOS control protocol requires IEEE-754 float32");
  uint8_t payload[sizeof(value)];
  memcpy(payload, &value, sizeof(value));
  return this->xmos_control_write_(resid, command, payload, sizeof(payload));
}

bool VoiceDSPPlus::read_resource_float_(uint8_t resid, uint8_t command, float &value) {
  uint8_t payload[sizeof(value)] = {};
  if (!this->xmos_control_read_(resid, command, payload, sizeof(payload)))
    return false;
  memcpy(&value, payload, sizeof(value));
  return true;
}

bool VoiceDSPPlus::write_pp_float3_(uint8_t command, float first, float second, float third) {
  const float values[3] = {first, second, third};
  uint8_t payload[sizeof(values)];
  memcpy(payload, values, sizeof(values));
  return this->xmos_control_write_(PP_RESID, command, payload, sizeof(payload));
}

bool VoiceDSPPlus::read_pp_float3_(uint8_t command, float &first, float &second, float &third) {
  float values[3] = {};
  if (!this->xmos_control_read_(PP_RESID, command, reinterpret_cast<uint8_t *>(values), sizeof(values)))
    return false;
  first = values[0];
  second = values[1];
  third = values[2];
  return true;
}

bool VoiceDSPPlus::apply_pp_profile(bool echo_suppression, float min_ns, float min_nn, bool agc_enabled,
                                    float agc_max_gain, float agc_target, bool limiter_enabled,
                                    float limiter_power) {
  bool ok = this->write_pp_int_(PP_ECHOONOFF, echo_suppression ? 1 : 0);
  ok &= this->write_pp_float_(PP_MIN_NS, min_ns);
  ok &= this->write_pp_float_(PP_MIN_NN, min_nn);
  ok &= this->write_pp_int_(PP_AGCONOFF, agc_enabled ? 1 : 0);
  ok &= this->write_pp_float_(PP_AGCMAXGAIN, agc_max_gain);
  ok &= this->write_pp_float_(PP_AGCDESIREDLEVEL, agc_target);
  ok &= this->write_pp_int_(PP_LIMITONOFF, limiter_enabled ? 1 : 0);
  ok &= this->write_pp_float_(PP_LIMITPLIMIT, limiter_power);
  if (!ok) {
    ESP_LOGE(TAG, "Failed to apply the complete XMOS PP profile");
    if (this->dsp_status_sensor_ != nullptr)
      this->dsp_status_sensor_->publish_state("write failed");
    return false;
  }
  return this->refresh_pp_profile();
}

bool VoiceDSPPlus::refresh_pp_profile() {
  int32_t echo = 0;
  int32_t agc = 0;
  int32_t limiter = 0;
  float min_ns = 0.0f;
  float min_nn = 0.0f;
  float agc_max = 0.0f;
  float agc_target = 0.0f;
  float limiter_power = 0.0f;
  bool ok = this->read_pp_int_(PP_ECHOONOFF, echo);
  ok &= this->read_pp_float_(PP_MIN_NS, min_ns);
  ok &= this->read_pp_float_(PP_MIN_NN, min_nn);
  ok &= this->read_pp_int_(PP_AGCONOFF, agc);
  ok &= this->read_pp_float_(PP_AGCMAXGAIN, agc_max);
  ok &= this->read_pp_float_(PP_AGCDESIREDLEVEL, agc_target);
  ok &= this->read_pp_int_(PP_LIMITONOFF, limiter);
  ok &= this->read_pp_float_(PP_LIMITPLIMIT, limiter_power);
  if (!ok) {
    ESP_LOGE(TAG, "Failed to read the complete XMOS PP profile");
    if (this->dsp_status_sensor_ != nullptr)
      this->dsp_status_sensor_->publish_state("read failed");
    return false;
  }

  char state[192];
  snprintf(state, sizeof(state), "echo=%ld ns=%.3f nn=%.3f agc=%ld max=%.2f target=%.6f limiter=%ld power=%.3f",
           static_cast<long>(echo), min_ns, min_nn, static_cast<long>(agc), agc_max, agc_target,
           static_cast<long>(limiter), limiter_power);
  ESP_LOGI(TAG, "XMOS PP profile: %s", state);
  if (this->dsp_status_sensor_ != nullptr)
    this->dsp_status_sensor_->publish_state(state);
  return true;
}

bool VoiceDSPPlus::apply_aec_profile(float far_ext_gain, bool echo_suppression, float gamma_e,
                                     float gamma_etail, float gamma_enl, bool nl_attenuation,
                                     float mgscale_max, float mgscale_min, float mgscale_current,
                                     int32_t dt_sensitive) {
  bool ok = this->write_resource_float_(AEC_RESID, AEC_FAR_EXTGAIN, far_ext_gain);
  ok &= this->write_pp_int_(PP_ECHOONOFF, echo_suppression ? 1 : 0);
  ok &= this->write_pp_float_(PP_GAMMA_E, gamma_e);
  ok &= this->write_pp_float_(PP_GAMMA_ETAIL, gamma_etail);
  ok &= this->write_pp_float_(PP_GAMMA_ENL, gamma_enl);
  ok &= this->write_pp_int_(PP_NLATTENONOFF, nl_attenuation ? 1 : 0);
  ok &= this->write_pp_float3_(PP_MGSCALE, mgscale_max, mgscale_min, mgscale_current);
  ok &= this->write_pp_int_(PP_DTSENSITIVE, dt_sensitive);
  if (!ok) {
    ESP_LOGE(TAG, "Failed to apply the complete XMOS AEC profile");
    if (this->aec_status_sensor_ != nullptr)
      this->aec_status_sensor_->publish_state("write failed");
    return false;
  }
  return this->refresh_aec_profile();
}

bool VoiceDSPPlus::refresh_aec_profile() {
  float far_ext_gain = 0.0f;
  float gamma_e = 0.0f;
  float gamma_etail = 0.0f;
  float gamma_enl = 0.0f;
  float mgscale_max = 0.0f;
  float mgscale_min = 0.0f;
  float mgscale_current = 0.0f;
  int32_t echo = 0;
  int32_t nl_attenuation = 0;
  int32_t dt_sensitive = 0;
  bool ok = this->read_resource_float_(AEC_RESID, AEC_FAR_EXTGAIN, far_ext_gain);
  ok &= this->read_pp_int_(PP_ECHOONOFF, echo);
  ok &= this->read_pp_float_(PP_GAMMA_E, gamma_e);
  ok &= this->read_pp_float_(PP_GAMMA_ETAIL, gamma_etail);
  ok &= this->read_pp_float_(PP_GAMMA_ENL, gamma_enl);
  ok &= this->read_pp_int_(PP_NLATTENONOFF, nl_attenuation);
  ok &= this->read_pp_float3_(PP_MGSCALE, mgscale_max, mgscale_min, mgscale_current);
  ok &= this->read_pp_int_(PP_DTSENSITIVE, dt_sensitive);
  if (!ok) {
    ESP_LOGE(TAG, "Failed to read the complete XMOS AEC profile");
    if (this->aec_status_sensor_ != nullptr)
      this->aec_status_sensor_->publish_state("read failed");
    return false;
  }

  char state[192];
  snprintf(state, sizeof(state),
           "far=%.1f echo=%ld gamma=%.2f/%.2f/%.2f nl=%ld mg=%.1f/%.1f/%.1f dt=%ld",
           far_ext_gain, static_cast<long>(echo), gamma_e, gamma_etail, gamma_enl,
           static_cast<long>(nl_attenuation), mgscale_max, mgscale_min, mgscale_current,
           static_cast<long>(dt_sensitive));
  ESP_LOGI(TAG, "XMOS AEC profile: %s", state);
  if (this->aec_status_sensor_ != nullptr)
    this->aec_status_sensor_->publish_state(state);
  return true;
}

bool VoiceDSPPlus::prepare_qspi_flash() {
  // Hold XVF3800 in reset so the ESP32 owns the shared QSPI pins.
  return this->set_amp_safe(true) && this->update_pcal_bits_(P3_BOOT_SEL, false) &&
         this->update_pcal_bits_(P0_XVF_RESET_N, false);
}

bool VoiceDSPPlus::release_qspi_boot() {
  bool ok = this->update_pcal_bits_(P3_BOOT_SEL, false);
  ok &= this->update_pcal_bits_(P0_XVF_RESET_N, true);
  delay(100);
  return ok;
}

bool VoiceDSPPlus::prepare_spi_slave_boot() {
  uint8_t output = 0;
  uint8_t config = 0;
  if (!this->read_pcal_(PCAL_OUTPUT, output) || !this->read_pcal_(PCAL_CONFIG, config))
    return false;

  output = static_cast<uint8_t>((output & ~P0_XVF_RESET_N) | P3_BOOT_SEL);
  config = static_cast<uint8_t>(config & ~(P0_XVF_RESET_N | P3_BOOT_SEL));
  if (!this->write_pcal_(PCAL_OUTPUT, output) || !this->write_pcal_(PCAL_CONFIG, config))
    return false;
  delay(2);

  // Release reset through the board pull-up, matching the validated loader.
  output = static_cast<uint8_t>(output | P0_XVF_RESET_N);
  if (!this->write_pcal_(PCAL_OUTPUT, output))
    return false;
  config = static_cast<uint8_t>(config | P0_XVF_RESET_N);
  if (!this->write_pcal_(PCAL_CONFIG, config))
    return false;
  delay(2);
  return true;
}

bool VoiceDSPPlus::release_spi_boot_pins() {
  uint8_t output = 0;
  uint8_t config = 0;
  if (!this->read_pcal_(PCAL_OUTPUT, output) || !this->read_pcal_(PCAL_CONFIG, config))
    return false;
  output = static_cast<uint8_t>((output | P0_XVF_RESET_N) & ~P3_BOOT_SEL);
  if (!this->write_pcal_(PCAL_OUTPUT, output))
    return false;
  config = static_cast<uint8_t>(config | P0_XVF_RESET_N | P3_BOOT_SEL);
  return this->write_pcal_(PCAL_CONFIG, config);
}

bool VoiceDSPPlus::reset_xmos() {
  bool ok = this->update_pcal_bits_(P0_XVF_RESET_N, false);
  delay(100);
  ok &= this->update_pcal_bits_(P0_XVF_RESET_N, true);
  delay(100);
  return ok;
}

bool VoiceDSPPlus::set_amp_safe(bool safe) {
  uint8_t output = 0;
  if (!this->read_pcal_(PCAL_OUTPUT, output))
    return false;
  if (safe) {
    output = static_cast<uint8_t>(output & ~0x90U);  // P4/P7 low disables the speaker amplifier.
  } else if (!this->last_jack_) {
    output = static_cast<uint8_t>((output & ~0x10U) | 0x80U);
  }
  return this->write_pcal_(PCAL_OUTPUT, output);
}

void VoiceDSPPlus::restore_audio_route() { this->apply_route_(this->last_jack_); }

std::string VoiceDSPPlus::xmos_status_string() const {
  return this->xmos_control_responding_ ? "48 kHz AUTO; I2C control responding"
                                       : "48 kHz AUTO; waiting for I2C control";
}

void VoiceDSPPlus::dump_config() {
  ESP_LOGCONFIG(TAG, "Voice DSP+ controller:");
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  XMOS control address: 0x%02X", this->xmos_address_);
  ESP_LOGCONFIG(TAG, "  DAC address: 0x%02X", this->dac_address_);
  ESP_LOGCONFIG(TAG, "  Geometry: %s", this->last_square_ ? "SQUARE" : "LINE");
  ESP_LOGCONFIG(TAG, "  Route: %s", this->last_jack_ ? "HEADPHONE" : "SPEAKER");
}

}  // namespace voice_dsp_plus
}  // namespace esphome
