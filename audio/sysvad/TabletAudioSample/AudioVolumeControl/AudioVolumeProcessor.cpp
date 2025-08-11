// AudioVolumeProcessor.cpp
#include <ntddk.h>
#include <windef.h>
#include "AudioVolumeControl.h"  // 包含全局 g_DeviceVolume, g_Muted 的声明

// -----------------------------------------------------------------------------
// Q15 定点增益换算表（索引为 dB + 40，范围 0~80）
// 表中数值单位为 Q15（32768 = 1.0）
// -----------------------------------------------------------------------------
static const USHORT DbGainQ15Table[81] = {
    327,    367,    412,    463,    520,    585,    658,    741,    835,    941,   // -40 ~ -31 dB
   1061,   1196,   1349,   1522,   1716,   1934,   2179,   2452,   2757,   3097,   // -30 ~ -21 dB
   3477,   3901,   4375,   4904,   5494,   6153,   6888,   7708,   8623,   9644,   // -20 ~ -11 dB
  10783,  12055,  13474,  15058,  16826,  18800,  21003,  23462,  26206,  29270,   // -10 ~ -1 dB
  32768,  36699,  41183,  46251,  51939,  58300,  65394,  73294,  82100,  91936,   // 0 ~ +9 dB
 102225, 113997, 127396, 142571, 159692, 178946, 200539, 224700, 251684, 281773,  // +10 ~ +19 dB
 315288, 352578, 394043, 440122, 491299, 548106, 611128, 680999, 758419, 844158,  // +20 ~ +29 dB
 939109,1044289,1160732,1290068,1433574,1592646,1768828,1963824,2179515,2417962, // +30 ~ +39 dB
 2681437                                                             // +40 dB
};

// 将 dB 转换为 Q15 增益值（返回 int，范围约 327~2681437）
static __forceinline int DbToQ15Gain(int gainDb)
{
    if (gainDb < -40) gainDb = -40;
    if (gainDb >  40) gainDb =  40;
    return (int)DbGainQ15Table[gainDb + 40];
}

// Q15 定点乘法：a * b >> 15（用于短整数增益放大）
static __forceinline SHORT MulQ15(SHORT sample, int q15Gain)
{
    return (SHORT)(((INT64)sample * q15Gain) >> 15);
}

// 核心音量处理函数（使用 Q15 定点）
void VolumeControl_Apply(BYTE* pBuffer, ULONG length, int gainDb)
{
    if (!pBuffer || length == 0 || g_Muted)
        return;

    SHORT* samples = (SHORT*)pBuffer;
    ULONG sampleCount = length / sizeof(SHORT);
    int q15Gain = DbToQ15Gain(gainDb);

    for (ULONG i = 0; i < sampleCount; ++i)
    {
        int scaled = MulQ15(samples[i], q15Gain);

        // 防止溢出（对齐短整型）
        if (scaled > 32767) scaled = 32767;
        if (scaled < -32768) scaled = -32768;

        samples[i] = (SHORT)scaled;
    }

    DbgPrint("[Volume] VolumeControl_Apply: gainDb=%d → q15Gain=%d, samples=%lu\n",
             gainDb, q15Gain, sampleCount);
}

// 初始化音量控制状态
void VolumeControl_Init()
{
    g_DeviceVolume = 30;
    g_Muted = FALSE;
    DbgPrint("[Volume] VolumeControl_Init complete. Volume=100, Muted=FALSE\n");
}
