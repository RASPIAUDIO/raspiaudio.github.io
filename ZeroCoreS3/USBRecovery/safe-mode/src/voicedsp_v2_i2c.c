#include "voicedsp_v2_i2c.h"
#include <stddef.h>

static void low(vdsp_v2_i2c *b, unsigned pin) { b->drive_low(b->context, pin); }
static void release(vdsp_v2_i2c *b, unsigned pin) { b->release(b->context, pin); }
static bool level(vdsp_v2_i2c *b, unsigned pin) { return b->level(b->context, pin); }
static void delay(vdsp_v2_i2c *b) { b->half_period(b->context); }
static void release_bus(vdsp_v2_i2c *b) { release(b, VDSP_SDA); release(b, VDSP_SCL); }

static int clock_high(vdsp_v2_i2c *b)
{
    unsigned i;
    release(b, VDSP_SCL);
    for (i = 0; i < b->stretch_limit; ++i) {
        delay(b);
        if (level(b, VDSP_SCL)) return 0;
    }
    return VDSP_I2C_TIMEOUT;
}

static int start(vdsp_v2_i2c *b, bool repeated)
{
    int rc;
    release(b, VDSP_SDA);
    delay(b);
    rc = clock_high(b);
    if (rc) return rc;
    if (!level(b, VDSP_SDA)) return repeated ? VDSP_I2C_COLLISION : VDSP_I2C_BUSY;
    low(b, VDSP_SDA);
    delay(b);
    low(b, VDSP_SCL);
    delay(b);
    return 0;
}

static int stop(vdsp_v2_i2c *b)
{
    int rc;
    low(b, VDSP_SDA);
    delay(b);
    rc = clock_high(b);
    if (rc) return rc;
    release(b, VDSP_SDA);
    delay(b);
    return level(b, VDSP_SDA) ? 0 : VDSP_I2C_BUSY;
}

static int write_byte(vdsp_v2_i2c *b, uint8_t value)
{
    int bit, rc;
    for (bit = 7; bit >= 0; --bit) {
        bool one = ((value >> bit) & 1u) != 0;
        if (one) release(b, VDSP_SDA); else low(b, VDSP_SDA);
        delay(b);
        rc = clock_high(b);
        if (rc) return rc;
        if (one && !level(b, VDSP_SDA)) return VDSP_I2C_COLLISION;
        low(b, VDSP_SCL);
        delay(b);
    }
    release(b, VDSP_SDA);
    delay(b);
    rc = clock_high(b);
    if (rc) return rc;
    rc = level(b, VDSP_SDA) ? VDSP_I2C_NACK : 0;
    low(b, VDSP_SCL);
    delay(b);
    return rc;
}

static int read_byte(vdsp_v2_i2c *b, uint8_t *value)
{
    int bit, rc;
    uint8_t result = 0;
    release(b, VDSP_SDA);
    for (bit = 7; bit >= 0; --bit) {
        delay(b);
        rc = clock_high(b);
        if (rc) return rc;
        if (level(b, VDSP_SDA)) result |= (uint8_t)(1u << bit);
        low(b, VDSP_SCL);
        delay(b);
    }
    /* Single-byte read: NACK the final byte without driving SDA high. */
    rc = clock_high(b);
    if (rc) return rc;
    low(b, VDSP_SCL);
    delay(b);
    *value = result;
    return 0;
}

static int transaction(vdsp_v2_i2c *b, uint8_t addr, uint8_t reg,
                       uint8_t *value, bool reading)
{
    int rc, stop_rc;
    bool started = false;
    uint8_t read_value = 0;
    if (!b || !b->drive_low || !b->release || !b->level || !b->half_period ||
        !b->stretch_limit || addr > 0x7f || !value) return VDSP_I2C_ARGUMENT;
    rc = start(b, false);
    if (rc) goto finish;
    started = true;
    rc = write_byte(b, (uint8_t)(addr << 1));
    if (rc) goto finish;
    rc = write_byte(b, reg);
    if (rc) goto finish;
    if (reading) {
        rc = start(b, true);
        if (rc) goto finish;
        rc = write_byte(b, (uint8_t)((addr << 1) | 1u));
        if (!rc) rc = read_byte(b, &read_value);
    } else {
        rc = write_byte(b, *value);
    }
finish:
    if (started && rc != VDSP_I2C_COLLISION && rc != VDSP_I2C_TIMEOUT) {
        stop_rc = stop(b);
        if (!rc) rc = stop_rc;
    }
    release_bus(b);
    if (!rc && reading) *value = read_value;
    return rc;
}

int vdsp_v2_i2c_write(vdsp_v2_i2c *b, uint8_t addr, uint8_t reg, uint8_t value)
{
    return transaction(b, addr, reg, &value, false);
}

int vdsp_v2_i2c_read(vdsp_v2_i2c *b, uint8_t addr, uint8_t reg, uint8_t *value)
{
    return transaction(b, addr, reg, value, true);
}
