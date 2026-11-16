#ifndef FACTORY_PROGRAM_H
#define FACTORY_PROGRAM_H
#include <stdint.h>
#include <stddef.h>
enum { FP_LOCKED, FP_READY, FP_ERASING, FP_WRITING, FP_VERIFYING, FP_VERIFIED, FP_FAILED };
void factory_program_authorize(void);
int factory_program_packet(const uint8_t *,size_t);
void factory_program_status(uint8_t out[16]);
unsigned factory_program_state(void);
void factory_program_poll(void);
#endif
