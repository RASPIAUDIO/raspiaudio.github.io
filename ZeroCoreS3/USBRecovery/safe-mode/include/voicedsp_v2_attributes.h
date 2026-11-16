#ifndef VOICEDSP_V2_ATTRIBUTES_H
#define VOICEDSP_V2_ATTRIBUTES_H
#if defined(__XS3A__)
#define VDSP_FPTR(group) __attribute__((fptrgroup(group)))
#else
#define VDSP_FPTR(group)
#endif
#endif
