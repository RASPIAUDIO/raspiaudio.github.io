#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/sha256/sha256.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/voice_dsp_plus/voice_dsp_plus.h"
#include "esphome/core/component.h"

namespace esphome {
namespace xmos_firmware_manager {

enum class ManagerState : uint8_t {
  START,
  WAIT_QSPI_BOOT,
  CHECK_VERSION,
  PREPARE_DFU,
  DFU_WAIT_IDLE,
  DFU_DOWNLOAD,
  DFU_WAIT_DOWNLOAD,
  DFU_FINISH_DOWNLOAD,
  DFU_WAIT_FINISH,
  DFU_PREPARE_VERIFY,
  DFU_UPLOAD_VERIFY,
  DFU_REBOOT,
  WAIT_FLASHED_BOOT,
  VERIFY_FLASHED_VERSION,
  FALLBACK_PREPARE,
  FALLBACK_TRANSFER,
  WAIT_FALLBACK_BOOT,
  READY,
  ERROR,
};

class XMOSFirmwareManager
    : public Component,
      public i2c::I2CDevice,
      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                            spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_5MHZ> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA - 2.0f; }

  void set_controller(voice_dsp_plus::VoiceDSPPlus *controller) { this->controller_ = controller; }
  void set_upgrade_image(const uint8_t *image, size_t size) {
    this->upgrade_image_ = image;
    this->upgrade_size_ = size;
  }
  void set_upgrade_sha256(const std::string &sha256) { this->upgrade_sha256_ = sha256; }
  void set_spi_boot_image(const uint8_t *image, size_t size) {
    this->spi_boot_image_ = image;
    this->spi_boot_size_ = size;
  }
  void set_spi_boot_sha256(const std::string &sha256) { this->spi_boot_sha256_ = sha256; }
  void set_spi_transfer_block_num(size_t block) { this->spi_transfer_block_num_ = block; }
  void set_qspi_capacity_bytes(uint32_t capacity) { this->configured_qspi_capacity_ = capacity; }
  void set_expected_version(uint8_t major, uint8_t minor, uint8_t patch) {
    this->expected_version_[0] = major;
    this->expected_version_[1] = minor;
    this->expected_version_[2] = patch;
  }
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }
  void set_progress_sensor(sensor::Sensor *sensor) { this->progress_sensor_ = sensor; }
  void set_button_sensor(binary_sensor::BinarySensor *sensor) { this->button_sensor_ = sensor; }
  void set_xmos_address(uint8_t address) { this->xmos_address_ = address; }
  void set_diagnostic_passive(bool passive) { this->diagnostic_passive_ = passive; }

  void request_reflash();
  void request_verify();
  bool set_ui_state(uint8_t state);
  bool set_ui_progress(uint8_t progress);
  bool set_mic_mute(bool muted);
  bool ready() const { return this->state_ == ManagerState::READY; }
  bool mic_muted() const { return this->mic_muted_; }

 protected:
  static constexpr size_t SPI_BOOT_BLOCK_SIZE = 4096;
  static constexpr size_t DFU_BLOCK_SIZE = 128;
  static constexpr size_t DFU_PAYLOAD_SIZE = DFU_BLOCK_SIZE + 2;
  static constexpr uint8_t APPLICATION_RESOURCE = 0x30;
  static constexpr uint8_t APPLICATION_VERSION = 0;
  static constexpr uint8_t APPLICATION_UI_STATE = 9;
  static constexpr uint8_t APPLICATION_UI_PROGRESS = 10;
  static constexpr uint8_t APPLICATION_MIC_MUTE = 11;
  static constexpr uint8_t APPLICATION_QSPI_CAPACITY = 12;
  static constexpr uint8_t IO_RESOURCE = 0x24;
  static constexpr uint8_t GPI_VALUE_ALL = 5;
  static constexpr uint8_t DFU_RESOURCE = 0xF0;
  static constexpr uint8_t DFU_DETACH = 0;
  static constexpr uint8_t DFU_DNLOAD = 1;
  static constexpr uint8_t DFU_UPLOAD = 2;
  static constexpr uint8_t DFU_GETSTATUS = 3;
  static constexpr uint8_t DFU_CLRSTATUS = 4;
  static constexpr uint8_t DFU_SETALTERNATE = 64;
  static constexpr uint8_t DFU_TRANSFERBLOCK = 65;
  static constexpr uint8_t DFU_GETVERSION = 88;
  static constexpr uint8_t DFU_ALT_UPGRADE = 1;
  static constexpr uint8_t DFU_STATE_IDLE = 2;
  static constexpr uint8_t DFU_STATE_DOWNLOAD_IDLE = 5;
  static constexpr uint8_t DFU_STATE_ERROR = 10;
  static constexpr uint8_t READ_BIT = 0x80;

  void transition_(ManagerState state, const std::string &status);
  void publish_progress_(uint8_t progress);
  void fail_dfu_(const std::string &reason);
  void finish_ready_(const std::string &status);
  bool host_write_(uint8_t resource, uint8_t command, const uint8_t *payload, size_t payload_len);
  bool host_read_(uint8_t resource, uint8_t command, uint8_t *payload, size_t payload_len);
  bool read_version_(uint8_t version[3]);
  bool read_qspi_capacity_(uint32_t &capacity);
  bool version_matches_(const uint8_t version[3]) const;
  bool probe_host_control_();
  void poll_button_();

  bool dfu_get_status_(uint8_t &status, uint8_t &state, uint32_t &timeout_ms);
  bool dfu_set_upgrade_alternate_();
  bool dfu_set_transfer_block_(uint16_t block);
  bool dfu_send_download_(const uint8_t *data, size_t length);
  bool dfu_read_upload_(uint8_t *data, size_t &length);
  bool dfu_clear_status_();
  bool dfu_detach_();
  bool transfer_spi_boot_image_();
  static uint8_t reverse_bits_(uint8_t value);

  voice_dsp_plus::VoiceDSPPlus *controller_{nullptr};
  text_sensor::TextSensor *status_sensor_{nullptr};
  sensor::Sensor *progress_sensor_{nullptr};
  binary_sensor::BinarySensor *button_sensor_{nullptr};
  const uint8_t *upgrade_image_{nullptr};
  size_t upgrade_size_{0};
  std::string upgrade_sha256_;
  const uint8_t *spi_boot_image_{nullptr};
  size_t spi_boot_size_{0};
  size_t spi_transfer_block_num_{0};
  std::string spi_boot_sha256_;
  uint8_t expected_version_[3]{0, 0, 0};
  uint8_t xmos_address_{0x2C};
  ManagerState state_{ManagerState::START};
  uint32_t state_started_ms_{0};
  uint32_t dfu_poll_after_ms_{0};
  uint32_t flash_capacity_{0};
  uint32_t configured_qspi_capacity_{0};
  size_t dfu_offset_{0};
  size_t dfu_extra_erased_bytes_{0};
  size_t spi_boot_block_{0};
  bool force_reflash_{false};
  bool verify_only_{false};
  bool spi_active_{false};
  bool fallback_active_{false};
  bool dfu_attempted_{false};
  bool mic_muted_{false};
  bool diagnostic_passive_{false};
  bool fallback_version_logged_{false};
  bool button_initialized_{false};
  bool button_raw_{false};
  bool button_stable_{false};
  bool button_armed_{false};
  uint8_t last_progress_{0xFF};
  uint32_t button_changed_ms_{0};
  uint32_t last_button_poll_ms_{0};
  uint32_t last_version_probe_ms_{0};
  sha256::SHA256 verify_hasher_;
};

}  // namespace xmos_firmware_manager
}  // namespace esphome
