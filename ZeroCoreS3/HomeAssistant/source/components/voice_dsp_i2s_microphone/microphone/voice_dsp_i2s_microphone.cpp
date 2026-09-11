#include "voice_dsp_i2s_microphone.h"

#ifdef USE_ESP32

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace i2s_audio {

static const size_t RING_BUFFER_LENGTH = 60;  // Measured in milliseconds
static const size_t QUEUE_LENGTH = 10;

static const UBaseType_t MAX_LISTENERS = 16;

static const uint32_t READ_DURATION_MS = 16;

static const size_t TASK_STACK_SIZE = 4096;
static const ssize_t TASK_PRIORITY = 17;

// Use an exponential moving average to correct a DC offset with weight factor 1/1000
static const int32_t DC_OFFSET_MOVING_AVERAGE_COEFFICIENT_DENOMINATOR = 1000;

static const char *const TAG = "i2s_audio.voice_dsp_microphone";

enum MicrophoneEventGroupBits : uint32_t {
  COMMAND_STOP = (1 << 0),  // stops the microphone task, set and cleared by ``loop``

  TASK_STARTING = (1 << 10),  // set by mic task, cleared by ``loop``
  TASK_RUNNING = (1 << 11),   // set by mic task, cleared by ``loop``
  TASK_STOPPED = (1 << 13),   // set by mic task, cleared by ``loop``

  ALL_BITS = 0x00FFFFFF,  // All valid FreeRTOS event group bits
};

void VoiceDSPI2SMicrophone::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Voice DSP+ I2S microphone...");
  if (this->pdm_) {
    ESP_LOGE(TAG, "PDM not supported for SAT1 integration!");
    this->mark_failed();
    return;
  }

  this->active_listeners_semaphore_ = xSemaphoreCreateCounting(MAX_LISTENERS, MAX_LISTENERS);
  if (this->active_listeners_semaphore_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create semaphore");
    this->mark_failed();
    return;
  }

  this->event_group_ = xEventGroupCreate();
  if (this->event_group_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create event group");
    this->mark_failed();
    return;
  }

  this->configure_stream_settings_();
}

void VoiceDSPI2SMicrophone::start() {
  if (this->is_failed())
    return;

  xSemaphoreTake(this->active_listeners_semaphore_, 0);
}

void VoiceDSPI2SMicrophone::stop() {
  if (this->state_ == microphone::STATE_STOPPED || this->is_failed())
    return;

  xSemaphoreGive(this->active_listeners_semaphore_);
}

void VoiceDSPI2SMicrophone::loop() {
  uint32_t event_group_bits = xEventGroupGetBits(this->event_group_);

  if (event_group_bits & MicrophoneEventGroupBits::TASK_STARTING) {
    ESP_LOGD(TAG, "Task started, attempting to allocate buffer");
    xEventGroupClearBits(this->event_group_, MicrophoneEventGroupBits::TASK_STARTING);
  }

  if (event_group_bits & MicrophoneEventGroupBits::TASK_RUNNING) {
    ESP_LOGD(TAG, "Task is running and reading data");

    xEventGroupClearBits(this->event_group_, MicrophoneEventGroupBits::TASK_RUNNING);
    this->state_ = microphone::STATE_RUNNING;
  }

  if ((event_group_bits & MicrophoneEventGroupBits::TASK_STOPPED)) {
    ESP_LOGD(TAG, "Task finished, freeing resources and uninstalling I2S driver");

    vTaskDelete(this->task_handle_);
    this->task_handle_ = nullptr;
    this->stop_driver_();
    xEventGroupClearBits(this->event_group_, ALL_BITS);
    this->status_clear_error();

    this->state_ = microphone::STATE_STOPPED;
  }

  // Start the microphone if any semaphores are taken
  if ((uxSemaphoreGetCount(this->active_listeners_semaphore_) < MAX_LISTENERS) &&
      (this->state_ == microphone::STATE_STOPPED)) {
    this->state_ = microphone::STATE_STARTING;
  }

  // Stop the microphone if all semaphores are returned
  if ((uxSemaphoreGetCount(this->active_listeners_semaphore_) == MAX_LISTENERS) &&
      (this->state_ == microphone::STATE_RUNNING)) {
    this->state_ = microphone::STATE_STOPPING;
  }

  switch (this->state_) {
    case microphone::STATE_STARTING:
      if (this->status_has_error()) {
        break;
      }

      if (!this->start_driver_()) {
        this->status_momentary_error("I2S driver failed to start, unloading it and attempting again in 1 second", 1000);
        this->stop_driver_();  // Stop/frees whatever possibly started
        break;
      }

      if (this->task_handle_ == nullptr) {
        xTaskCreate(VoiceDSPI2SMicrophone::mic_task, "voice_dsp_mic", TASK_STACK_SIZE, (void *) this, TASK_PRIORITY,
                    &this->task_handle_);

        if (this->task_handle_ == nullptr) {
          this->status_momentary_error("Task failed to start, attempting again in 1 second", 1000);
          this->stop_driver_();  // Stops the driver to return the lock; will be reloaded in next attempt
        }
      }

      break;
    case microphone::STATE_RUNNING:
      break;
    case microphone::STATE_STOPPING:
      xEventGroupSetBits(this->event_group_, MicrophoneEventGroupBits::COMMAND_STOP);
      break;
    case microphone::STATE_STOPPED:
      break;
  }
}

