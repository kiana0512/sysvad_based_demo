#pragma once
//
// AudioEQTypes.h —— EQ 基础类型
// 只放数据结构，不放 IOCTL/分发声明。类型来自 <ntddk.h>，不要自定义 BYTE 等。
// 

#include <ntddk.h>

#define EQ_BANDS      12
#define MAX_EQ_BANDS  EQ_BANDS

#pragma pack(push, 1)

// 单段双二阶（biquad）系数（Q15）
typedef struct _EQBandCoeffs {
    LONG b0, b1, b2;   // 前向系数
    LONG a1, a2;       // 反馈系数（a0 假定为 1.0）
} EQBandCoeffs;

// 每段每声道的历史状态
typedef struct _EQBandRuntime {
    LONG x1, x2;       // 输入延迟
    LONG y1, y2;       // 输出延迟
} EQBandRuntime;

// 一次性下发/读取的一组（最多 12 段）系数
typedef struct _EQCoeffParams {
    LONG          BandCount;          // 1..EQ_BANDS
    EQBandCoeffs  Bands[EQ_BANDS];    // 前 BandCount 个有效
} EQCoeffParams;

#pragma pack(pop)

// 仅供 EQ 模块实现使用（在 .cpp 中定义）
#ifdef __cplusplus
extern "C" {
#endif
extern EQBandCoeffs  g_EqBands[EQ_BANDS];
extern EQBandRuntime g_EqStates[EQ_BANDS][2];  // [band][0=L,1=R]
#ifdef __cplusplus
}
#endif
