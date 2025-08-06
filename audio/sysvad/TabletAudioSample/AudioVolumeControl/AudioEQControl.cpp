#include <ntddk.h>
#include "AudioEQControl.h"
#define EQ_BANDS 12

// ---------------------------
// 全局变量定义
// ---------------------------

// EQ 滤波器状态（每段一个 Biquad 滤波器）
EQBandCoeffs g_EqBands[EQ_BANDS] = {0};
EQBandRuntime g_EqStates[EQ_BANDS][2] = {0};

// PCM 缓冲区（用于用户态测试数据）
UCHAR g_EqTestBuffer[EQ_TEST_BUFFER_SIZE] = {0};
ULONG g_EqTestSize = 0;

// Q15 乘法
__forceinline int MulQ15(int a, int b)
{
    return (int)(((__int64)a * (__int64)b) >> 15);
}

// ---------------------------
// EQ 初始化（可选清空状态）
// ---------------------------

void EQControl_Init()
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

// ---------------------------
// 设置 EQ Q15 系数（用户态传入）
// ---------------------------

void EQControl_SetBiquadCoeffs(const EQCoeffParams *params)
{
    if (!params || params->BandCount > EQ_BANDS)
    {
        DbgPrint("[EQ] Invalid EQCoeffParams pointer or BandCount\n");
        return;
    }

    for (int i = 0; i < params->BandCount; ++i)
    {
        g_EqBands[i].b0 = params->Bands[i].b0;
        g_EqBands[i].b1 = params->Bands[i].b1;
        g_EqBands[i].b2 = params->Bands[i].b2;
        g_EqBands[i].a1 = params->Bands[i].a1;
        g_EqBands[i].a2 = params->Bands[i].a2;

        // 重置滤波器历史状态（避免爆音）
        g_EqStates[i][0].x1 = g_EqStates[i][0].x2 = 0;
        g_EqStates[i][0].y1 = g_EqStates[i][0].y2 = 0;
        g_EqStates[i][1].x1 = g_EqStates[i][1].x2 = 0;
        g_EqStates[i][1].y1 = g_EqStates[i][1].y2 = 0;

        DbgPrint("[EQ] Band[%02d] Q15 Coeff: b0=%d, b1=%d, b2=%d, a1=%d, a2=%d\n",
                 i, params->Bands[i].b0, params->Bands[i].b1, params->Bands[i].b2,
                 params->Bands[i].a1, params->Bands[i].a2);
    }

    DbgPrint("[EQ] SetBiquadCoeffs updated. BandCount=%d\n", params->BandCount);
}
void EQControl_GetBiquadCoeffs(EQCoeffParams *outParams)
{
    if (!outParams)
        return;

    outParams->BandCount = EQ_BANDS;

    for (int i = 0; i < EQ_BANDS; ++i)
    {
        outParams->Bands[i].b0 = g_EqBands[i].b0;
        outParams->Bands[i].b1 = g_EqBands[i].b1;
        outParams->Bands[i].b2 = g_EqBands[i].b2;
        outParams->Bands[i].a1 = g_EqBands[i].a1;
        outParams->Bands[i].a2 = g_EqBands[i].a2;

        DbgPrint("[EQ] GetBand[%02d]: b0=%d b1=%d b2=%d a1=%d a2=%d\n",
                 i,
                 g_EqBands[i].b0, g_EqBands[i].b1, g_EqBands[i].b2,
                 g_EqBands[i].a1, g_EqBands[i].a2);
    }

    DbgPrint("[EQ] EQControl_GetBiquadCoeffs: OK, BandCount=%d\n", EQ_BANDS);
}
void EQControl_Apply(BYTE *pBuffer, ULONG length)
{
    if (!pBuffer || length < 4)
        return;

    SHORT *samples = (SHORT *)pBuffer;
    ULONG sampleCount = length / sizeof(SHORT); // 总样本数（包含左右声道）
    if (sampleCount % 2 != 0)
        sampleCount--; // 必须为偶数，防止越界

    for (ULONG i = 0; i < sampleCount; i += 2)
    {
        int left = samples[i];
        int right = samples[i + 1];

        // 对每个通道分别进行 12 段 EQ 串联处理
        for (int band = 0; band < EQ_BANDS; ++band)
        {
            EQBandCoeffs *coeff = &g_EqBands[band];
            EQBandRuntime *stateL = &g_EqStates[band][0]; // 左声道
            EQBandRuntime *stateR = &g_EqStates[band][1]; // 右声道

            // ----------- 左声道处理 -----------
            int x0 = left;
            int y0 = MulQ15(coeff->b0, x0) +
                     MulQ15(coeff->b1, stateL->x1) +
                     MulQ15(coeff->b2, stateL->x2) -
                     MulQ15(coeff->a1, stateL->y1) -
                     MulQ15(coeff->a2, stateL->y2);

            // 更新状态
            stateL->x2 = stateL->x1;
            stateL->x1 = x0;
            stateL->y2 = stateL->y1;
            stateL->y1 = y0;
            left = y0;

            // ----------- 右声道处理 -----------
            x0 = right;
            y0 = MulQ15(coeff->b0, x0) +
                 MulQ15(coeff->b1, stateR->x1) +
                 MulQ15(coeff->b2, stateR->x2) -
                 MulQ15(coeff->a1, stateR->y1) -
                 MulQ15(coeff->a2, stateR->y2);

            stateR->x2 = stateR->x1;
            stateR->x1 = x0;
            stateR->y2 = stateR->y1;
            stateR->y1 = y0;
            right = y0;
        }

        // 饱和
        if (left > 32767)
            left = 32767;
        if (left < -32768)
            left = -32768;
        if (right > 32767)
            right = 32767;
        if (right < -32768)
            right = -32768;

        // 写回结果
        samples[i] = (SHORT)left;
        samples[i + 1] = (SHORT)right;
    }

    DbgPrint("[EQ] EQControl_Apply finished. Samples processed: %lu\n", sampleCount / 2);
}
