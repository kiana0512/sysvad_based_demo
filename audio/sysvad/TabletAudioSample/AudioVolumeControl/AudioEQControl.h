#pragma once
#include <ntddk.h>
#include "AudioEQTypes.h"

// -------------------------
// IOCTL 定义
// -------------------------
#define IOCTL_SET_EQ_PARAMS       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_EQ_PARAMS       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_EQ_GAIN_ARRAY   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x903, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SEND_PCM            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x911, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_RECV_PCM            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x912, METHOD_BUFFERED, FILE_READ_DATA)

// -------------------------
// EQ 参数配置
// -------------------------
#define EQ_BANDS        12
#define MAX_EQ_BANDS    12
#define EQ_TEST_BUFFER_SIZE 262144  // 256KB


#ifdef __cplusplus
#endif
extern int eqGainInt[EQ_BANDS];
void ProcessEqPcmBuffer(short* pcm, int samples);

// -------------------------
// 全局变量声明
// -------------------------
extern int eqGainInt[EQ_BANDS];                      // 映射后的整型增益值
extern EQControlParams g_EqParams;                   // 当前 EQ 配置参数
extern UCHAR g_EqTestBuffer[EQ_TEST_BUFFER_SIZE];    // 临时 PCM 缓冲区
extern ULONG g_EqTestSize;                           // 实际处理大小

// -------------------------
// 函数声明
// -------------------------
void EQControl_Init();
void EQControl_SetParams(const EQControlParams* newParams);
void EQControl_GetParams(EQControlParams* outParams);
void ProcessEqPcmBuffer(SHORT* samples, ULONG count);  // EQ 处理函数