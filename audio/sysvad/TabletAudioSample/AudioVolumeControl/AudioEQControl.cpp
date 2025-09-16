//
// AudioEQControl.cpp —— 12 段双声道串联 biquad（Q15）+ 详细 DBG
// 只在本编译单元实现一次 EQControl_Apply，杜绝重定义。
// 依赖 <ntddk.h> 提供类型；不再自定义 BYTE。
// 

#include <ntddk.h>
#include "AudioEQControl.h"

#ifndef LOG_TAG
#   define LOG_TAG "KIANA/EQ"
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

// ===== 可调日志细粒度 =====
// 1 = 逐段打印系数（Set 时），0 = 只打印汇总
#ifndef EQ_LOG_EACH_BAND
#define EQ_LOG_EACH_BAND 1
#endif
// 1 = Apply 时统计输入/输出极值与削顶次数，0 = 只打印帧数
#ifndef EQ_LOG_SAMPLE_STATS
#define EQ_LOG_SAMPLE_STATS 1
#endif

#define CLAMP16(v)  (((v) > 32767) ? 32767 : (((v) < -32768) ? -32768 : (v)))

// ===== 全局：系数与状态（仅 EQ 模块内部使用） ===============================
EQBandCoeffs  g_EqBands[EQ_BANDS]     = { 0 };
EQBandRuntime g_EqStates[EQ_BANDS][2] = { 0 };  // [band][0=L,1=R]

// （可选）测试缓冲：配合 IOCTL_SEND/RECV_PCM
UCHAR g_EqTestBuffer[EQ_TEST_BUFFER_SIZE] = { 0 };
ULONG g_EqTestSize = 0;

// Q15 乘法：返回 (a*b)>>15
__forceinline static LONG MulQ15(LONG a, LONG b)
{
    return (LONG)(((__int64)a * (__int64)b) >> 15);
}

// =================== 初始化 ===================
void EQControl_Init(void)
{
    for (int i = 0; i < EQ_BANDS; ++i)
    {
        g_EqBands[i].b0 = g_EqBands[i].b1 = g_EqBands[i].b2 = 0;
        g_EqBands[i].a1 = g_EqBands[i].a2 = 0;

        g_EqStates[i][0].x1 = g_EqStates[i][0].x2 = 0;
        g_EqStates[i][0].y1 = g_EqStates[i][0].y2 = 0;
        g_EqStates[i][1].x1 = g_EqStates[i][1].x2 = 0;
        g_EqStates[i][1].y1 = g_EqStates[i][1].y2 = 0;
    }
    g_EqTestSize = 0;

    LOG_DBG("Init: reset %d bands, testBuffer=%u bytes", EQ_BANDS, (unsigned)EQ_TEST_BUFFER_SIZE);
}

// =================== 设置/读取系数 ===================
static __forceinline LONG ClampQ15(LONG v)
{
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return v;
}

void EQControl_SetBiquadCoeffs(_In_ const EQCoeffParams* params)
{
    const KIRQL irql = KeGetCurrentIrql();
    if (!params) {
        LOG_ERR("SetCoeffs[fail]: null params (irql=%lu)", (ULONG)irql);
        return;
    }
    if (params->BandCount <= 0 || params->BandCount > EQ_BANDS) {
        LOG_ERR("SetCoeffs[fail]: invalid BandCount=%ld (irql=%lu)", params->BandCount, (ULONG)irql);
        return;
    }

    ULONG clipped = 0;
    const int n = (int)params->BandCount;

    for (int i = 0; i < n; ++i)
    {
        EQBandCoeffs c = params->Bands[i];

        // 统计越界（如果发现 Q15 超界，这里裁剪并计数）
        LONG in[5]  = { c.b0, c.b1, c.b2, c.a1, c.a2 };
        LONG out[5] = { 0 };

        for (int k = 0; k < 5; ++k) {
            out[k] = ClampQ15(in[k]);
            if (out[k] != in[k]) clipped++;
        }

        g_EqBands[i].b0 = out[0];
        g_EqBands[i].b1 = out[1];
        g_EqBands[i].b2 = out[2];
        g_EqBands[i].a1 = out[3];
        g_EqBands[i].a2 = out[4];

        // 换系数时清历史，避免爆音/冲击
        g_EqStates[i][0].x1 = g_EqStates[i][0].x2 = 0;
        g_EqStates[i][0].y1 = g_EqStates[i][0].y2 = 0;
        g_EqStates[i][1].x1 = g_EqStates[i][1].x2 = 0;
        g_EqStates[i][1].y1 = g_EqStates[i][1].y2 = 0;

#if EQ_LOG_EACH_BAND
        LOG_DBG("SetCoeffs: Band[%02d] b0=%ld b1=%ld b2=%ld a1=%ld a2=%ld",
                i, g_EqBands[i].b0, g_EqBands[i].b1, g_EqBands[i].b2,
                   g_EqBands[i].a1, g_EqBands[i].a2);
#endif
    }

    if (clipped)
        LOG_WRN("SetCoeffs: BandCount=%d, Q15-clipped=%lu value(s)", n, clipped);
    LOG_DBG("SetCoeffs[ok]: BandCount=%d (irql=%lu)", n, (ULONG)irql);
}

