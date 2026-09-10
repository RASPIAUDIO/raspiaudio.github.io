#include "xmos_spi_boot.h"

#include <cstring>

#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace xmos_spi_boot {

static const char *const TAG = "xmos_spi_boot";

bool XMOSSPIBoot::read_pcal_(uint8_t reg, uint8_t &value) {
  return this->read_register(reg, &value, 1) == i2c::ERROR_OK;
}

bool XMOSSPIBoot::write_pcal_(uint8_t reg, uint8_t value) {
  return this->write_register(reg, &value, 1) == i2c::ERROR_OK;
}

uint8_t XMOSSPIBoot::reverse_bits_(uint8_t value) {
  value = static_cast<uint8_t>(((value & 0xF0U) >> 4U) | ((value & 0x0FU) << 4U));
  value = static_cast<uint8_t>(((value & 0xCCU) >> 2U) | ((value & 0x33U) << 2U));
  return static_cast<uint8_t>(((value & 0xAAU) >> 1U) | ((value & 0x55U) << 1U));
}

void XMOSSPIBoot::publish_status_(const std::string &status) {
  if (this->status_sensor_ != nullptr)
    this->status_sensor_->publish_state(status);
}

bool XMOSSPIBoot::boot_qspi_() {
  uint8_t output = 0;
  uint8_t config = 0;
  if (!this->read_pcal_(PCAL_OUTPUT, output) || !this->read_pcal_(PCAL_CONFIG, config))
    return false;

  // Sample BOOT_SEL low while reset is asserted, then release both pins to
  // high impedance. All unrelated PCAL pins retain their current state.
  output = static_cast<uint8_t>(output & ~(P0_XVF_RESET_N | P3_BOOT_SEL));
  if (!this->write_pcal_(PCAL_OUTPUT, output))
    return false;
  config = static_cast<uint8_t>(config & ~(P0_XVF_RESET_N | P3_BOOT_SEL));
  if (!this->write_pcal_(PCAL_CONFIG, config))
    return false;
  delay(2);

  output = static_cast<uint8_t>(output | P0_XVF_RESET_N);
  if (!this->write_pcal_(PCAL_OUTPUT, output))
    return false;
  delay(1);
  config = static_cast<uint8_t>(config | P0_XVF_RESET_N | P3_BOOT_SEL);
  if (!this->write_pcal_(PCAL_CONFIG, config))
    return false;
  delay(1500);
  return true;
}

bool XMOSSPIBoot::prepare_spi_boot_() {
  uint8_t output = 0;
  uint8_t config = 0;
  if (!this->read_pcal_(PCAL_OUTPUT, output) || !this->read_pcal_(PCAL_CONFIG, config))
    return false;

  // Change only P0 and P3. P4/P7 and all other outputs retain their existing state.
  output = static_cast<uint8_t>((output & ~P0_XVF_RESET_N) | P3_BOOT_SEL);
  config = static_cast<uint8_t>(config & ~(P0_XVF_RESET_N | P3_BOOT_SEL));
  if (!this->write_pcal_(PCAL_OUTPUT, output) || !this->write_pcal_(PCAL_CONFIG, config))
    return false;
  delay(2);

  // Release reset through the board pull-up, matching the validated standalone loader.
  output = static_cast<uint8_t>(output | P0_XVF_RESET_N);
  if (!this->write_pcal_(PCAL_OUTPUT, output))
    return false;
  config = static_cast<uint8_t>(config | P0_XVF_RESET_N);
  if (!this->write_pcal_(PCAL_CONFIG, config))
    return false;
  delay(2);
  return true;
}

