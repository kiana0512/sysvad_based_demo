// AudioEQTypes.h
#pragma once

// 最大 EQ 段数（你可以后续支持 5/10/12）
#define MAX_EQ_BANDS 12

#pragma pack(push, 1)
typedef struct _EQBandParam {
    int FrequencyHz;
    float GainDb;
    float Q;
} EQBandParam;

typedef struct _EQControlParams {
    EQBandParam Bands[12];
    int BandCount;
} EQControlParams;
#pragma pack(pop)
