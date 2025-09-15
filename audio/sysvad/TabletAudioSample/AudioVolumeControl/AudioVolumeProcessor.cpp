//
// AudioVolumeProcessor.cpp ——【按 dB 的 Q15 定点音量处理】
//
// 设计要点：
//   - 与 KS 入口解耦：本模块只负责“dB → Q15 增益 → 样本缩放”。
//   - g_Muted/g_DeviceVolume 的业务语义在更上游（ApplyAudioProcessing.cpp）统一映射；
//     本模块里仅在 g_Muted == TRUE 时快速返回（双保险），无副作用。
//   - 严格的健壮性：空指针/空长度直接返回；长度按 2 字节对齐处理，防越界。
//   - 日志限流交由上游；此处仅在处理完毕后打一条简要日志（可按需关闭）。
//

#include <ntddk.h>
#include <windef.h>
#include "AudioVolumeControl.h"  // 导入 g_DeviceVolume / g_Muted 声明与接口

// ===== dB → Q15 增益换算表（索引为 gainDb + 40，范围 -40..+40）===========
// 表中单位：Q15（32768 对应 1.0）
static const USHORT DbGainQ15Table[81] = {
    327,    367,    412,    463,    520,    585,    658,    741,    835,    941,   // -40..-31
   1061,   1196,   1349,   1522,   1716,   1934,   2179,   2452,   2757,   3097,   // -30..-21
   3477,   3901,   4375,   4904,   5494,   6153,   6888,   7708,   8623,   9644,   // -20..-11
  10783,  12055,  13474,  15058,  16826,  18800,  21003,  23462,  26206,  29270,   // -10.. -1
  32768,  36699,  41183,  46251,  51939,  58300,  65394,  73294,  82100,  91936,   // +0 .. +9
 102225, 113997, 127396, 142571, 159692, 178946, 200539, 224700, 251684, 281773,  // +10..+19
 315288, 352578, 394043, 440122, 491299, 548106, 611128, 680999, 758419, 844158,  // +20..+29
 939109,1044289,1160732,1290068,1433574,1592646,1768828,1963824,2179515,2417962, // +30..+39
 2681437                                                                                 // +40
};

// dB → Q15（边界裁剪：-40..+40）
static __forceinline int DbToQ15Gain(int gainDb)
{
    if (gainDb < -40) gainDb = -40;
    if (gainDb >  40) gainDb =  40;
    return (int)DbGainQ15Table[gainDb + 40];
}

// Q15 定点乘法（sample * gain >> 15），返回 16-bit 饱和值
static __forceinline SHORT MulQ15Sat(SHORT sample, int q15Gain)
{
    const INT64 prod = (INT64)sample * (INT64)q15Gain;  // 允 64 位中间值
    int scaled = (int)(prod >> 15);

    if (scaled >  32767) scaled =  32767;
    if (scaled < -32768) scaled = -32768;
    return (SHORT)scaled;
}

// ====== 核心 API：样本级音量处理 ============================================
EXTERN_C
VOID VolumeControl_Apply(_Inout_updates_bytes_(length) BYTE* pBuffer,
                         _In_                           ULONG  length,
                         _In_                           int    gainDb)
{
    // 基本防御
    if (!pBuffer || length == 0)
        return;

    // 静音的“双保险”：上游（ApplyAudioProcessing）已将静音映射为 -40dB 或跳过处理，
    // 这里再做一次快速短路，避免无谓开销。
    if (g_Muted)
        return;

    // 保证 2 字节对齐处理范围，防止越界
    const ULONG aligned = length & ~(sizeof(SHORT) - 1);
    if (!aligned)
        return;

    SHORT* samples     = (SHORT*)pBuffer;
    const ULONG count  = aligned / sizeof(SHORT);
    const int   gainQ15 = DbToQ15Gain(gainDb);

    for (ULONG i = 0; i < count; ++i)
    {
        samples[i] = MulQ15Sat(samples[i], gainQ15);
    }

    // 简要日志（可按需注释掉以减少开销）
    LOG_DBG("VolumeControl_Apply: gainDb=%d -> q15=%d, samples=%lu",
            gainDb, gainQ15, count);
}

// ====== 初始化：设置默认状态，与日志一致 ===================================
EXTERN_C
VOID VolumeControl_Init()
{
    g_DeviceVolume = 100;  // 100% → 0 dB
    g_Muted        = FALSE;

    LOG_DBG("VolumeControl_Init: Volume=%lu%%, Muted=%s",
            g_DeviceVolume, g_Muted ? "TRUE" : "FALSE");
}
