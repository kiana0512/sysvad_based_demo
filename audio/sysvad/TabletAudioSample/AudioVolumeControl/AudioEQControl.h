#pragma once
//
// AudioEQControl.h —— EQ 控制接口（与 KS 驱动对齐）
// 只声明 EQ 初始化/应用/读写系数；不定义 IOCTL；不声明分发函数。
// 这里还定义调试用的共享常量 EQ_TEST_BUFFER_SIZE，并导出测试缓冲 extern，
// 供 AudioControlDispatch.cpp 的 IOCTL_SEND/RECV_PCM 使用。
//

#include <ntddk.h>
#include "AudioEQTypes.h"

// === 共享给内核内多个编译单元的测试缓冲大小（默认 256 KB） ===
#ifndef EQ_TEST_BUFFER_SIZE
#define EQ_TEST_BUFFER_SIZE 262144u
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 初始化：清空 12 段的系数与历史状态
void EQControl_Init(void);

// 下发/读取一整组 Q15 系数（前 BandCount 有效）
// 换系数时内部会清历史状态，避免爆音
void EQControl_SetBiquadCoeffs(_In_ const EQCoeffParams* params);
void EQControl_GetBiquadCoeffs(_Out_ EQCoeffParams* outParams);

// 对一帧 PCM（立体声 16-bit 小端）执行 12 段串联 EQ
// 注意：统一使用 UCHAR*，避免 BYTE 未声明
void EQControl_Apply(_Inout_updates_bytes_(length) UCHAR* pBuffer, _In_ ULONG length);

// === 调试缓冲（由 AudioEQControl.cpp 定义；这里仅导出给分发模块使用） ===
extern UCHAR g_EqTestBuffer[EQ_TEST_BUFFER_SIZE];
extern ULONG g_EqTestSize;

#ifdef __cplusplus
}
#endif
