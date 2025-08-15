#include "ApplyAudioProcessing.h"
#include "../../AudioVolumeControl/AudioEQControl.h"
#include "../../AudioVolumeControl/AudioVolumeControl.h"
int g_CurrentGainDb = 0;

// 假设你有一个全局变量保存当前音量值（单位s dB）
extern int g_CurrentGainDb;
// 补充 BYTE 类型定义（如果没有）
typedef unsigned char BYTE;
// 主处理函数
// 主处理函数：被 PinWriteProcess 调用
void ApplyAudioProcessing(BYTE *pBuffer, ULONG bufferLength)
{
    if (!pBuffer || bufferLength == 0)
        return;

    // === 基本变量初始化 ===
    SHORT *samples = (SHORT *)pBuffer;
    ULONG sampleCount = bufferLength / sizeof(SHORT);

    static ULONG s_frameCounter = 0;
    s_frameCounter++;

    // === 判断是否为静音 ===
    bool isSilence = true;
    for (ULONG i = 0; i < sampleCount; ++i)
    {
        if (samples[i] != 0)
        {
            isSilence = false;
            break;
        }
    }

    // === 打印样本（每30帧打印一次） ===
    if (!isSilence && s_frameCounter % 30 == 0)
    {
        DbgPrint("[Audio] Playback ACTIVE - Frame #%lu, SampleCount = %lu\n", s_frameCounter, sampleCount);
        for (ULONG i = 0; i < min(sampleCount, 8); ++i)
        {
            DbgPrint("  Sample[%lu] = %d\n", i, samples[i]);
        }
    }
    else if (isSilence && s_frameCounter % 100 == 0) // 偶尔打印静音帧标记
    {
        DbgPrint("[Audio] Silence - Frame #%lu\n", s_frameCounter);
    }

    // === 调用音量控制模块 ===
    VolumeControl_Apply(pBuffer, bufferLength, g_CurrentGainDb);
    DbgPrint("[Audio] VolumeControl_Apply finished\n");

    // === 调用 EQ 模块 ===
    EQControl_Apply(pBuffer, bufferLength);
    DbgPrint("[Audio] EQControl_Apply finished\n");
}