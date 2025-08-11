#pragma once

// 最大 EQ 段数定义
#define EQ_BANDS 12
#define MAX_EQ_BANDS EQ_BANDS

#pragma pack(push, 1)

// =========================================================================
// EQ 系数结构体（b0, b1, b2, a1, a2）
// =========================================================================
typedef struct _EQBandCoeffs
{
    int b0, b1, b2;
    int a1, a2;
} EQBandCoeffs;

// =========================================================================
// EQ 状态结构体（x1, x2, y1, y2）：用于每个声道保存滤波状态
// =========================================================================
typedef struct _EQBandRuntime
{
    int x1, x2;
    int y1, y2;
} EQBandRuntime;

// =========================================================================
// EQ 参数结构体：用于 IOCTL 接收一整组系数（可选）
// =========================================================================
typedef struct _EQCoeffParams
{
    int BandCount;
    EQBandCoeffs Bands[EQ_BANDS];
} EQCoeffParams;

#pragma pack(pop)

// =========================================================================
// 外部变量声明（在 .cpp 中定义）
// =========================================================================
#ifdef __cplusplus
extern "C" {
#endif

extern EQBandCoeffs   g_EqBands[EQ_BANDS];          // 每段 EQ 的系数
extern EQBandRuntime  g_EqStates[EQ_BANDS][2];      // 每段 EQ 的左右声道状态

#ifdef __cplusplus
}
#endif
