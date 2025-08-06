// AudioVolumeProcessor.cpp
#include <ntddk.h>
#include <windef.h>
#include "AudioVolumeControl.h"  // 包含全局 g_DeviceVolume, g_Muted 的声明

// 将 dB（±40范围）转换为线性增益（近似处理）
static __forceinline float DbToLinearGain(int gainDb)
{
    // ln(10) ≈ 2.302585f，所以 10^(x) = exp(x * ln(10))
    // 内核无 expf()，此为近似线性展开（Taylor 或查表可进一步优化）
    float x = gainDb / 20.0f;
    float ln10 = 2.302585f;

    // 简易指数近似：exp(x) ≈ 1 + x + x²/2 + x³/6
    float power = x * ln10;
    float result = 1.0f + power + (power * power) / 2.0f + (power * power * power) / 6.0f;

    return result;
}

// 核心音量处理函数
void VolumeControl_Apply(BYTE* pBuffer, ULONG length, int gainDb)
{
    if (!pBuffer || length == 0 || g_Muted)
        return;

    SHORT* samples = (SHORT*)pBuffer;
    ULONG sampleCount = length / sizeof(SHORT);
    float gain = DbToLinearGain(gainDb);

    for (ULONG i = 0; i < sampleCount; ++i)
    {
        float scaled = samples[i] * gain;
        if (scaled > 32767.0f) scaled = 32767.0f;
        if (scaled < -32768.0f) scaled = -32768.0f;
        samples[i] = (SHORT)scaled;
    }

    DbgPrint("[Volume] VolumeControl_Apply: gainDb=%d → gain=%.2f, samples=%lu\n",
             gainDb, gain, sampleCount);
}
void VolumeControl_Init()
{
    g_DeviceVolume = 30;
    g_Muted = FALSE;
    DbgPrint("[Volume] VolumeControl_Init complete. Volume=100, Muted=FALSE\n");
}
