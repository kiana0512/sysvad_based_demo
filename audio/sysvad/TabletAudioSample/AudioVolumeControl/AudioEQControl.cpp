//
// AudioEQControl.cpp —— 12 段双声道串联 biquad（Q15）
//
// 只在本编译单元实现一次 EQControl_Apply，杜绝重定义。
// 依赖 <ntddk.h> 提供类型；不再自定义 BYTE。
// 

#include <ntddk.h>
#include "AudioEQControl.h"

#define CLAMP16(v)  (((v) > 32767) ? 32767 : (((v) < -32768) ? -32768 : (v)))

// ===== 全局：系数与状态（仅 EQ 模块内部使用） ===============================
EQBandCoeffs  g_EqBands[EQ_BANDS]     = { 0 };
EQBandRuntime g_EqStates[EQ_BANDS][2] = { 0 };  // [band][0=L,1=R]

// （可选）测试缓冲：与 AudioControlDispatch.cpp 的 send/recv PCM 调试配合
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

    DbgPrint("[EQ] EQControl_Init complete. All state reset.\n");
}

// =================== 设置/读取系数 ===================
void EQControl_SetBiquadCoeffs(_In_ const EQCoeffParams* params)
{
    if (!params || params->BandCount <= 0 || params->BandCount > EQ_BANDS)
    {
        DbgPrint("[EQ] SetBiquadCoeffs: invalid params or BandCount=%d\n",
                 params ? params->BandCount : -1);
        return;
    }

    const int n = (int)params->BandCount;
    for (int i = 0; i < n; ++i)
    {
        g_EqBands[i] = params->Bands[i];

        // 换系数时清历史，避免爆音/冲击
        g_EqStates[i][0].x1 = g_EqStates[i][0].x2 = 0;
        g_EqStates[i][0].y1 = g_EqStates[i][0].y2 = 0;
        g_EqStates[i][1].x1 = g_EqStates[i][1].x2 = 0;
        g_EqStates[i][1].y1 = g_EqStates[i][1].y2 = 0;

        DbgPrint("[EQ] Band[%02d] b0=%ld b1=%ld b2=%ld a1=%ld a2=%ld\n",
                 i, g_EqBands[i].b0, g_EqBands[i].b1, g_EqBands[i].b2,
                    g_EqBands[i].a1, g_EqBands[i].a2);
    }

    DbgPrint("[EQ] SetBiquadCoeffs OK, BandCount=%d\n", n);
}

void EQControl_GetBiquadCoeffs(_Out_ EQCoeffParams* outParams)
{
    if (!outParams) return;

    outParams->BandCount = EQ_BANDS;
    for (int i = 0; i < EQ_BANDS; ++i)
        outParams->Bands[i] = g_EqBands[i];

    DbgPrint("[EQ] GetBiquadCoeffs: BandCount=%d\n", outParams->BandCount);
}

// =================== EQ 处理（立体声 16-bit） ===================
void EQControl_Apply(_Inout_updates_bytes_(length) UCHAR* pBuffer, _In_ ULONG length)
{
    if (!pBuffer || length < sizeof(SHORT) * 2)
        return;

    // 2 字节对齐
    ULONG aligned = length & ~(sizeof(SHORT) - 1);
    SHORT* s      = (SHORT*)pBuffer;
    ULONG  nSmpl  = aligned / sizeof(SHORT);

    // 立体声：需要偶数样本
    if ((nSmpl & 1) != 0)
        nSmpl--;

    for (ULONG i = 0; i + 1 < nSmpl; i += 2)
    {
        LONG l = s[i];
        LONG r = s[i + 1];

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

        s[i]     = (SHORT)CLAMP16(l);
        s[i + 1] = (SHORT)CLAMP16(r);
    }

    DbgPrint("[EQ] Apply done. frames=%lu\n", (ULONG)(nSmpl / 2));
}
