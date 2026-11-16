/* Single-owner, open-drain I2C. No blind recovery clocks or push-pull highs. */
#ifndef VOICEDSP_V2_I2C_H
#define VOICEDSP_V2_I2C_H
#include <stdbool.h>
#include <stdint.h>
#include "voicedsp_v2_attributes.h"

enum { VDSP_SCL = 0, VDSP_SDA = 1 };
enum { VDSP_I2C_OK = 0, VDSP_I2C_ARGUMENT = -1, VDSP_I2C_BUSY = -2,
       VDSP_I2C_TIMEOUT = -3, VDSP_I2C_NACK = -4, VDSP_I2C_COLLISION = -5 };
typedef struct {
    void *context;
    VDSP_FPTR("vdsp_bus_output") void (*drive_low)(void *, unsigned);
    VDSP_FPTR("vdsp_bus_output") void (*release)(void *, unsigned);
    VDSP_FPTR("vdsp_bus_input") bool (*level)(void *, unsigned);
    VDSP_FPTR("vdsp_bus_delay") void (*half_period)(void *);
    unsigned stretch_limit;
} vdsp_v2_i2c;

/* Caller serializes the entire transaction and any page-select sequences.
 * Collision detection is defensive, NOT a multi-master arbitration service.
 */
int vdsp_v2_i2c_write(vdsp_v2_i2c *, uint8_t address, uint8_t reg, uint8_t value);
int vdsp_v2_i2c_read(vdsp_v2_i2c *, uint8_t address, uint8_t reg, uint8_t *value);
#endif
