#pragma once

// 每段 EQ 的 Q15 滤波器系数（b0-b2, a1-a2）
typedef struct _EQBandCoeffs
{
    int b0, b1, b2;
    int a1, a2;
} EQBandCoeffs;

// 所有 EQ 参数（最多 12 段）
typedef struct _EQCoeffParams
{
    int BandCount;
    EQBandCoeffs Bands[12];
} EQCoeffParams;

// 每段 EQ 状态（滤波器运行时使用）
typedef struct _EQBandState
{
    int b0, b1, b2;
    int a1, a2;
    int x1, x2;
    int y1, y2;
} EQBandState;

// 主处理接口：对 PCM 应用 EQ
void ProcessEqPcmBuffer(short* pcm, int samples);

// 设置 EQ 参数（由用户态传入）
void SetEqCoeffs(const EQCoeffParams* params);
