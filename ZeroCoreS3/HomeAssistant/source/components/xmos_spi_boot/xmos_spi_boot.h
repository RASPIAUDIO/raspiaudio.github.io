#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esphome/components/i2c/i2c.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace xmos_spi_boot {

class XMOSSPIBoot
    : public Component,
      public i2c::I2CDevice,
      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                            spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_5MHZ> {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

  void set_image(const uint8_t *image, size_t size) {
    this->image_ = image;
    this->image_size_ = size;
  }
  void set_expected_sha256(const std::string &sha256) { this->expected_sha256_ = sha256; }
  void set_expected_version(uint8_t major, uint8_t minor, uint8_t patch) {
    this->expected_version_[0] = major;
    this->expected_version_[1] = minor;
    this->expected_version_[2] = patch;
  }
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }
  void set_xmos_address(uint8_t address) { this->xmos_address_ = address; }
  void set_transfer_block_num(size_t value) { this->transfer_block_num_ = value; }
  void set_usb_ram_test(bool value) { this->usb_ram_test_ = value; }

 protected:
  static constexpr uint8_t PCAL_OUTPUT = 0x01;
  static constexpr uint8_t PCAL_CONFIG = 0x03;
  static constexpr uint8_t P0_XVF_RESET_N = 0x01;
  static constexpr uint8_t P3_BOOT_SEL = 0x08;
  static constexpr size_t SPI_BLOCK_SIZE = 4096;
  static constexpr uint8_t DFU_RESOURCE = 0xF0;
  static constexpr uint8_t DFU_GETVERSION = 88;
  static constexpr uint8_t READ_BIT = 0x80;

  bool read_pcal_(uint8_t reg, uint8_t &value);
  bool write_pcal_(uint8_t reg, uint8_t value);
  bool boot_qspi_();
  bool prepare_spi_boot_();
  bool release_boot_pins_();
  bool transfer_image_();
  bool probe_xmos_();
  bool host_read_(uint8_t resource, uint8_t command, uint8_t *payload, size_t payload_len);
  bool read_version_(uint8_t version[3]);
  bool version_matches_(const uint8_t version[3]) const;
  void publish_status_(const std::string &status);
  static uint8_t reverse_bits_(uint8_t value);

  const uint8_t *image_{nullptr};
  size_t image_size_{0};
  std::string expected_sha256_;
  uint8_t expected_version_[3]{0, 0, 0};
  text_sensor::TextSensor *status_sensor_{nullptr};
  uint8_t xmos_address_{0x2C};
  size_t transfer_block_num_{106};
  bool boot_passed_{false};
  bool usb_ram_test_{false};
};

}  // namespace xmos_spi_boot
}  // namespace esphome
