#ifndef VDSP_SAFE_SELECTION_H
#define VDSP_SAFE_SELECTION_H

typedef struct {
    unsigned factory, application;
    int version, recovery;
} vdsp_boot_selection;

static inline void vdsp_boot_init(vdsp_boot_selection *s, int recovery)
{
    s->factory = s->application = 0;
    s->version = -1;
    s->recovery = recovery;
}

static inline int vdsp_boot_check(const vdsp_boot_selection *s, int version)
{
    return version == 0 || (!s->recovery && version > s->version);
}

/* Called ONLY after XTC has verified the image CRC. */
static inline void vdsp_boot_record(vdsp_boot_selection *s, int version, unsigned address)
{
    if (!address) return;
    if (version == 0) s->factory = address;
    else if (!s->recovery && version > s->version) {
        s->application = address;
        s->version = version;
    }
}

static inline unsigned vdsp_boot_address(const vdsp_boot_selection *s)
{
    return s->recovery || !s->application ? s->factory : s->application;
}
#endif