void VoiceDSPI2SMicrophone::configure_stream_settings_() {
  uint8_t channel_count = this->num_of_channels();
  uint8_t bits_per_sample = 32;
  if (this->slot_bit_width_ != I2S_SLOT_BIT_WIDTH_AUTO) {
    bits_per_sample = this->slot_bit_width_;
  }

  if (this->slot_mode_ == I2S_SLOT_MODE_STEREO) {
    channel_count = 2;
  }
  // report 16kHz sample rate, as the 48kHz i2s samples will be subsampled to 16kHz
  this->audio_stream_info_ = audio::AudioStreamInfo(bits_per_sample, channel_count, 16000);
}

bool VoiceDSPI2SMicrophone::start_driver_() {
  if (!this->start_i2s_channel_()) {
    ESP_LOGE(TAG, "Failed to start I2S channel");
    return false;
  }
  this->configure_stream_settings_();  // redetermine the settings in case some settings were changed after compilation
  return true;
}

bool VoiceDSPI2SMicrophone::stop_driver_() { return this->stop_i2s_channel_(); }

size_t VoiceDSPI2SMicrophone::read_(uint8_t *buf, size_t len, TickType_t ticks_to_wait) {
  size_t bytes_read = 0;
  // i2s_channel_read expects the timeout value in ms, not ticks
  esp_err_t err = i2s_channel_read(this->parent_->get_rx_handle(), buf, len, &bytes_read, pdTICKS_TO_MS(ticks_to_wait));
  if ((err != ESP_OK) && ((err != ESP_ERR_TIMEOUT) || (ticks_to_wait != 0))) {
    // Ignore ESP_ERR_TIMEOUT if ticks_to_wait = 0, as it will read the data on the next call
    if (!this->status_has_warning()) {
      // Avoid spamming the logs with the error message if its repeated
      ESP_LOGW(TAG, "Error reading from I2S microphone: %s", esp_err_to_name(err));
    }
    this->status_set_warning();
    return 0;
  }
  if ((bytes_read == 0) && (ticks_to_wait > 0)) {
    this->status_set_warning();
    return 0;
  }
  this->status_clear_warning();

  return bytes_read;
}

void VoiceDSPI2SMicrophone::fix_dc_offset_(std::vector<uint8_t> &data) {
  const size_t bytes_per_sample = this->audio_stream_info_.samples_to_bytes(1);
  const uint32_t total_samples = this->audio_stream_info_.bytes_to_samples(data.size());

  if (total_samples == 0) {
    return;
  }

  int64_t offset_accumulator = 0;
  for (uint32_t sample_index = 0; sample_index < total_samples; ++sample_index) {
    const uint32_t byte_index = sample_index * bytes_per_sample;
    int32_t sample = audio::unpack_audio_sample_to_q31(&data[byte_index], bytes_per_sample);
    offset_accumulator += sample;
    sample -= this->dc_offset_;
    audio::pack_q31_as_audio_sample(sample, &data[byte_index], bytes_per_sample);
  }

  const int32_t new_offset = offset_accumulator / total_samples;
  this->dc_offset_ = new_offset / DC_OFFSET_MOVING_AVERAGE_COEFFICIENT_DENOMINATOR +
                     (DC_OFFSET_MOVING_AVERAGE_COEFFICIENT_DENOMINATOR - 1) * this->dc_offset_ /
                         DC_OFFSET_MOVING_AVERAGE_COEFFICIENT_DENOMINATOR;
}