bool XMOSSPIBoot::release_boot_pins_() {
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

bool XMOSSPIBoot::transfer_image_() {
  if (this->image_ == nullptr || this->image_size_ == 0 || this->image_size_ % SPI_BLOCK_SIZE != 0)
    return false;

  auto *block = static_cast<uint8_t *>(heap_caps_malloc(SPI_BLOCK_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (block == nullptr)
    return false;

  const size_t blocks = this->image_size_ / SPI_BLOCK_SIZE;
  ESP_LOGI(TAG, "XMOS SPI boot start: %u bytes, %u blocks, mode=0, 5000000 Hz",
           static_cast<unsigned>(this->image_size_), static_cast<unsigned>(blocks));

  this->spi_setup();
  if (!this->spi_is_ready()) {
    heap_caps_free(block);
    return false;
  }

  for (size_t index = 0; index < blocks; ++index) {
    if (index == 1)
      delay(1);
    if (index == this->transfer_block_num_)
      delay(5);
    const uint8_t *source = this->image_ + index * SPI_BLOCK_SIZE;
    for (size_t byte = 0; byte < SPI_BLOCK_SIZE; ++byte)
      block[byte] = reverse_bits_(source[byte]);
    this->enable();
    this->write_array(block, SPI_BLOCK_SIZE);
    this->disable();
    esp_rom_delay_us(5);
    if ((index + 1) % 32 == 0 || index + 1 == blocks) {
      ESP_LOGI(TAG, "XMOS SPI boot progress: %u/%u", static_cast<unsigned>(index + 1),
               static_cast<unsigned>(blocks));
      App.feed_wdt();
    }
  }

  this->spi_teardown();
  heap_caps_free(block);
  return true;
}

bool XMOSSPIBoot::probe_xmos_() {
  return this->bus_->write(this->xmos_address_, nullptr, 0) == i2c::ERROR_OK;
}

bool XMOSSPIBoot::host_read_(uint8_t resource, uint8_t command, uint8_t *payload,
                             size_t payload_len) {
  if (payload_len == 0 || payload_len > 32)
    return false;
  const size_t response_len = payload_len + 1;
  const uint8_t request[3] = {resource, static_cast<uint8_t>(command | READ_BIT),
                              static_cast<uint8_t>(response_len)};
  uint8_t response[33]{0};
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (this->bus_->write_readv(this->xmos_address_, request, sizeof(request), response,
                                response_len) == i2c::ERROR_OK) {
      if (response[0] != 0)
        return false;
      memcpy(payload, response + 1, payload_len);
      return true;
    }
    delay(2);
  }
  return false;
}

bool XMOSSPIBoot::read_version_(uint8_t version[3]) {
  return this->host_read_(DFU_RESOURCE, DFU_GETVERSION, version, 3);
}

bool XMOSSPIBoot::version_matches_(const uint8_t version[3]) const {
  return memcmp(version, this->expected_version_, sizeof(this->expected_version_)) == 0;
}

void XMOSSPIBoot::setup() {
  if (this->usb_ram_test_) {
    // Lab-only configuration: no ESP32 audio, PAJ or runtime controller may
    // coexist with this mode. The USB firmware becomes the sole I2C owner.
    for (gpio_num_t pin : {GPIO_NUM_7, GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_15, GPIO_NUM_16})
      gpio_reset_pin(pin);
    uint8_t output = 0, config = 0;
    if (!this->read_pcal_(PCAL_OUTPUT, output) || !this->read_pcal_(PCAL_CONFIG, config) ||
        !this->write_pcal_(PCAL_OUTPUT, output & ~0x90U) ||
        !this->write_pcal_(PCAL_CONFIG, config & ~0x90U) ||
        !this->prepare_spi_boot_()) {
      this->publish_status_("FAIL: USB RAM test PCAL preparation");
      this->mark_failed();
      return;
    }
    const bool transferred = this->transfer_image_();
    const bool released = this->release_boot_pins_();
    // SPI CLK shares the XMOS flash clock net. Relinquish it and the I2C pins
    // instead of driving against the independently running USB firmware.
    for (gpio_num_t pin : {GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_10, GPIO_NUM_11,
                           GPIO_NUM_12, GPIO_NUM_13})
      gpio_reset_pin(pin);
    this->publish_status_(transferred && released
        ? "USB RAM image sent; verify USB identity on PC; XMOS QSPI untouched"
        : "FAIL: USB RAM transfer/release; restore ESP32 reference via OTA");
    ESP_LOGW(TAG, "USB RAM TEST sent=%d released=%d; no I2S, no further I2C; QSPI not written",
             transferred, released);
    return;
  }
  this->publish_status_("Checking persistent XMOS QSPI firmware");
  uint8_t version[3]{0};
  if (this->boot_qspi_() && this->read_version_(version)) {
    ESP_LOGI(TAG, "XMOS QSPI version: %u.%u.%u", version[0], version[1], version[2]);
    if (this->version_matches_(version)) {
      this->boot_passed_ = true;
      this->publish_status_("PASS: XMOS booted from QSPI; SPI-RAM fallback skipped");
      ESP_LOGI(TAG, "XMOS QSPI boot PASS; persistent target present, SPI transfer skipped");
      return;
    }
    ESP_LOGW(TAG, "XMOS QSPI target mismatch; expected %u.%u.%u, using recovery SPI-RAM",
             this->expected_version_[0], this->expected_version_[1], this->expected_version_[2]);
  } else {
    ESP_LOGW(TAG, "XMOS QSPI did not provide a readable version; using recovery SPI-RAM");
  }

  this->publish_status_("Booting XMOS 48 kHz AUTO over SPI");
  if (!this->prepare_spi_boot_()) {
    ESP_LOGE(TAG, "Failed to set PCAL P0/P3 for XMOS SPI boot");
    this->publish_status_("FAIL: PCAL boot control");
    this->mark_failed();
    return;
  }
  if (!this->transfer_image_()) {
    ESP_LOGE(TAG, "XMOS SPI image transfer failed");
    this->release_boot_pins_();
    this->publish_status_("FAIL: SPI image transfer");
    this->mark_failed();
    return;
  }
  if (!this->release_boot_pins_()) {
    ESP_LOGE(TAG, "Failed to release XMOS BOOT_SEL/reset pins");
    this->publish_status_("FAIL: PCAL boot release");
    this->mark_failed();
    return;
  }

  delay(1500);
  if (!this->probe_xmos_()) {
    ESP_LOGE(TAG, "XMOS host-control did not acknowledge at 0x%02X", this->xmos_address_);
    this->publish_status_("FAIL: XMOS host-control 0x2C");
    this->mark_failed();
    return;
  }

  this->boot_passed_ = true;
  this->publish_status_("PASS: 48 kHz AUTO SPI boot; I2C 0x2C responding");
  ESP_LOGI(TAG, "XMOS SPI boot PASS: host-control 0x%02X acknowledged", this->xmos_address_);
}

void XMOSSPIBoot::dump_config() {
  ESP_LOGCONFIG(TAG, "XMOS persistent QSPI boot with SPI-RAM recovery:");
  ESP_LOGCONFIG(TAG, "  Expected QSPI version: %u.%u.%u", this->expected_version_[0],
                this->expected_version_[1], this->expected_version_[2]);
  ESP_LOGCONFIG(TAG, "  Image size: %u bytes", static_cast<unsigned>(this->image_size_));
  ESP_LOGCONFIG(TAG, "  Image SHA256: %s", this->expected_sha256_.c_str());
  ESP_LOGCONFIG(TAG, "  SPI: mode 0, 5000000 Hz");
  ESP_LOGCONFIG(TAG, "  SPI transition block: %u", static_cast<unsigned>(this->transfer_block_num_));
  ESP_LOGCONFIG(TAG, "  PCAL ownership: P0 reset and P3 BOOT_SEL only");
  ESP_LOGCONFIG(TAG, "  Host-control: 0x%02X (%s)", this->xmos_address_, this->boot_passed_ ? "PASS" : "FAIL");
}

}  // namespace xmos_spi_boot
}  // namespace esphome