void EQControl_GetBiquadCoeffs(_Out_ EQCoeffParams* outParams)
{
    const KIRQL irql = KeGetCurrentIrql();
    if (!outParams) {
        LOG_ERR("GetCoeffs[fail]: null outParams (irql=%lu)", (ULONG)irql);
        return;
    }

    outParams->BandCount = EQ_BANDS;
    for (int i = 0; i < EQ_BANDS; ++i)
        outParams->Bands[i] = g_EqBands[i];

    LOG_DBG("GetCoeffs[ok]: BandCount=%d (irql=%lu)", outParams->BandCount, (ULONG)irql);
}

// =================== EQ 处理（立体声 16-bit） ===================
void EQControl_Apply(_Inout_updates_bytes_(length) UCHAR* pBuffer, _In_ ULONG length)
{
    const KIRQL irql = KeGetCurrentIrql();

    if (!pBuffer || length < sizeof(SHORT) * 2) {
        LOG_WRN("Apply[exit-early]: buf=%p len=%lu (irql=%lu)", pBuffer, length, (ULONG)irql);
        return;
    }

    // 2 字节对齐
    ULONG aligned = length & ~(ULONG)(sizeof(SHORT) - 1);
    SHORT* s      = (SHORT*)pBuffer;
    ULONG  nSmpl  = aligned / sizeof(SHORT);

    // 立体声：样本数为偶数
    if (nSmpl & 1) nSmpl--;

#if EQ_LOG_SAMPLE_STATS
    SHORT inMinL =  32767, inMaxL = -32768;
    SHORT inMinR =  32767, inMaxR = -32768;
    SHORT outMinL =  32767, outMaxL = -32768;
    SHORT outMinR =  32767, outMaxR = -32768;
    ULONG clipped = 0;
#endif

    for (ULONG i = 0; i + 1 < nSmpl; i += 2)
    {
        LONG l = s[i];
        LONG r = s[i + 1];

#if EQ_LOG_SAMPLE_STATS
        if (l < inMinL) inMinL = (SHORT)l; if (l > inMaxL) inMaxL = (SHORT)l;
        if (r < inMinR) inMinR = (SHORT)r; if (r > inMaxR) inMaxR = (SHORT)r;
#endif

        // 12 段串联
        for (int b = 0; b < EQ_BANDS; ++b)
        {
            EQBandCoeffs*  c = &g_EqBands[b];
            EQBandRuntime* L = &g_EqStates[b][0];
            EQBandRuntime* R = &g_EqStates[b][1];

            // Left
            {
                const LONG x0 = l;
                const LONG y0 =  MulQ15(c->b0, x0)
                                +MulQ15(c->b1, L->x1)
                                +MulQ15(c->b2, L->x2)
                                -MulQ15(c->a1, L->y1)
                                -MulQ15(c->a2, L->y2);
                L->x2 = L->x1; L->x1 = x0;
                L->y2 = L->y1; L->y1 = y0;
                l = y0;
            }

            // Right
            {
                const LONG x0 = r;
                const LONG y0 =  MulQ15(c->b0, x0)
                                +MulQ15(c->b1, R->x1)
                                +MulQ15(c->b2, R->x2)
                                -MulQ15(c->a1, R->y1)
                                -MulQ15(c->a2, R->y2);
                R->x2 = R->x1; R->x1 = x0;
                R->y2 = R->y1; R->y1 = y0;
                r = y0;
            }
        }

#if EQ_LOG_SAMPLE_STATS
        if (l > 32767 || l < -32768) clipped++;
        if (r > 32767 || r < -32768) clipped++;
#endif

        s[i]     = (SHORT)CLAMP16(l);
        s[i + 1] = (SHORT)CLAMP16(r);

#if EQ_LOG_SAMPLE_STATS
        if (s[i]     < outMinL) outMinL = s[i];
        if (s[i]     > outMaxL) outMaxL = s[i];
        if (s[i + 1] < outMinR) outMinR = s[i + 1];
        if (s[i + 1] > outMaxR) outMaxR = s[i + 1];
#endif
    }

    const ULONG frames = (ULONG)(nSmpl / 2);

#if EQ_LOG_SAMPLE_STATS
    LOG_DBG("Apply[done]: irql=%lu, buf=%p len=%lu (aligned=%lu), frames=%lu, "
            "inL[min,max]=[%hd,%hd] inR[min,max]=[%hd,%hd], "
            "outL[min,max]=[%hd,%hd] outR[min,max]=[%hd,%hd], clipped=%lu",
            (ULONG)irql, pBuffer, length, aligned, frames,
            inMinL, inMaxL, inMinR, inMaxR, outMinL, outMaxL, outMinR, outMaxR, clipped);
#else
    LOG_DBG("Apply[done]: irql=%lu, buf=%p len=%lu (aligned=%lu), frames=%lu",
            (ULONG)irql, pBuffer, length, aligned, frames);
#endif
}
