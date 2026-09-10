#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#include <string>

namespace esphome {
namespace voice_dsp_plus {

class VoiceDSPPlus : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::IO; }

  void set_jack_sensor(binary_sensor::BinarySensor *sensor) { this->jack_sensor_ = sensor; }
  void set_square_sensor(binary_sensor::BinarySensor *sensor) { this->square_sensor_ = sensor; }
  void set_xmos_status_sensor(text_sensor::TextSensor *sensor) { this->xmos_status_sensor_ = sensor; }
  void set_dsp_status_sensor(text_sensor::TextSensor *sensor) { this->dsp_status_sensor_ = sensor; }
  void set_aec_status_sensor(text_sensor::TextSensor *sensor) { this->aec_status_sensor_ = sensor; }
  void set_doa_sensor(sensor::Sensor *sensor) { this->doa_sensor_ = sensor; }
  void set_doa_energy_sensor(sensor::Sensor *sensor) { this->doa_energy_sensor_ = sensor; }
  void set_mic_level_sensor(uint8_t index, sensor::Sensor *sensor) {
    if (index < 4)
      this->mic_level_sensors_[index] = sensor;
  }
  void set_mic_diagnostic_status_sensor(text_sensor::TextSensor *sensor) {
    this->mic_diagnostic_status_sensor_ = sensor;
  }
  void set_xmos_address(uint8_t address) { this->xmos_address_ = address; }
  void set_dac_address(uint8_t address) { this->dac_address_ = address; }

  bool prepare_qspi_flash();
  bool release_qspi_boot();
  bool prepare_spi_slave_boot();
  bool release_spi_boot_pins();
  bool reset_xmos();
  bool set_amp_safe(bool safe);
  void restore_audio_route();
  void set_polling_suspended(bool suspended) { this->polling_suspended_ = suspended; }
  bool last_jack() const { return this->last_jack_; }
  bool last_square() const { return this->last_square_; }
  bool xmos_control_responding() const { return this->xmos_control_responding_; }
  std::string xmos_status_string() const;
  bool apply_pp_profile(bool echo_suppression, float min_ns, float min_nn, bool agc_enabled,
                        float agc_max_gain, float agc_target, bool limiter_enabled, float limiter_power);
  bool refresh_pp_profile();
  bool apply_aec_profile(float far_ext_gain, bool echo_suppression, float gamma_e, float gamma_etail,
                         float gamma_enl, bool nl_attenuation, float mgscale_max, float mgscale_min,
                         float mgscale_current, int32_t dt_sensitive);
  bool refresh_aec_profile();
  bool apply_output_equalizer(float bass_db, float treble_db);
  std::string audio_output_status();
  void start_microphone_diagnostic();
  void start_doa_diagnostic();

 protected:
  static constexpr uint8_t PCAL_INPUT = 0x00;
  static constexpr uint8_t PCAL_OUTPUT = 0x01;
  static constexpr uint8_t PCAL_CONFIG = 0x03;
  static constexpr uint8_t PCAL_CONFIG_VOICE_DSP = 0x62;

  static constexpr uint8_t P0_XVF_RESET_N = 0x01;
  static constexpr uint8_t P3_BOOT_SEL = 0x08;
  static constexpr uint8_t P5_JACK = 0x20;
  static constexpr uint8_t P6_SQUARE = 0x40;
  static constexpr uint8_t OUTPUT_HEADPHONE = 0x05;
  static constexpr uint8_t OUTPUT_SPEAKER = 0x85;
  static constexpr uint8_t AEC_RESID = 0x21;
  static constexpr uint8_t AEC_FAR_EXTGAIN = 5;
  static constexpr uint8_t AEC_AZIMUTH_VALUES = 75;
  static constexpr uint8_t AEC_SPENERGY_VALUES = 80;
  static constexpr uint8_t AEC_AUTO_SELECTED_INDEX = 3;
  static constexpr uint8_t APPLICATION_RESID = 0x30;
  static constexpr uint8_t APPLICATION_MIC_RAW_LEVELS = 13;
  static constexpr uint8_t PP_RESID = 0x11;
  static constexpr uint8_t PP_AGCONOFF = 10;
  static constexpr uint8_t PP_AGCMAXGAIN = 11;
  static constexpr uint8_t PP_AGCDESIREDLEVEL = 12;
  static constexpr uint8_t PP_LIMITONOFF = 19;
  static constexpr uint8_t PP_LIMITPLIMIT = 20;
  static constexpr uint8_t PP_MIN_NS = 21;
  static constexpr uint8_t PP_MIN_NN = 22;
  static constexpr uint8_t PP_ECHOONOFF = 23;
  static constexpr uint8_t PP_GAMMA_E = 24;
  static constexpr uint8_t PP_GAMMA_ETAIL = 25;
  static constexpr uint8_t PP_GAMMA_ENL = 26;
  static constexpr uint8_t PP_NLATTENONOFF = 27;
  static constexpr uint8_t PP_MGSCALE = 29;
  static constexpr uint8_t PP_DTSENSITIVE = 31;
  static constexpr uint8_t CONTROL_READ = 0x80;
  static constexpr uint8_t CONTROL_SUCCESS = 0;
  static constexpr uint8_t CONTROL_RETRY = 64;
  static constexpr uint8_t CONTROL_QUEUE_FULL = 68;

