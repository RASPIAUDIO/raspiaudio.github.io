#ifndef VDSP_FACTORY_PCAL_BOOT_H
#define VDSP_FACTORY_PCAL_BOOT_H
#include <stdint.h>

static inline bool factory_pcal_boot_allowed(uint8_t config, uint8_t output) {
    if(config==0xff)return true; // Power-on all-input state.
    if(config==0xef && (output==0xea || output==0))return true;
    // Our interrupted ROM loader: DAC reset low, BOOT high, amp disabled.
    // E2 holds XMOS reset; E3 has released it. P7 remains an input in both.
    return (config==0xe2 || config==0xe3) && output==0xea;
}
#endif
