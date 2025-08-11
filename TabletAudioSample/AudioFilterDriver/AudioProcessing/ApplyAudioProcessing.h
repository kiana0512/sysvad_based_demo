#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <ntddk.h>
#include <ntddk.h>
// 补充 BYTE 类型定义（如果没有）
typedef unsigned char BYTE;
// 主处理函数：处理一块 PCM 缓冲区
void ApplyAudioProcessing(BYTE* pBuffer, ULONG bufferLength);

#ifdef __cplusplus
}
#endif
