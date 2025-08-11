#include "eq.h"

// 定义 EQ 段数
#define EQ_BANDS 12
#define Q15_SHIFT 15

// 全局滤波器状态数组（12 段）
static EQBandState eqBands[EQ_BANDS];

// Q15 定点乘法：a * b >> 15
static inline int MulQ15(int a, int b)
{
    return (int)(((__int64)a * (__int64)b) >> Q15_SHIFT);
}

// 应用一个 EQ 段的 Biquad 滤波器
static short ApplyBiquad(EQBandState* state, short input)
{
    int x = (int)input;

    int y = MulQ15(state->b0, x)
          + MulQ15(state->b1, state->x1)
          + MulQ15(state->b2, state->x2)
          - MulQ15(state->a1, state->y1)
          - MulQ15(state->a2, state->y2);

    // 更新状态
    state->x2 = state->x1;
    state->x1 = x;
    state->y2 = state->y1;
    state->y1 = y;

    // 限幅
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;

    return (short)y;
}

// 主处理函数：替换原模拟 EQ 实现
void ProcessEqPcmBuffer(short* pcm, int samples)
{
    for (int i = 0; i < samples; ++i)
    {
        short sample = pcm[i];

        // 顺序通过所有 EQ 段
        for (int b = 0; b < EQ_BANDS; ++b)
        {
            sample = ApplyBiquad(&eqBands[b], sample);
        }

        pcm[i] = sample;
    }
}

// 设置 Q15 系数（来自用户态）
void SetEqCoeffs(const EQCoeffParams* params)
{
    if (!params || params->BandCount > EQ_BANDS)
        return;

    for (int i = 0; i < params->BandCount; ++i)
    {
        eqBands[i].b0 = params->Bands[i].b0;
        eqBands[i].b1 = params->Bands[i].b1;
        eqBands[i].b2 = params->Bands[i].b2;
        eqBands[i].a1 = params->Bands[i].a1;
        eqBands[i].a2 = params->Bands[i].a2;

        // 清空历史状态
        eqBands[i].x1 = eqBands[i].x2 = 0;
        eqBands[i].y1 = eqBands[i].y2 = 0;
    }
}
