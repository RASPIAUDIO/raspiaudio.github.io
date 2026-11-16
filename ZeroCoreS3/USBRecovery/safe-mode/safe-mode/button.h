#ifndef VDSP_SAFE_BUTTON_H
#define VDSP_SAFE_BUTTON_H
#include "voicedsp_v2_i2c.h"

typedef void (*vdsp_boot_wait)(unsigned milliseconds);

static int vdsp_boot_register(vdsp_v2_i2c *b, unsigned reg, unsigned set, unsigned clear)
{
    uint8_t value;
    if (vdsp_v2_i2c_read(b, 0x20, reg, &value)) return -1;
    return vdsp_v2_i2c_write(b, 0x20, reg, (value | set) & ~clear);
}

/* Never touch port 0: it contains XMOS reset, boot selection and amp enable.
 * Unreadable buttons select recovery rather than prevent boot indefinitely. */
static int vdsp_boot_recovery(vdsp_v2_i2c *b, vdsp_boot_wait wait_ms VDSP_FPTR("vdsp_boot_wait"))
{
    unsigned pressed = 0;
    if (vdsp_boot_register(b, 0x07, 0x0f, 0) ||
        vdsp_boot_register(b, 0x05, 0, 0x0f) ||
        vdsp_boot_register(b, 0x45, 0, 0x0f) ||
        vdsp_boot_register(b, 0x49, 0x0f, 0) ||
        vdsp_boot_register(b, 0x47, 0x0f, 0)) return 1;
    wait_ms(5);
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t value;
        if (vdsp_v2_i2c_read(b, 0x20, 0x01, &value)) return 1;
        if (!(value & 0x02)) ++pressed; /* P1.1 = DOWN, active low. */
        if (i != 2) wait_ms(40);
    }
    return pressed == 3;
}
#endif
