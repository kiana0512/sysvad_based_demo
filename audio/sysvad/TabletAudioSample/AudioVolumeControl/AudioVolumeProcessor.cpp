// AudioVolumeProcessor.cpp ——【按 dB 的 Q15 定点音量处理 + 超详细 DBG 日志】
//
// 设计要点：
//   - 与 KS 入口解耦：本模块只做“dB → Q15 增益 → 16-bit 样本缩放/饱和”。
//   - g_Muted/g_DeviceVolume 由上游统一管理；这里仅做静音短路（双保险）。
//   - 对所有边界情况（空指针/零长度/未对齐/静音/0 dB）均给出清晰 DBG。
//   - 每次处理输出一条开始、一条结束 DBG：含 IRQL、参数、对齐、Q15 值、样本统计、削顶统计、极值等。
//   - 日志可按需降噪：可把 LOG_DBG 宏映射到 INFO/TRACE 或注释掉。
//
// 可视化：请用 DbgView（勾选 Capture Kernel）或 WinDbg（!dbgprint）。
//

#include <ntddk.h>
#include <windef.h>
#include "AudioVolumeControl.h"  // 声明：g_DeviceVolume / g_Muted、API原型

// ======== 日志宏（若工程已有同名宏，会优先用工程内的定义） ===================
#ifndef LOG_TAG
#   define LOG_TAG "KIANA/VOL"
#endif

#ifndef LOG_DBG
#   define LOG_DBG(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,  "[" LOG_TAG "] " fmt "\n", __VA_ARGS__)
#endif
#ifndef LOG_WRN
#   define LOG_WRN(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,"[" LOG_TAG "] " fmt "\n", __VA_ARGS__)
#endif
#ifndef LOG_ERR
#   define LOG_ERR(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,  "[" LOG_TAG "] " fmt "\n", __VA_ARGS__)
#endif

// ===== dB → Q15 增益表（索引为 gainDb + 40，范围 -40..+40）===================
// 单位：Q15（32768 == 1.0）。+40 dB ≈ 100 倍 ⇒ Q15 ≈ 3,276,800（需 32 位存储）。
static const ULONG DbGainQ15Table[81] = {
      327,     367,     412,     463,     520,     585,     658,     741,     835,     941,   // -40..-31
     1061,    1196,    1349,    1522,    1716,    1934,    2179,    2452,    2757,    3097,   // -30..-21
     3477,    3901,    4375,    4904,    5494,    6153,    6888,    7708,    8623,    9644,   // -20..-11
    10783,   12055,   13474,   15058,   16826,   18800,   21003,   23462,   26206,   29270,   // -10.. -1
    32768,   36699,   41183,   46251,   51939,   58300,   65394,   73294,   82100,   91936,   // +0 .. +9
   102225,  113997,  127396,  142571,  159692,  178946,  200539,  224700,  251684,  281773,  // +10..+19
   315288,  352578,  394043,  440122,  491299,  548106,  611128,  680999,  758419,  844158,  // +20..+29
   939109, 1044289, 1160732, 1290068, 1433574, 1592646, 1768828, 1963824, 2179515, 2417962, // +30..+39
  2681437                                                                                   // +40
};
C_ASSERT(ARRAYSIZE(DbGainQ15Table) == 81);

// 边界裁剪后的 dB → Q15
_Use_decl_annotations_
static __forceinline LONG DbToQ15Gain(int gainDb)
{
    if (gainDb < -40) gainDb = -40;
    if (gainDb >  40) gainDb =  40;
    return (LONG)DbGainQ15Table[gainDb + 40];
}

// Q15 定点乘法：sample * gainQ15 >> 15，带饱和，并返回是否发生削顶
_Use_decl_annotations_
static __forceinline SHORT MulQ15Sat(SHORT sample, LONG gainQ15, __out_opt ULONG* clipped)
{
    const LONG  s  = (LONG)sample;
    const __int64 prod = (__int64)s * (__int64)gainQ15; // 64 位中间值
    LONG scaled = (LONG)(prod >> 15);                   // Q15 右移得到 16-bit 实际值

    if (scaled > 32767)  { scaled = 32767;  if (clipped) (*clipped)++; }
    if (scaled < -32768) { scaled = -32768; if (clipped) (*clipped)++; }
    return (SHORT)scaled;
}

// ====== 核心 API：样本级音量处理 ==============================================
EXTERN_C
VOID VolumeControl_Apply(_Inout_updates_bytes_(length) BYTE* pBuffer,
                         _In_                           ULONG  length,
                         _In_                           int    gainDb)
{
    const KIRQL irql = KeGetCurrentIrql();

    // 开始日志：只记录关键信息，避免刷屏；细节在结束日志汇总。
    LOG_DBG("Apply[enter]: irql=%lu, buf=%p, len=%lu, gainDb(in)=%d, muted=%s",
            (ULONG)irql, pBuffer, length, gainDb, g_Muted ? "TRUE" : "FALSE");

    // 基本防御
    if (!pBuffer) {
        LOG_WRN("Apply[exit-early]: null buffer");
        return;
    }
    if (length == 0) {
        LOG_WRN("Apply[exit-early]: zero length");
        return;
    }

    // 静音短路（双保险）
    if (g_Muted) {
        LOG_DBG("Apply[exit-early]: muted=TRUE, skip processing");
        return;
    }

    // 2 字节对齐（16-bit PCM）
    const ULONG aligned = length & ~(ULONG)(sizeof(SHORT) - 1);
    if (!aligned) {
        LOG_WRN("Apply[exit-early]: unaligned length=%lu (not even number of bytes)", length);
        return;
    }

    // 0 dB 优化路径：直接返回，但也记日志
    if (gainDb == 0) {
        LOG_DBG("Apply[exit-early]: gainDb=0 (no-op), alignedLen=%lu bytes", aligned);
        return;
    }

    SHORT* const samples = (SHORT*)pBuffer;
    const ULONG  count   = aligned / sizeof(SHORT);
    const LONG   q15     = DbToQ15Gain(gainDb);

    // 统计信息
    ULONG clipped = 0;
    SHORT inMin =  32767, inMax = -32768;
    SHORT outMin =  32767, outMax = -32768;

    // 主循环
    for (ULONG i = 0; i < count; ++i)
    {
        const SHORT s = samples[i];
        if (s < inMin) inMin = s;
        if (s > inMax) inMax = s;

        const SHORT o = MulQ15Sat(s, q15, &clipped);

        if (o < outMin) outMin = o;
        if (o > outMax) outMax = o;

        samples[i] = o;
    }

    // 结束日志：完整汇总，便于核对
    LOG_DBG("Apply[done]: gainDb=%d -> q15=%ld, aligned=%luB, samples=%lu, "
            "in[min,max]=[%hd,%hd], out[min,max]=[%hd,%hd], clipped=%lu",
            gainDb, q15, aligned, count, inMin, inMax, outMin, outMax, clipped);
}

// ====== 初始化：设置默认状态并记录日志 ========================================
EXTERN_C
VOID VolumeControl_Init()
{
    g_DeviceVolume = 100;  // 100% → 0 dB（由上游映射）
    g_Muted        = FALSE;

    LOG_DBG("Init: Volume=%lu%% (mapped to 0 dB by upper layer), Muted=%s",
            g_DeviceVolume, g_Muted ? "TRUE" : "FALSE");
}