void VoiceDSPI2SMicrophone::mic_task(void *params) {
  VoiceDSPI2SMicrophone *this_microphone = (VoiceDSPI2SMicrophone *) params;
  xEventGroupSetBits(this_microphone->event_group_, MicrophoneEventGroupBits::TASK_STARTING);

  {  // Ensures the samples vector is freed when the task stops
    // read 3 times the amount of bytes as we need to subsample from 48 kHz to 16 kHz
    const size_t bytes_to_read = 3 * this_microphone->audio_stream_info_.ms_to_bytes(READ_DURATION_MS);
    std::vector<uint8_t> samples;
    samples.reserve(bytes_to_read);
    uint32_t report_started_ms = millis();
    uint32_t report_callbacks = 0;
    size_t report_bytes = 0;
    uint32_t report_peak[2] = {0, 0};

    xEventGroupSetBits(this_microphone->event_group_, MicrophoneEventGroupBits::TASK_RUNNING);
    while (!(xEventGroupGetBits(this_microphone->event_group_) & MicrophoneEventGroupBits::COMMAND_STOP)) {
      if (this_microphone->data_callbacks_.size() > 0) {
        samples.resize(bytes_to_read);
        size_t bytes_read = this_microphone->read_(samples.data(), bytes_to_read, 2 * pdMS_TO_TICKS(READ_DURATION_MS));
        if (bytes_read == 0) {
          samples.clear();
          continue;
        }
        size_t samples_read = bytes_read / sizeof(int32_t);
        int32_t *samples_32 = reinterpret_cast<int32_t *>(samples.data());
        const size_t channel_count = this_microphone->audio_stream_info_.get_channels();
        const size_t frames_read = channel_count == 0 ? 0 : samples_read / channel_count;
        size_t output_samples = 0;
        for (size_t frame = 0; frame < frames_read; frame += 3) {
          for (size_t channel = 0; channel < channel_count; ++channel) {
            const int32_t sample = samples_32[frame * channel_count + channel];
            samples_32[output_samples++] = sample;
            if (channel < 2) {
              const uint32_t magnitude = sample == INT32_MIN ? UINT32_MAX : static_cast<uint32_t>(abs(sample));
              report_peak[channel] = std::max(report_peak[channel], magnitude);
            }
          }
        }
        samples.resize(output_samples * sizeof(int32_t));
        if (this_microphone->correct_dc_offset_) {
          this_microphone->fix_dc_offset_(samples);
        }
        this_microphone->data_callbacks_.call(samples);
        report_callbacks++;
        report_bytes += samples.size();

        const uint32_t now = millis();
        const uint32_t elapsed_ms = now - report_started_ms;
        if (elapsed_ms >= 2000) {
          const uint32_t bytes_per_second = static_cast<uint32_t>((report_bytes * 1000U) / elapsed_ms);
          ESP_LOGI(TAG, "I2S microphone: %u callbacks, %u B/s, peak L=%u R=%u",
                   static_cast<unsigned>(report_callbacks), static_cast<unsigned>(bytes_per_second),
                   static_cast<unsigned>(report_peak[0]), static_cast<unsigned>(report_peak[1]));
          report_started_ms = now;
          report_callbacks = 0;
          report_bytes = 0;
          report_peak[0] = 0;
          report_peak[1] = 0;
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(READ_DURATION_MS));
      }
    }
  }

  xEventGroupSetBits(this_microphone->event_group_, MicrophoneEventGroupBits::TASK_STOPPED);
  while (true) {
    // Continuously delay until the loop method deletes the task
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace i2s_audio
}  // namespace esphome

#endif  // USE_ESP32
