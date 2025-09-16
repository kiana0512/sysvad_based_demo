#pragma once
//
// AudioEQTypes.h —— EQ 基础类型（只放数据结构，不放 IOCTL/分发声明）
// 来自 <ntddk.h> 的类型，严禁自定义 BYTE 等以免冲突。
// 结构按 1 字节对齐，确保用户态/内核态一致。
// 

#include <ntddk.h>

#define EQ_BANDS      12
#define MAX_EQ_BANDS  EQ_BANDS

#pragma pack(push, 1)

// 单段双二阶（biquad）系数（Q15）
typedef struct _EQBandCoeffs {
    LONG b0, b1, b2;   // feedforward
    LONG a1, a2;       // feedback（a0 归一化为 1.0）
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

// 结构尺寸自检（防止用户态/内核态布局不一致）
C_ASSERT(sizeof(EQBandCoeffs)  == 20);   // 5 * 4
C_ASSERT(sizeof(EQCoeffParams) == 244);  // 4 + 12*20

#ifdef __cplusplus
extern "C" {
#endif
// 仅供 EQ 模块实现使用（在 .cpp 中定义）
extern EQBandCoeffs  g_EqBands[EQ_BANDS];
extern EQBandRuntime g_EqStates[EQ_BANDS][2];  // [band][0=L,1=R]
#ifdef __cplusplus
}
#endif
