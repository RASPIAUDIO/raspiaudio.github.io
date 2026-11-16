#include <platform.h>
extern void recovery_writer(void);
static void idle_tile(void) { timer t; unsigned now; t :> now;
    while(1){now+=100000000; t when timerafter(now) :> void;} }
int main(void) { par { on tile[0]: recovery_writer(); on tile[1]: idle_tile(); } return 0; }
