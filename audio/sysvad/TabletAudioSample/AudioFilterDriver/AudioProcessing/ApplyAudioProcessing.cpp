//
// ApplyAudioProcessing.cpp  —— 音频数据处理入口（以 KS 驱动的状态为唯一真相）
//
// 设计要点：
//   1) 不再使用 g_CurrentGainDb；真实处理增益 = f(g_DeviceVolume, g_Muted)
//      - g_DeviceVolume 由用户态 IOCTL_KIANA_SET_VOLUME 写入（0~100%）
//      - g_Muted        由 IOCTL_KIANA_MUTE / IOCTL_KIANA_UNMUTE 写入
//   2) 统一把 [0%..100%] 映射成 [-40dB..0dB]（静音直接 -40dB）后，调用 VolumeControl_Apply
//   3) 顺序保持：先音量，再 EQ（EQControl_Apply）
//   4) 加强健壮性：空指针/空长度检查、样本对齐检查、打印限流
//
// 线程/IRQL 说明：
//   - 本函数由 Pin 写路径（KS 数据流）调用，典型运行于 PASSIVE_LEVEL。
//   - 本实现不触碰分页代码标注（不使用 PAGED_CODE），避免上层以更高 IRQL 触发页错。
//

#include "ApplyAudioProcessing.h"
#include "../../AudioVolumeControl/AudioEQControl.h"
#include "../../AudioVolumeControl/AudioVolumeControl.h"

// ===== 来自音量子系统的“唯一真相” =====
// 在 AudioVolumeControl.h 中声明并定义：
//   extern ULONG   g_DeviceVolume;   // 0~100
//   extern BOOLEAN g_Muted;          // TRUE/FALSE
// 这里直接使用它们，不再引入 g_CurrentGainDb。
extern "C" ULONG   g_DeviceVolume;
extern "C" BOOLEAN g_Muted;

// 为避免不同编译单元的定义不一致，这里不再重复 typedef BYTE；
// BYTE/ULONG/SHORT 等类型已由 WDK 头引入（通过上面的包含链）。

//------------------------------------------------------------------------------
// 把 0~100% 的音量映射为 -40dB ~ 0dB；静音时固定 -40dB。
// VolumeControl_Apply 会把 dB 转成 Q15 增益后做乘法与饱和。
//------------------------------------------------------------------------------
static __forceinline int PercentToGainDb(_In_ ULONG volPercent, _In_ BOOLEAN muted)
{
    if (muted)
        return -40;                        // 静音：最小增益（对应近似完全衰减）

    if (volPercent > 100)                  // 容错：限制上界
        volPercent = 100;

    // 线性映射：0% -> -40dB，100% -> 0dB
    // 注意：这里按你 VolumeControl 的 Q15 表范围设计（-40..+40dB）
    // 若未来需要“>0dB 增益”，可把上界放宽到 +6dB 或 +12dB 并同步更新查表。
    return (int)((long)volPercent * 40 / 100) - 40;
}

//------------------------------------------------------------------------------
// 主处理函数：被 KS Pin 的写路径调用，输入为 PCM 16-bit（little-endian）
// pBuffer:  音频缓冲区（BYTE*）
// bufferLength: 字节长度（必须是 sizeof(SHORT) 的整数倍）
//------------------------------------------------------------------------------
extern "C"
void ApplyAudioProcessing(_Inout_updates_bytes_(bufferLength) BYTE* pBuffer,
                          _In_                           ULONG  bufferLength)
{
    // 基本防御：空指针/空长度直接返回
    if (!pBuffer || bufferLength == 0)
        return;

    // 若长度不是 2 字节对齐（SHORT 对齐），向下取整处理，避免越界
    ULONG alignedBytes = bufferLength & ~(sizeof(SHORT) - 1);
    if (alignedBytes == 0)
        return;

    SHORT* samples     = reinterpret_cast<SHORT*>(pBuffer);
    ULONG  sampleCount = alignedBytes / sizeof(SHORT);

    // ------------------ 诊断打印（限流） ------------------
    // 避免大量打印导致的性能/时序干扰：仅做非常轻的采样打印
    static ULONG s_frameCounter = 0;
    s_frameCounter++;

    bool isSilence = true;
    // 轻量判静音：最多检查前 256 个样本，避免长缓冲全扫描
    const ULONG probe = (sampleCount < 256) ? sampleCount : 256;
    for (ULONG i = 0; i < probe; ++i)
    {
        if (samples[i] != 0) { isSilence = false; break; }
    }

    if (!isSilence && (s_frameCounter % 50 == 0))
    {
        DbgPrint("[Audio] Active frame #%lu, sampleCount=%lu (show first 8)\n",
                 s_frameCounter, sampleCount);
        for (ULONG i = 0; i < (sampleCount < 8 ? sampleCount : 8); ++i)
            DbgPrint("  s[%lu]=%d\n", i, samples[i]);
    }
    else if (isSilence && (s_frameCounter % 200 == 0))
    {
        DbgPrint("[Audio] Silence frame #%lu\n", s_frameCounter);
    }

    // ------------------ 计算真实增益（以 KS 驱动状态为准） ------------------
    const int gainDb = PercentToGainDb(g_DeviceVolume, g_Muted);
    //DbgPrint("[Audio] gainDb=%d (vol=%lu%%, muted=%s)\n", gainDb, g_DeviceVolume, g_Muted ? "TRUE" : "FALSE");

    // ------------------ 先音量，再 EQ ------------------
    // 说明：保持原先处理顺序（Volume → EQ），避免 EQ 后再放大导致可能的溢出。
    VolumeControl_Apply(pBuffer, alignedBytes, gainDb);
    EQControl_Apply(pBuffer, alignedBytes);

    // 若 bufferLength 不是偶数，剩余 1 字节原样保留；上层通常不依赖尾字节内容。
    // 如果你的上游保证永远 2 字节对齐，可移除此注释和对齐分支。
}
