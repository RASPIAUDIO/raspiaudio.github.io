#ifndef VDSP_RECOVERY_MAILBOX_H
#define VDSP_RECOVERY_MAILBOX_H
#include <stdint.h>
/* Tile 0 boot-reserved RAM, above the XTC initial SP (0xfff78).
 * SDK tile_common.c documents 0xfff80 as boot-safe on tile 1; tile 0 is unused
 * by core-burn. Leave the standard DFU flag at 0xfffcc untouched.
 * Validate the application's stack/allocated sections when packaging. */
#define VDSP_RECOVERY_MAILBOX_ADDRESS 0xfff80u
#define VDSP_RECOVERY_WINDOW 0x56445731u
#define VDSP_RECOVERY_APPLICATION 0x56444131u
static inline uint32_t vdsp_mailbox_take(volatile uint32_t *p)
{
    uint32_t value=p[0], inverse=p[1];
    p[0]=0;p[1]=0;
    return inverse==~value ? value : 0;
}
static inline void vdsp_mailbox_put(volatile uint32_t *p,uint32_t value)
{ p[0]=0;p[1]=~value;p[0]=value; }
static inline unsigned vdsp_recovery_select(unsigned factory,unsigned application,
                                           unsigned resume,volatile uint32_t *p)
{
    p[0]=0;p[1]=0;
    if(resume && application)return application;
    if(application)vdsp_mailbox_put(p,VDSP_RECOVERY_WINDOW);
    return factory;
}
#endif
