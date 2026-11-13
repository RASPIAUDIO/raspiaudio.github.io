#include <Arduino.h>
#include <SPI.h>
#include "factory_pcal_boot.h"
#include <Preferences.h>
#include <esp_system.h>
#include "factory_autoboot.h"
static bool coldBoot;
static bool attemptedCold;
static uint8_t bootRegisters[8], bootRegistersValid;
static const char *bootOutcome="not_attempted";
#include "spi_images.h"

// Guarded automatic RAM boot; no recovery clocks or XMOS QSPI writes.
static constexpr int SDA_PIN = 5;
static constexpr int SCL_PIN = 6;
static bool fault;
static bool handedOff;
extern const uint8_t factory_start[] asm("_binary_images_factory_bin_start");
extern const uint8_t factory_end[] asm("_binary_images_factory_bin_end");

static void release(int pin) { pinMode(pin, INPUT); }
static void low(int pin) {
  digitalWrite(pin, LOW);
  pinMode(pin, OUTPUT_OPEN_DRAIN);
}
static void tick() { delayMicroseconds(10); }
static bool highClock() {
  release(SCL_PIN);
  uint32_t start = micros();
  while (!digitalRead(SCL_PIN)) {
    if (micros() - start > 2500) { fault = true; return false; }
  }
  tick();
  return true;
}
static void releaseBus() { release(SDA_PIN); release(SCL_PIN); }
static bool startBus(bool repeated = false) {
  release(SDA_PIN);
  if (!highClock()) return false;
  if (!digitalRead(SDA_PIN)) { fault = true; return false; }
  low(SDA_PIN); tick(); low(SCL_PIN); tick();
  (void) repeated;
  return true;
}
static void stopBus() {
  if (!fault) {
    low(SDA_PIN); tick();
    if (highClock()) { release(SDA_PIN); tick(); }
  }
  releaseBus();
}
static bool writeByte(uint8_t value) {
  for (int i = 7; i >= 0; --i) {
    if (value & (1 << i)) release(SDA_PIN); else low(SDA_PIN);
    tick(); if (!highClock()) return false;
    low(SCL_PIN); tick();
  }
  release(SDA_PIN); tick();
  if (!highClock()) return false;
  bool ack = !digitalRead(SDA_PIN);
  low(SCL_PIN); tick();
  return ack;
}
static bool readRegister(uint8_t address, uint8_t reg, uint8_t &value) {
  fault = false;
  bool ok = startBus() && writeByte(address << 1) && writeByte(reg)
      && startBus(true) && writeByte((address << 1) | 1);
  value = 0;
  if (ok) {
    release(SDA_PIN);
    for (unsigned i = 0; i < 8; ++i) {
      if (!highClock()) { ok = false; break; }
      value = (value << 1) | digitalRead(SDA_PIN);
      low(SCL_PIN); tick();
    }
    if (ok) { // NACK terminates this one-byte read.
      release(SDA_PIN);
      ok = highClock(); low(SCL_PIN); tick();
    }
  }
  stopBus();
  return ok && !fault;
}
static void scan() {
  fault = false;
  releaseBus(); tick();
  Serial.printf("BUS sda=%d scl=%d\n", digitalRead(SDA_PIN), digitalRead(SCL_PIN));
  if (!digitalRead(SDA_PIN) || !digitalRead(SCL_PIN)) {
    Serial.println("BUS_NOT_IDLE: check power/pullups/other master; no recovery attempted");
    return;
  }
  const uint8_t addresses[] = {0x18, 0x20, 0x2c, 0x73};
  for (auto address : addresses) {
    bool ack = startBus() && writeByte(address << 1);
    stopBus();
    Serial.printf("I2C 0x%02X %s\n", address, fault ? "BUS_ERROR" : ack ? "ACK" : "NACK");
    if (fault) return;
  }
}
static void pcal() {
  // Pointer selection only. No writes to configuration/output registers.
  for (uint8_t reg = 0; reg < 8; ++reg) {
    uint8_t value;
    if (!readRegister(0x20, reg, value)) {
      Serial.printf("PCAL_READ_FAILED reg=0x%02X\n", reg);
      return;
    }
    Serial.printf("PCAL reg=0x%02X value=0x%02X\n", reg, value);
  }
}
static bool writeRegister(uint8_t reg, uint8_t value) {
  fault = false;
  bool ok = startBus() && writeByte(0x20 << 1) && writeByte(reg) && writeByte(value);
  stopBus();
  return ok && !fault;
}
static uint8_t reverseBits(uint8_t v) {
  v = ((v & 0xf0) >> 4) | ((v & 0x0f) << 4);
  v = ((v & 0xcc) >> 2) | ((v & 0x33) << 2);
  return ((v & 0xaa) >> 1) | ((v & 0x55) << 1);
}
static void bootImage(const uint8_t *image, size_t size, size_t tile0block) {
  if (!size || size % 4096 || tile0block >= size / 4096) {
    Serial.println("INVALID_IMAGE"); return;
  }
  // A running USB firmware owns I2C. Never reset it by racing its transfers.
  releaseBus();
  uint32_t quiet=micros();
  while((uint32_t)(micros()-quiet)<(coldBoot?5000u:1000000u)) {
    if(!digitalRead(SDA_PIN) || !digitalRead(SCL_PIN) || (coldBoot && digitalRead(8))) {
      bootOutcome="BOOT_ABORT active_XMOS_bus_requires_handoff";Serial.println(bootOutcome);return;
    }
    delayMicroseconds(2);
  }
  uint8_t config, output, input;
  if (!readRegister(0x20, 6, config) || !readRegister(0x20, 2, output)) {
    bootOutcome="BOOT_ABORT PCAL_READ";Serial.println(bootOutcome); return;
  }
  bootRegistersValid=(1u<<2)|(1u<<6);
  bootRegisters[2]=output;bootRegisters[6]=config;
  // The warm path already observed a full second of quiet bus. Cache reads
  // here so later serial queries do not access a bus handed to the XMOS.
  if(!coldBoot)for(unsigned reg=0;reg<8;reg++) {
    if(reg==2 || reg==6)continue;
    uint8_t value;
    if(!readRegister(0x20,reg,value)) {
      bootOutcome="BOOT_ABORT PCAL_SNAPSHOT_READ";Serial.println(bootOutcome);return;
    }
    bootRegisters[reg]=value;bootRegistersValid|=1u<<reg;
  }
  if(coldBoot && (config!=0xff || output!=0xff)) {
    bootOutcome="BOOT_ABORT not_power_on_PCAL_state";Serial.println(bootOutcome);return;
  }
  // For this initial bring-up, accept only the observed power-on all-input state.
  if (!factory_pcal_boot_allowed(config, output)) {
    bootOutcome="BOOT_ABORT unrecognized_state_cold_start_required";Serial.println(bootOutcome); return;
  }
  // PCAL6416 port0: reset P0, DAC reset P2, boot P3, amp enable P4.
  // DAC reset is held low during ROM load. P7 remains an input, never AMP_CTRL2.
  output = (output & ~0x15u) | 0x08u;
  config &= ~0x1du;
  if (!writeRegister(2, output) || !writeRegister(6, config) ||
      !readRegister(0x20, 0, input) || (input & 0x91u)) {
    releaseBus(); bootOutcome="BOOT_ABORT reset_or_amp_readback";Serial.println(bootOutcome); return;
  }
  delay(5);
  SPI.begin(12, 13, 11, 10);
  digitalWrite(10, HIGH); pinMode(10, OUTPUT);
  // Release reset through its pull-up, retaining boot high long enough for ROM sampling.
  bool ok = writeRegister(6, config | 0x01u);
  delay(10);
  // Keep BOOT_SEL driven throughout ROM/loader reception. Release before the last
  // application block, so the application's I2C master cannot collide with us.
  releaseBus();
  handedOff = true;
  if (ok) {
    Serial.printf("SPI_RAM_BEGIN bytes=%u mode=0 hz=1000000\n", (unsigned)size);
    uint8_t block[4096];
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    for (size_t index = 0; index < size / 4096; ++index) {
      if (index == 1) delay(1);
      if (index == tile0block) delay(5);
      if (index + 1 == size / 4096) {
        ok = writeRegister(6, config | 0x0du);
        releaseBus();
        if (!ok) break;
      }
      for (size_t j = 0; j < sizeof(block); ++j) block[j] = reverseBits(image[index * 4096 + j]);
      digitalWrite(10, LOW);
      SPI.writeBytes(block, sizeof(block));
      digitalWrite(10, HIGH);
      delayMicroseconds(5);
      if ((index + 1) % 32 == 0) delay(1);
    }
    SPI.endTransaction();
  }
  SPI.end();
  for (int pin : {10, 11, 12, 13}) release(pin);
  bootOutcome=ok?"RAM_sent_execution_unverified":"SPI_abort";
  Serial.println(ok ? "SPI_RAM_SENT_NOT_VERIFIED QSPI_unchanged I2C_locked_until_power_cycle"
                    : "SPI_RAM_ABORT boot_pin_release_failed power_off_required");
}
static void help() {
  Serial.println("VOICE_DSP_V2_USB_RECOVERY_0.1.0");
  Serial.println("Commands: help, status, cold-status, registers, restart, rearm-idle, boot-usb. RESET loads USB firmware into RAM once; finish with the USB web updater.");

  Serial.println("Automatic SPI RAM USB recovery at startup/RESET. No external amplifier or XMOS QSPI writes.");
}
void setup() {
  for(int pin : {7,8,9,15,16})release(pin);
  releaseBus();
  Serial.begin(115200);
  esp_reset_reason_t reason=esp_reset_reason();
  bool abnormal=reason==ESP_RST_PANIC || reason==ESP_RST_INT_WDT ||
    reason==ESP_RST_TASK_WDT || reason==ESP_RST_WDT || reason==ESP_RST_BROWNOUT;
  if(abnormal)bootOutcome="AUTO_SKIPPED abnormal_reset_press_RESET";
  attemptedCold=!abnormal;
  factory_auto_start(abnormal, [](bool cold) {
    coldBoot=cold;
    bootImage(factory_start,factory_end-factory_start,factory_tile0_block);
    coldBoot=false;
  }, [](){return handedOff;}, [](){delay(500);});

}
void loop() {
  static char command[32];
  static size_t used = 0;
  static bool overflow = false;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      command[used] = 0;
      if (overflow) Serial.println("COMMAND_TOO_LONG");
      else if (!strcmp(command, "help")) help();
      else if (!strcmp(command, "status"))
        Serial.printf("READY uptime_ms=%lu heap=%u SDA=%d SCL=%d bus_activity=automatic_once_per_reset\n",
                      (unsigned long)millis(), ESP.getFreeHeap(), digitalRead(SDA_PIN), digitalRead(SCL_PIN));
      else if (!strcmp(command,"restart")) {
        Serial.println("RESTART_USB_RECOVERY");Serial.flush();delay(100);ESP.restart();
      }
      else if (!strcmp(command,"registers")) {
        Serial.printf("PCAL_BOOT_SNAPSHOT valid=0x%02X cached_not_live\n",bootRegistersValid);
        for(unsigned reg=0;reg<8;reg++)if(bootRegistersValid&(1u<<reg))
          Serial.printf("PCAL reg=0x%02X value=0x%02X\n",reg,bootRegisters[reg]);
      }
      else if (!strcmp(command,"cold-status"))
        Serial.printf("COLD attempted=%u handed_off=%u reset_reason=%u outcome=%s\n",attemptedCold,handedOff,(unsigned)esp_reset_reason(),bootOutcome);
      else if (!strcmp(command,"rearm-idle")) {
        bool idle=true;uint32_t begin=micros();
        while((uint32_t)(micros()-begin)<1000000u) {
          if(!digitalRead(SDA_PIN)||!digitalRead(SCL_PIN)){idle=false;break;}
          delayMicroseconds(2);
        }
        if(idle){handedOff=false;Serial.println("REARMED idle_bus_no_register_write");}
        else Serial.println("REARM_ABORT active_bus");
      }
      else if (handedOff && used) Serial.println("BUS_LOCKED XMOS_owns_bus power_cycle_both_to_rearm");
      else if (!strcmp(command, "boot-usb")) bootImage(factory_start, factory_end - factory_start, factory_tile0_block);
      else if (used) Serial.println("UNKNOWN_COMMAND");
      used = 0; overflow = false;
    } else if (used < sizeof(command) - 1) command[used++] = c;
    else overflow = true;
  }
  delay(2);
}
