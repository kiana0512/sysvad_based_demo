#pragma once

#include <ntddk.h>
#include "AudioEQTypes.h"
// 补充 BYTE 类型定义（如果没有）
typedef unsigned char BYTE;
// -------------------------
// IOCTL 定义
// -------------------------
#define IOCTL_SET_EQ_PARAMS           CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_EQ_PARAMS           CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_EQ_GAIN_ARRAY       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x903, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_EQ_BIQUAD_COEFFS    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x910, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_EQ_BIQUAD_COEFFS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x913, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SEND_PCM                CTL_CODE(FILE_DEVICE_UNKNOWN, 0x911, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_RECV_PCM                CTL_CODE(FILE_DEVICE_UNKNOWN, 0x912, METHOD_BUFFERED, FILE_READ_DATA)

// -------------------------
// EQ 相关参数
// -------------------------
#define EQ_BANDS            12
#define MAX_EQ_BANDS        12
#define EQ_TEST_BUFFER_SIZE 262144  // 256KB 缓冲区

// -------------------------
// 全局缓冲（用于 PCM 处理测试）
// -------------------------
extern UCHAR g_EqTestBuffer[EQ_TEST_BUFFER_SIZE];
extern ULONG g_EqTestSize;

// -------------------------
// 函数声明
// -------------------------

// 初始化
void EQControl_Init();

// PCM EQ 处理函数（Biquad 版本）
void ProcessEqPcmBuffer(short* pcm, int samples);

// 设置 EQ 系数（来自 IOCTL）
void EQControl_SetBiquadCoeffs(const EQCoeffParams* params);

void EQControl_GetBiquadCoeffs(EQCoeffParams* outParams);

// PCM EQ 处理接口（接收 PCM 缓冲区）
void EQControl_Apply(BYTE* pBuffer, ULONG length);
