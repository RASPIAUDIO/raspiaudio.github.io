#ifndef VDSP_RECOVERY_WINDOW_H
#define VDSP_RECOVERY_WINDOW_H
#include <stdbool.h>
#include <stdint.h>
#define VDSP_RECOVERY_WINDOW_MS 5000u
typedef struct {
    uint32_t start;
    bool eligible, mounted, held, switching;
} vdsp_recovery_window;
static inline void vdsp_window_init(vdsp_recovery_window *w,uint32_t now,bool eligible)
{ *w=(vdsp_recovery_window){.start=now,.eligible=eligible}; }
static inline void vdsp_window_mount(vdsp_recovery_window *w,uint32_t now)
{
    // Allow the full window after first enumeration, but never extend on resets.
    if(!w->mounted && !w->switching){w->mounted=true;w->start=now;}
}
static inline bool vdsp_window_hold(vdsp_recovery_window *w)
{
    if(w->switching)return false;
    w->held=true;return true;
}
static inline bool vdsp_window_expired(vdsp_recovery_window *w,uint32_t now)
{
    if(!w->eligible || w->held || w->switching || (uint32_t)(now-w->start)<VDSP_RECOVERY_WINDOW_MS)return false;
    w->switching=true;return true;
}
#endif
