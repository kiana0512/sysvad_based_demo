#include "ApplyAudioProcessing.h"
#include "AudioEQControl.h"
#include "AudioVolumeControl.h"
unsigned long g_DeviceVolume = 0;
unsigned char g_Muted = 0;
int g_CurrentGainDb = 0;

// 假设你有一个全局变量保存当前音量值（单位s dB）
extern int g_CurrentGainDb;
// 补充 BYTE 类型定义（如果没有）
typedef unsigned char BYTE;
// 主处理函数
void ApplyAudioProcessing(BYTE* pBuffer, ULONG bufferLength)
{
    if (!pBuffer || bufferLength == 0)
        return;

    // 1. 应用音量增益（如果你有 VolumeControl 模块）
    VolumeControl_Apply(pBuffer, bufferLength, g_CurrentGainDb);
    DbgPrint("[Audio] VolumeControl_Apply finished\n");

    // 2. 应用 EQ 处理（你自己的 EQControl_Apply 函数）
    EQControl_Apply(pBuffer, bufferLength);
    DbgPrint("[Audio] EQControl_Apply finished\n");
}