  bool read_pcal_(uint8_t reg, uint8_t &value);
  bool write_pcal_(uint8_t reg, uint8_t value);
  bool update_pcal_bits_(uint8_t mask, bool high);
  bool read_dac_(uint8_t page, uint8_t reg, uint8_t &value);
  bool write_dac_(uint8_t page, uint8_t reg, uint8_t value);
  bool write_dac_block_(uint8_t page, uint8_t reg, const uint8_t *data, size_t data_len);
  bool apply_output_equalizer_();
  bool write_dac_volume_db_(float gain_db);
  bool configure_headphone_();
  bool configure_speaker_();
  bool publish_xmos_leds_(bool jack, bool square);
  bool send_xmos_gpo_(uint8_t pin, bool state);
  bool xmos_control_write_(uint8_t resid, uint8_t command, const uint8_t *payload, size_t payload_len);
  bool xmos_control_read_(uint8_t resid, uint8_t command, uint8_t *payload, size_t payload_len,
                          unsigned max_attempts = 1000);
  bool refresh_doa_();
  bool refresh_mic_levels_();
  void finish_microphone_diagnostic_();
  bool write_pp_int_(uint8_t command, int32_t value);
  bool write_pp_float_(uint8_t command, float value);
  bool read_pp_int_(uint8_t command, int32_t &value);
  bool read_pp_float_(uint8_t command, float &value);
  bool write_resource_float_(uint8_t resid, uint8_t command, float value);
  bool read_resource_float_(uint8_t resid, uint8_t command, float &value);
  bool write_pp_float3_(uint8_t command, float first, float second, float third);
  bool read_pp_float3_(uint8_t command, float &first, float &second, float &third);
  void apply_route_(bool jack);

  binary_sensor::BinarySensor *jack_sensor_{nullptr};
  binary_sensor::BinarySensor *square_sensor_{nullptr};
  text_sensor::TextSensor *xmos_status_sensor_{nullptr};
  text_sensor::TextSensor *dsp_status_sensor_{nullptr};
  text_sensor::TextSensor *aec_status_sensor_{nullptr};
  sensor::Sensor *doa_sensor_{nullptr};
  sensor::Sensor *doa_energy_sensor_{nullptr};
  sensor::Sensor *mic_level_sensors_[4] = {nullptr, nullptr, nullptr, nullptr};
  text_sensor::TextSensor *mic_diagnostic_status_sensor_{nullptr};
  uint8_t xmos_address_{0x2C};
  uint8_t dac_address_{0x18};
  bool state_valid_{false};
  bool last_jack_{false};
  bool last_square_{false};
  bool xmos_control_responding_{false};
  bool polling_suspended_{false};
  uint32_t last_doa_poll_ms_{0};
  bool doa_diagnostic_active_{false};
  uint32_t doa_diagnostic_started_ms_{0};
  uint32_t last_mic_level_poll_ms_{0};
  uint32_t mic_diagnostic_started_ms_{0};
  float mic_diagnostic_sums_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  uint8_t mic_diagnostic_samples_{0};
  bool mic_diagnostic_active_{false};
  float output_eq_bass_db_{0.0f};
  float output_eq_treble_db_{0.0f};
  float output_eq_digital_gain_db_{0.0f};
};

}  // namespace voice_dsp_plus
}  // namespace esphome
