#include "xmos_firmware_manager.h"

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace xmos_firmware_manager {

static const char *const TAG = "xmos_firmware_manager";
static constexpr size_t MAX_DFU_ERASED_TAIL = 4096;

void XMOSFirmwareManager::transition_(ManagerState state, const std::string &status) {
  this->state_ = state;
  this->state_started_ms_ = millis();
  if (this->status_sensor_ != nullptr)
    this->status_sensor_->publish_state(status);
  ESP_LOGI(TAG, "%s", status.c_str());
}

void XMOSFirmwareManager::publish_progress_(uint8_t progress) {
  progress = std::min<uint8_t>(progress, 100);
  if (progress == this->last_progress_)
    return;
  this->last_progress_ = progress;
  if (progress % 10 == 0 || progress == 100)
    ESP_LOGI(TAG, "XMOS persistent update progress: %u%%", progress);
  if (this->progress_sensor_ != nullptr)
    this->progress_sensor_->publish_state(progress);
  if (this->probe_host_control_())
    this->set_ui_progress(progress);
}

bool XMOSFirmwareManager::host_write_(uint8_t resource, uint8_t command, const uint8_t *payload,
                                      size_t payload_len) {
  if (payload_len > DFU_PAYLOAD_SIZE)
    return false;
  uint8_t packet[DFU_PAYLOAD_SIZE + 3]{0};
  packet[0] = resource;
  packet[1] = static_cast<uint8_t>(command & ~READ_BIT);
  packet[2] = static_cast<uint8_t>(payload_len);
  if (payload_len != 0)
    memcpy(packet + 3, payload, payload_len);
  if (this->bus_->write(this->xmos_address_, packet, payload_len + 3) != i2c::ERROR_OK)
    return false;
  delay(2);
  uint8_t status = 0xFF;
  return this->bus_->read(this->xmos_address_, &status, 1) == i2c::ERROR_OK && status == 0;
}

bool XMOSFirmwareManager::host_read_(uint8_t resource, uint8_t command, uint8_t *payload, size_t payload_len) {
  if (payload_len == 0 || payload_len > DFU_PAYLOAD_SIZE)
    return false;
  const size_t response_len = payload_len + 1;
  const uint8_t request[3] = {resource, static_cast<uint8_t>(command | READ_BIT),
                              static_cast<uint8_t>(response_len)};
  uint8_t response[DFU_PAYLOAD_SIZE + 1]{0};
  for (int attempt = 0; attempt < 3; ++attempt) {
    // XMOS device-control reads require a repeated START between the command
    // header and response. The requested length includes one status byte;
    // servicer.c removes it before validating the command-map payload length.
    if (this->bus_->write_readv(this->xmos_address_, request, sizeof(request), response, response_len) ==
        i2c::ERROR_OK) {
      if (response[0] != 0) {
        ESP_LOGW(TAG, "XMOS read resid=0x%02X cmd=0x%02X status=%u", resource, command,
                 response[0]);
        return false;
      }
      memcpy(payload, response + 1, payload_len);
      return true;
    }
    delay(2);
  }
  return false;
}

bool XMOSFirmwareManager::read_version_(uint8_t version[3]) {
  // DFU_GETVERSION is part of the stock XMOS control surface and exists in
  // both the validated 0.1.0 recovery image and the product firmware.
  return this->host_read_(DFU_RESOURCE, DFU_GETVERSION, version, 3);
}

bool XMOSFirmwareManager::read_qspi_capacity_(uint32_t &capacity) {
  if (this->fallback_active_ && this->configured_qspi_capacity_ != 0) {
    capacity = this->configured_qspi_capacity_;
    ESP_LOGW(TAG, "Recovery bootstrap uses configured QSPI size=%u bytes",
             static_cast<unsigned>(capacity));
    return capacity >= this->upgrade_size_;
  }
  uint8_t value[4]{0};
  if (this->host_read_(APPLICATION_RESOURCE, APPLICATION_QSPI_CAPACITY, value, sizeof(value))) {
    capacity = static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8) |
               (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
  } else if (this->configured_qspi_capacity_ != 0) {
    // The validated recovery bootstrap predates QSPI_CAPACITY. The Voice DSP+
    // production BOM fixes this device to a W25Q16, so use the declared size.
    capacity = this->configured_qspi_capacity_;
    ESP_LOGW(TAG, "Recovery bootstrap has no capacity command; using configured QSPI size=%u bytes",
             static_cast<unsigned>(capacity));
  } else {
    return false;
  }
  return capacity >= this->upgrade_size_;
}

bool XMOSFirmwareManager::version_matches_(const uint8_t version[3]) const {
  return memcmp(version, this->expected_version_, sizeof(this->expected_version_)) == 0;
}

bool XMOSFirmwareManager::probe_host_control_() {
  return this->bus_->write(this->xmos_address_, nullptr, 0) == i2c::ERROR_OK;
}

bool XMOSFirmwareManager::set_ui_state(uint8_t state) {
  if (state > 14)
    return false;
  if (this->diagnostic_passive_)
    return true;
  const bool ok = this->host_write_(APPLICATION_RESOURCE, APPLICATION_UI_STATE, &state, 1);
  ESP_LOGD(TAG, "XMOS UI state=%u: %s", state, ok ? "acknowledged" : "write failed");
  return ok;
}

bool XMOSFirmwareManager::set_ui_progress(uint8_t progress) {
  progress = std::min<uint8_t>(progress, 100);
  if (this->diagnostic_passive_)
    return true;
  return this->host_write_(APPLICATION_RESOURCE, APPLICATION_UI_PROGRESS, &progress, 1);
}

bool XMOSFirmwareManager::set_mic_mute(bool muted) {
  const uint8_t value = muted ? 1 : 0;
  if (this->diagnostic_passive_) {
    this->mic_muted_ = muted;
    return true;
  }
  if (!this->host_write_(APPLICATION_RESOURCE, APPLICATION_MIC_MUTE, &value, 1))
    return false;
  this->mic_muted_ = muted;
  return true;
}

void XMOSFirmwareManager::request_reflash() {
  if (this->diagnostic_passive_) {
    ESP_LOGW(TAG, "XMOS reflash ignored while diagnostic passive mode is active");
    return;
  }
  if (this->state_ != ManagerState::READY && this->state_ != ManagerState::ERROR) {
    ESP_LOGW(TAG, "Manual XMOS reflash ignored while manager is busy");
    return;
  }
  this->force_reflash_ = true;
  this->verify_only_ = false;
  this->dfu_attempted_ = false;
  this->transition_(ManagerState::PREPARE_DFU, "Manual XMOS QSPI repair requested");
}

bool XMOSFirmwareManager::dfu_get_status_(uint8_t &status, uint8_t &state, uint32_t &timeout_ms) {
  uint8_t payload[5]{0};
  if (!this->host_read_(DFU_RESOURCE, DFU_GETSTATUS, payload, sizeof(payload)))
    return false;
  status = payload[0];
  timeout_ms = static_cast<uint32_t>(payload[1]) | (static_cast<uint32_t>(payload[2]) << 8) |
               (static_cast<uint32_t>(payload[3]) << 16);
  state = payload[4];
  return true;
}

bool XMOSFirmwareManager::dfu_set_upgrade_alternate_() {
  const uint8_t alternate = DFU_ALT_UPGRADE;
  return this->host_write_(DFU_RESOURCE, DFU_SETALTERNATE, &alternate, 1);
}

bool XMOSFirmwareManager::dfu_set_transfer_block_(uint16_t block) {
  const uint8_t payload[2] = {static_cast<uint8_t>(block), static_cast<uint8_t>(block >> 8)};
  return this->host_write_(DFU_RESOURCE, DFU_TRANSFERBLOCK, payload, sizeof(payload));
}

bool XMOSFirmwareManager::dfu_send_download_(const uint8_t *data, size_t length) {
  if (length > DFU_BLOCK_SIZE)
    return false;
  uint8_t payload[DFU_PAYLOAD_SIZE]{0};
  payload[0] = static_cast<uint8_t>(length);
  payload[1] = static_cast<uint8_t>(length >> 8);
  if (length != 0)
    memcpy(payload + 2, data, length);
  return this->host_write_(DFU_RESOURCE, DFU_DNLOAD, payload, sizeof(payload));
}

bool XMOSFirmwareManager::dfu_read_upload_(uint8_t *data, size_t &length) {
  uint8_t payload[DFU_PAYLOAD_SIZE]{0};
  if (!this->host_read_(DFU_RESOURCE, DFU_UPLOAD, payload, sizeof(payload)))
    return false;
  length = static_cast<size_t>(payload[0]) | (static_cast<size_t>(payload[1]) << 8);
  if (length > DFU_BLOCK_SIZE)
    return false;
  if (length != 0)
    memcpy(data, payload + 2, length);
  return true;
}

bool XMOSFirmwareManager::dfu_clear_status_() {
  const uint8_t value = 0;
  return this->host_write_(DFU_RESOURCE, DFU_CLRSTATUS, &value, 1);
}

bool XMOSFirmwareManager::dfu_detach_() {
  // DETACH resets the XMOS immediately, so there may be no status byte to read.
  const uint8_t packet[4] = {DFU_RESOURCE, DFU_DETACH, 1, 0};
  return this->bus_->write(this->xmos_address_, packet, sizeof(packet)) == i2c::ERROR_OK;
}

uint8_t XMOSFirmwareManager::reverse_bits_(uint8_t value) {
  value = static_cast<uint8_t>(((value & 0xF0U) >> 4U) | ((value & 0x0FU) << 4U));
  value = static_cast<uint8_t>(((value & 0xCCU) >> 2U) | ((value & 0x33U) << 2U));
  return static_cast<uint8_t>(((value & 0xAAU) >> 1U) | ((value & 0x55U) << 1U));
}

bool XMOSFirmwareManager::transfer_spi_boot_image_() {
  if (this->spi_boot_image_ == nullptr || this->spi_boot_size_ == 0 ||
      this->spi_boot_size_ % SPI_BOOT_BLOCK_SIZE != 0)
    return false;
  auto *block = static_cast<uint8_t *>(
      heap_caps_malloc(SPI_BOOT_BLOCK_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (block == nullptr)
    return false;
  const size_t blocks = this->spi_boot_size_ / SPI_BOOT_BLOCK_SIZE;
  ESP_LOGI(TAG, "XMOS SPI-RAM boot start: %u bytes, %u blocks", static_cast<unsigned>(this->spi_boot_size_),
           static_cast<unsigned>(blocks));
  while (this->spi_boot_block_ < blocks) {
    if (this->spi_boot_block_ == 1)
      delay(1);
    if (this->spi_boot_block_ == this->spi_transfer_block_num_)
      delay(5);
    const uint8_t *source = this->spi_boot_image_ + this->spi_boot_block_ * SPI_BOOT_BLOCK_SIZE;
    for (size_t index = 0; index < SPI_BOOT_BLOCK_SIZE; ++index)
      block[index] = reverse_bits_(source[index]);
    this->enable();
    this->write_array(block, SPI_BOOT_BLOCK_SIZE);
    this->disable();
    esp_rom_delay_us(5);
    this->spi_boot_block_++;
    if (this->spi_boot_block_ % 32 == 0 || this->spi_boot_block_ == blocks) {
      ESP_LOGI(TAG, "XMOS SPI-RAM progress: %u/%u", static_cast<unsigned>(this->spi_boot_block_),
               static_cast<unsigned>(blocks));
      App.feed_wdt();
    }
  }
  heap_caps_free(block);
  return true;
}

void XMOSFirmwareManager::fail_dfu_(const std::string &reason) {
  ESP_LOGE(TAG, "%s", reason.c_str());
  this->set_ui_state(14);
  if (this->verify_only_) {
    this->controller_->set_polling_suspended(false);
    this->transition_(ManagerState::ERROR, "FAIL: read-only QSPI verification; no repair attempted");
    return;
  }
  if (this->fallback_active_) {
    this->finish_ready_("DEGRADED: SPI-RAM active; persistent XMOS repair will retry next boot");
  } else {
    this->transition_(ManagerState::FALLBACK_PREPARE, "Persistent update failed; starting SPI-RAM fallback");
  }
}

void XMOSFirmwareManager::finish_ready_(const std::string &status) {
  this->verify_only_ = false;
  this->controller_->set_polling_suspended(false);
  this->controller_->restore_audio_route();
  this->set_mic_mute(false);
  this->mic_muted_ = false;
  this->publish_progress_(100);
  this->set_ui_state(0);
  this->transition_(ManagerState::READY, status);
}

void XMOSFirmwareManager::poll_button_() {
  // BUT_A is read through XMOS host-control, not a direct ESP32 GPIO. A 10 Hz
  // poll is responsive for a human button without starving the audio/TinyML
  // tasks with continuous I2C transactions.
  if (millis() - this->last_button_poll_ms_ < 100)
    return;
  this->last_button_poll_ms_ = millis();
  uint8_t value[4]{0};
  if (!this->host_read_(IO_RESOURCE, GPI_VALUE_ALL, value, sizeof(value)))
    return;
  const uint32_t bitmap = static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8) |
                          (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
  const bool pressed = (bitmap & 0x01U) != 0;
  if (!this->button_initialized_) {
    this->button_initialized_ = true;
    this->button_raw_ = pressed;
    this->button_stable_ = false;
    this->button_armed_ = !pressed;
    this->button_changed_ms_ = millis();
    if (this->button_sensor_ != nullptr)
      this->button_sensor_->publish_state(false);
    ESP_LOGI(TAG, "User button initialized: raw=%s armed=%s", YESNO(pressed), YESNO(this->button_armed_));
    return;
  }
  if (pressed != this->button_raw_) {
    this->button_raw_ = pressed;
    this->button_changed_ms_ = millis();
  }
  if (pressed != this->button_stable_ && millis() - this->button_changed_ms_ >= 250) {
    this->button_stable_ = pressed;
    if (!pressed) {
      this->button_armed_ = true;
      ESP_LOGI(TAG, "User button released; next press armed");
      if (this->button_sensor_ != nullptr)
        this->button_sensor_->publish_state(false);
    } else if (this->button_armed_) {
      ESP_LOGI(TAG, "User button press accepted after 250 ms debounce");
      if (this->button_sensor_ != nullptr)
        this->button_sensor_->publish_state(true);
    } else {
      ESP_LOGW(TAG, "Ignoring user button held during initialization");
    }
  }
}

void XMOSFirmwareManager::request_verify() {
  if (this->diagnostic_passive_ ||
      (this->state_ != ManagerState::READY && this->state_ != ManagerState::ERROR))
    return;
  this->verify_only_ = true;
  this->controller_->set_polling_suspended(true);
  if (!this->controller_->prepare_qspi_flash() || !this->controller_->release_qspi_boot()) {
    this->controller_->set_polling_suspended(false);
    this->transition_(ManagerState::ERROR, "ERROR: cannot reboot XMOS for read-only verification");
    return;
  }
  this->fallback_active_ = false;
  this->transition_(ManagerState::WAIT_FLASHED_BOOT, "Rebooting XMOS before read-only QSPI verification");
}

void XMOSFirmwareManager::setup() {
  if (this->diagnostic_passive_) {
    this->mic_muted_ = false;
    if (this->progress_sensor_ != nullptr)
      this->progress_sensor_->publish_state(100);
    this->transition_(ManagerState::READY,
                      "DIAGNOSTIC: XMOS firmware management and button polling disabled");
    return;
  }
  this->publish_progress_(0);
  this->mic_muted_ = false;
  this->transition_(ManagerState::START, "Checking persistent XMOS firmware");
}

void XMOSFirmwareManager::loop() {
  if (this->diagnostic_passive_)
    return;
  switch (this->state_) {
    case ManagerState::START:
      this->controller_->set_polling_suspended(true);
      {
        uint8_t version[3]{0};
        if (this->read_version_(version)) {
          ESP_LOGI(TAG, "XMOS active before QSPI check: version %u.%u.%u", version[0], version[1], version[2]);
          this->fallback_active_ = true;
          if (this->version_matches_(version))
            this->finish_ready_("PASS: expected XMOS version active; no transfer required (boot source not verified)");
          else {
            // A completed download may be newer than the running application.
            // Try QSPI before needlessly erasing/programming again.
            if (!this->controller_->prepare_qspi_flash() || !this->controller_->release_qspi_boot()) {
              this->controller_->set_polling_suspended(false);
              this->transition_(ManagerState::ERROR, "ERROR: cannot restart XMOS from QSPI");
            } else {
              this->fallback_active_ = false;
              this->transition_(ManagerState::WAIT_QSPI_BOOT, "Checking installed QSPI image before updating");
            }
          }
          break;
        }
        if (this->probe_host_control_()) {
          this->fallback_active_ = true;
          this->finish_ready_("DEGRADED: XMOS SPI-RAM active; application version unreadable");
          break;
        }
      }
      if (!this->controller_->release_qspi_boot()) {
        this->controller_->set_polling_suspended(false);
        this->transition_(ManagerState::ERROR, "ERROR: I2C/PCAL unavailable; no flash attempted");
        break;
      }
      this->transition_(ManagerState::WAIT_QSPI_BOOT, "Waiting for XMOS QSPI boot");
      break;

    case ManagerState::WAIT_QSPI_BOOT:
      if (millis() - this->state_started_ms_ >= 1500)
        this->transition_(ManagerState::CHECK_VERSION, "Reading XMOS firmware version");
      break;

    case ManagerState::CHECK_VERSION: {
      uint8_t version[3]{0};
      if (this->read_version_(version)) {
        if (!this->force_reflash_ && this->version_matches_(version)) {
          this->finish_ready_("PASS: expected XMOS version active; no transfer required");
        } else {
          this->fallback_active_ = true;
          ESP_LOGW(TAG, "XMOS QSPI version mismatch: %u.%u.%u", version[0], version[1], version[2]);
          this->transition_(ManagerState::PREPARE_DFU, "XMOS QSPI update required");
        }
      } else {
        this->transition_(ManagerState::FALLBACK_PREPARE, "No QSPI firmware; starting SPI-RAM recovery");
      }
      break;
    }

    case ManagerState::PREPARE_DFU: {
      this->controller_->set_polling_suspended(true);
      this->controller_->set_amp_safe(true);
      this->set_mic_mute(true);
      this->set_ui_state(12);
      uint32_t capacity = 0;
      if (!this->read_qspi_capacity_(capacity)) {
        this->fail_dfu_("XMOS did not report a sufficient QSPI capacity");
        break;
      }
      this->flash_capacity_ = capacity;
      ESP_LOGI(TAG, "XMOS reports QSPI capacity=%u bytes", static_cast<unsigned>(capacity));
      if (!this->dfu_set_upgrade_alternate_()) {
        this->fail_dfu_("Could not select the XMOS upgrade partition");
        break;
      }
      this->dfu_attempted_ = true;
      this->dfu_poll_after_ms_ = millis();
      this->transition_(ManagerState::DFU_WAIT_IDLE, "Preparing XMOS upgrade partition over I2C DFU");
      break;
    }

    case ManagerState::DFU_WAIT_IDLE: {
      if (millis() < this->dfu_poll_after_ms_)
        break;
      uint8_t status = 0;
      uint8_t state = 0;
      uint32_t timeout = 0;
      if (!this->dfu_get_status_(status, state, timeout)) {
        this->fail_dfu_("DFU status read failed before download");
      } else if (state == DFU_STATE_ERROR || status != 0) {
        ESP_LOGE(TAG, "DFU pre-download error: status=%u state=%u timeout=%u ms", status, state,
                 static_cast<unsigned>(timeout));
        this->dfu_clear_status_();
        this->fail_dfu_("XMOS DFU entered an error before download");
      } else if (state == DFU_STATE_IDLE) {
        this->dfu_offset_ = 0;
        this->publish_progress_(0);
        this->transition_(ManagerState::DFU_DOWNLOAD, "Writing XMOS upgrade image over I2C DFU");
      } else {
        this->dfu_poll_after_ms_ = millis() + std::max<uint32_t>(timeout, 1) + 5;
      }
      break;
    }

    case ManagerState::DFU_DOWNLOAD:
      if (this->dfu_offset_ < this->upgrade_size_) {
        const size_t length = std::min(DFU_BLOCK_SIZE, this->upgrade_size_ - this->dfu_offset_);
        if (!this->dfu_send_download_(this->upgrade_image_ + this->dfu_offset_, length)) {
          this->fail_dfu_("XMOS DFU download block failed");
          break;
        }
        this->dfu_offset_ += length;
        this->publish_progress_(static_cast<uint8_t>((this->dfu_offset_ * 70U) / this->upgrade_size_));
        this->dfu_poll_after_ms_ = millis();
        this->state_ = ManagerState::DFU_WAIT_DOWNLOAD;
      } else {
        this->transition_(ManagerState::DFU_FINISH_DOWNLOAD, "Finalizing XMOS upgrade download");
      }
      break;

    case ManagerState::DFU_WAIT_DOWNLOAD: {
      if (millis() < this->dfu_poll_after_ms_)
        break;
      uint8_t status = 0;
      uint8_t state = 0;
      uint32_t timeout = 0;
      if (!this->dfu_get_status_(status, state, timeout)) {
        this->fail_dfu_("XMOS DFU status read failed during download");
      } else if (state == DFU_STATE_ERROR || status != 0) {
        ESP_LOGE(TAG, "DFU block error at offset=%u: status=%u state=%u timeout=%u ms",
                 static_cast<unsigned>(this->dfu_offset_), status, state,
                 static_cast<unsigned>(timeout));
        this->dfu_clear_status_();
          this->fail_dfu_("XMOS DFU rejected an upgrade block");
      } else if (state == DFU_STATE_DOWNLOAD_IDLE) {
        this->state_ = ManagerState::DFU_DOWNLOAD;
      } else {
        // XMOS treats a GETSTATUS received even slightly before its advertised
        // timeout as a stalled packet and enters dfuERROR. Leave margin for
        // the independent ESP32/XMOS millisecond clocks and task scheduling.
        this->dfu_poll_after_ms_ = millis() + std::max<uint32_t>(timeout, 1) + 5;
      }
      break;
    }

    case ManagerState::DFU_FINISH_DOWNLOAD:
      if (!this->dfu_send_download_(nullptr, 0)) {
        this->fail_dfu_("XMOS DFU zero-length completion block failed");
      } else {
        this->dfu_poll_after_ms_ = millis();
        this->transition_(ManagerState::DFU_WAIT_FINISH, "Waiting for XMOS upgrade manifest");
      }
      break;

    case ManagerState::DFU_WAIT_FINISH: {
      if (millis() < this->dfu_poll_after_ms_)
        break;
      uint8_t status = 0;
      uint8_t state = 0;
      uint32_t timeout = 0;
      if (!this->dfu_get_status_(status, state, timeout)) {
        this->fail_dfu_("XMOS DFU status read failed during manifest");
      } else if (state == DFU_STATE_ERROR || status != 0) {
        ESP_LOGE(TAG, "DFU manifest error: status=%u state=%u timeout=%u ms", status, state,
                 static_cast<unsigned>(timeout));
        this->dfu_clear_status_();
        this->fail_dfu_("XMOS DFU upgrade manifest failed");
      } else if (state == DFU_STATE_IDLE) {
        // rtos_dfu_image caches partition sizes at boot. Before reboot, UPLOAD
        // can truncate a larger new image at the previous image's extent.
        this->transition_(ManagerState::DFU_REBOOT, "Rebooting XMOS to refresh DFU image metadata");
      } else {
        this->dfu_poll_after_ms_ = millis() + std::max<uint32_t>(timeout, 1) + 5;
      }
      break;
    }

    case ManagerState::DFU_PREPARE_VERIFY:
      if (!this->dfu_set_upgrade_alternate_() || !this->dfu_set_transfer_block_(0)) {
        this->fail_dfu_("Could not start XMOS upgrade readback");
        break;
      }
      this->dfu_offset_ = 0;
      this->dfu_extra_erased_bytes_ = 0;
      this->verify_hasher_.init();
      this->state_ = ManagerState::DFU_UPLOAD_VERIFY;
      break;

    case ManagerState::DFU_UPLOAD_VERIFY: {
      uint8_t data[DFU_BLOCK_SIZE]{0};
      size_t length = 0;
      if (!this->dfu_read_upload_(data, length)) {
        this->fail_dfu_("XMOS factory readback failed");
        break;
      }
      if (length == 0) {
        if (this->dfu_offset_ != this->upgrade_size_) {
          const size_t erased_tail = this->upgrade_size_ - this->dfu_offset_;
          bool tail_is_erased = erased_tail <= MAX_DFU_ERASED_TAIL &&
                                (this->dfu_offset_ % DFU_BLOCK_SIZE) == 0;
          for (size_t index = this->dfu_offset_; tail_is_erased && index < this->upgrade_size_; ++index)
            tail_is_erased = this->upgrade_image_[index] == 0xFF;
          if (!tail_is_erased) {
            ESP_LOGE(TAG, "XMOS upgrade readback ended at %u/%u bytes",
                     static_cast<unsigned>(this->dfu_offset_), static_cast<unsigned>(this->upgrade_size_));
            this->fail_dfu_("XMOS upgrade readback ended before the expected size");
            break;
          }
          // XMOS omits trailing fully erased DFU blocks from UPLOAD. Reconstruct
          // the declared 0xFF padding so the full artifact hash still matches.
          this->verify_hasher_.add(this->upgrade_image_ + this->dfu_offset_, erased_tail);
          this->dfu_offset_ = this->upgrade_size_;
          ESP_LOGI(TAG, "Accepted %u-byte erased 0xFF tail omitted by XMOS DFU upload",
                   static_cast<unsigned>(erased_tail));
        }
        this->verify_hasher_.calculate();
        char actual[65]{0};
        this->verify_hasher_.get_hex(actual);
        ESP_LOGI(TAG, "Upgrade readback SHA256: %s", actual);
        if (!this->verify_hasher_.equals_hex(this->upgrade_sha256_.c_str())) {
          this->fail_dfu_("XMOS upgrade readback SHA256 mismatch");
          break;
        }
        this->publish_progress_(100);
        this->force_reflash_ = false;
        this->finish_ready_("PASS: XMOS QSPI read back, SHA256 and running version verified");
        break;
      }
      if (this->dfu_offset_ + length > this->upgrade_size_) {
        bool erased_extension = this->dfu_offset_ == this->upgrade_size_ &&
                                this->dfu_extra_erased_bytes_ + length <= MAX_DFU_ERASED_TAIL;
        for (size_t index = 0; erased_extension && index < length; ++index)
          erased_extension = data[index] == 0xFF;
        if (erased_extension) {
          this->dfu_extra_erased_bytes_ += length;
          ESP_LOGI(TAG, "Ignored %u-byte erased 0xFF block after XMOS upgrade image (%u total)",
                   static_cast<unsigned>(length),
                   static_cast<unsigned>(this->dfu_extra_erased_bytes_));
          App.feed_wdt();
          break;
        }
        ESP_LOGE(TAG, "XMOS upgrade readback exceeds image: offset=%u length=%u size=%u",
                 static_cast<unsigned>(this->dfu_offset_), static_cast<unsigned>(length),
                 static_cast<unsigned>(this->upgrade_size_));
        this->fail_dfu_("XMOS upgrade readback data mismatch");
        break;
      }
      if (memcmp(data, this->upgrade_image_ + this->dfu_offset_, length) != 0) {
        size_t mismatch = 0;
        while (mismatch < length && data[mismatch] == this->upgrade_image_[this->dfu_offset_ + mismatch])
          ++mismatch;
        const size_t absolute_offset = this->dfu_offset_ + mismatch;
        ESP_LOGE(TAG, "XMOS upgrade readback mismatch at offset=%u block_offset=%u length=%u expected=0x%02X actual=0x%02X",
                 static_cast<unsigned>(absolute_offset), static_cast<unsigned>(mismatch),
                 static_cast<unsigned>(length), this->upgrade_image_[absolute_offset], data[mismatch]);
        this->fail_dfu_("XMOS upgrade readback data mismatch");
        break;
      }
      this->verify_hasher_.add(data, length);
      this->dfu_offset_ += length;
      this->publish_progress_(static_cast<uint8_t>(70U + (this->dfu_offset_ * 29U) / this->upgrade_size_));
      App.feed_wdt();
      break;
    }

    case ManagerState::DFU_REBOOT:
      this->set_ui_state(13);
      if (!this->dfu_detach_()) {
        this->fail_dfu_("XMOS DFU reboot command failed");
      } else {
        this->fallback_active_ = false;
        this->transition_(ManagerState::WAIT_FLASHED_BOOT, "Booting updated XMOS from QSPI");
      }
      break;

    case ManagerState::WAIT_FLASHED_BOOT:
      if (millis() - this->state_started_ms_ >= 2500)
        this->transition_(ManagerState::VERIFY_FLASHED_VERSION, "Verifying updated XMOS version");
      break;

    case ManagerState::VERIFY_FLASHED_VERSION: {
      uint8_t version[3]{0};
      if (this->read_version_(version) && this->version_matches_(version)) {
        this->transition_(ManagerState::DFU_PREPARE_VERIFY, "Reading back full XMOS image after reboot");
      } else if (this->verify_only_) {
        this->fail_dfu_("Installed XMOS version differs; verification cannot proceed");
      } else {
        this->transition_(ManagerState::FALLBACK_PREPARE, "Updated QSPI did not boot; using SPI-RAM fallback");
      }
      break;
    }

    case ManagerState::FALLBACK_PREPARE:
      this->controller_->set_polling_suspended(true);
      this->controller_->set_amp_safe(true);
      if (!this->controller_->prepare_spi_slave_boot()) {
        this->transition_(ManagerState::ERROR, "FAIL: PCAL could not start SPI-RAM fallback");
        break;
      }
      this->spi_setup();
      this->spi_active_ = true;
      if (!this->spi_is_ready()) {
        this->transition_(ManagerState::ERROR, "FAIL: SPI unavailable for XMOS RAM fallback");
        break;
      }
      this->spi_boot_block_ = 0;
      this->last_version_probe_ms_ = 0;
      this->fallback_version_logged_ = false;
      this->transition_(ManagerState::FALLBACK_TRANSFER, "Loading validated XMOS SPI-RAM fallback");
      break;

    case ManagerState::FALLBACK_TRANSFER:
      if (!this->transfer_spi_boot_image_()) {
        this->transition_(ManagerState::ERROR, "FAIL: XMOS SPI-RAM transfer");
        break;
      }
      this->spi_teardown();
      this->spi_active_ = false;
      if (!this->controller_->release_spi_boot_pins()) {
        this->transition_(ManagerState::ERROR, "FAIL: PCAL could not release SPI boot pins");
        break;
      }
      this->transition_(ManagerState::WAIT_FALLBACK_BOOT, "Waiting for XMOS SPI-RAM fallback");
      break;

    case ManagerState::WAIT_FALLBACK_BOOT:
      if (millis() - this->state_started_ms_ >= 1500 && millis() - this->last_version_probe_ms_ >= 250) {
        this->last_version_probe_ms_ = millis();
        uint8_t version[3]{0};
        const bool version_read = this->read_version_(version);
        if (version_read) {
          this->fallback_active_ = true;
          ESP_LOGI(TAG, "Recovery bootstrap version %u.%u.%u is active", version[0], version[1], version[2]);
          if (this->dfu_attempted_) {
            this->finish_ready_("DEGRADED: SPI-RAM fallback active; QSPI repair will retry next boot");
          } else {
            this->transition_(ManagerState::PREPARE_DFU, "SPI-RAM recovery active; repairing QSPI through I2C DFU");
          }
        } else if (millis() - this->state_started_ms_ >= 10000) {
          this->controller_->set_polling_suspended(false);
          this->controller_->restore_audio_route();
          this->transition_(ManagerState::ERROR, "FAIL: XMOS QSPI and SPI-RAM fallback unavailable");
        }
      }
      break;

    case ManagerState::READY:
      this->poll_button_();
      break;

    case ManagerState::ERROR:
      break;
  }
}

void XMOSFirmwareManager::dump_config() {
  ESP_LOGCONFIG(TAG, "Voice DSP+ XMOS firmware manager:");
  ESP_LOGCONFIG(TAG, "  Diagnostic passive mode: %s", YESNO(this->diagnostic_passive_));
  ESP_LOGCONFIG(TAG, "  Expected version: %u.%u.%u", this->expected_version_[0], this->expected_version_[1],
                this->expected_version_[2]);
  ESP_LOGCONFIG(TAG, "  Upgrade image: %u bytes, SHA256 %s", static_cast<unsigned>(this->upgrade_size_),
                this->upgrade_sha256_.c_str());
  ESP_LOGCONFIG(TAG, "  SPI-RAM fallback: %u bytes, SHA256 %s", static_cast<unsigned>(this->spi_boot_size_),
                this->spi_boot_sha256_.c_str());
  ESP_LOGCONFIG(TAG, "  SPI tile transition block: %u", static_cast<unsigned>(this->spi_transfer_block_num_));
  ESP_LOGCONFIG(TAG, "  Product QSPI capacity: %u bytes", static_cast<unsigned>(this->configured_qspi_capacity_));
  ESP_LOGCONFIG(TAG, "  Persistent update: I2C DFU 0x2C, byte readback plus erased-tail SHA256");
  ESP_LOGCONFIG(TAG, "  Recovery boot: SPI mode 0, 5000000 Hz");
}

}  // namespace xmos_firmware_manager
}  // namespace esphome
