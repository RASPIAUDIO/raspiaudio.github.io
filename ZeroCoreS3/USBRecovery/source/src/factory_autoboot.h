#ifndef VDSP_FACTORY_AUTOBOOT_H
#define VDSP_FACTORY_AUTOBOOT_H

// Try the power-on window, then the guarded idle-bus path for a warm reset.
// Once bus ownership was handed off, never start a second transfer.
template<class Attempt, class HandedOff, class Pause>
void factory_auto_start(bool abnormal_reset, Attempt attempt,
                        HandedOff handed_off, Pause pause) {
    if(abnormal_reset)return;
    attempt(true);
    if(handed_off())return;
    pause();
    attempt(false);
}
#endif
