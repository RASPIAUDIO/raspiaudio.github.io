/* XTC custom boot-selection hooks. No RTOS, flash writes or audio processing. */
#include <xs1.h>
#include <xcore/port.h>
#include <xcore/hwtimer.h>
#include "selection.h"
#include "button.h"
#include "recovery_mailbox.h"
/* One object is passed to xflash --loader. Reuse the tested open-drain driver. */
#include "../src/voicedsp_v2_i2c.c"

static vdsp_boot_selection selected;
static unsigned resume_application;
static port_t pin(unsigned n) { return n == VDSP_SCL ? XS1_PORT_1N : XS1_PORT_1O; }
static void low_pin(void *unused, unsigned n) { (void)unused; port_out(pin(n), 0); }
static void release_pin(void *unused, unsigned n) { (void)unused; (void)port_in(pin(n)); }
static bool level_pin(void *unused, unsigned n) { (void)unused; return port_peek(pin(n)) != 0; }
static void ticks(unsigned count)
{
    unsigned start = get_reference_time();
    while ((unsigned)(get_reference_time() - start) < count) {}
}
static void half_period(void *unused) { (void)unused; ticks(500); }
static void wait_ms(unsigned ms) { ticks(ms * 100000u); }

void init(void)
{
#if VDSP_BOOT_USB_WINDOW
    resume_application=vdsp_mailbox_take((volatile uint32_t*)VDSP_RECOVERY_MAILBOX_ADDRESS)==VDSP_RECOVERY_APPLICATION;
    /* Scan/CRC-check both images, including the upgrade, without I2C. */
    vdsp_boot_init(&selected,0);
#elif VDSP_BOOT_USB_ALWAYS
    /* Isolate the QSPI loader from expander/button initialization. */
    vdsp_boot_init(&selected, 1);
#else
    vdsp_v2_i2c bus = {0};
    port_enable(XS1_PORT_1N);
    port_enable(XS1_PORT_1O);
    bus.drive_low = low_pin;
    bus.release = release_pin;
    bus.level = level_pin;
    bus.half_period = half_period;
    bus.stretch_limit = 200; /* At most 1 ms per stretched edge. */
    wait_ms(20);
    vdsp_boot_init(&selected, vdsp_boot_recovery(&bus, wait_ms));
    release_pin(0, VDSP_SCL);
    release_pin(0, VDSP_SDA);
    port_disable(XS1_PORT_1N);
    port_disable(XS1_PORT_1O);
#endif
}

int checkCandidateImageVersion(int version) { return vdsp_boot_check(&selected, version); }
void recordCandidateImage(int version, unsigned address) { vdsp_boot_record(&selected, version, address); }
unsigned reportSelectedImage(void) {
#if VDSP_BOOT_USB_WINDOW
    return vdsp_recovery_select(selected.factory,selected.application,resume_application,
                               (volatile uint32_t*)VDSP_RECOVERY_MAILBOX_ADDRESS);
#else
    return vdsp_boot_address(&selected);
#endif
}
