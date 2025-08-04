#define EQ_BANDS 12
#include <wdm.h>  // 内核 API
#include "AudioEQControl.h"

// 每段 EQ 增益（-100 ~ +100），单位是百分比
// 例如 eqGainInt[0] = -50 表示该频段音量衰减 50%
static int eqGainInt[EQ_BANDS] = {
    0, 0, 0, 0, 0, 0,   // 31Hz~1kHz
    0, 0, 0, 0, 0, 0    // 2kHz~24kHz
};

// 最简整数 EQ 模拟算法（每 12 个采样一个周期，按段增益）
void ProcessEqPcmBuffer(short* pcm, int samples)
{
    for (int i = 0; i < samples; ++i)
    {
        int band = i % EQ_BANDS;  // 模拟每个频段一个采样
        int gain = eqGainInt[band];  // 百分比

        // 应用增益（[-100, 100] → [0, 2] 倍）
        int val = pcm[i];
        val = val * (100 + gain) / 100;

        // 限制范围 [-32768, 32767]
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;

        pcm[i] = (short)val;
        DbgPrint("[EQDBG] Band[%d] Gain = %d", i, eqGainInt[i]);
    }
}
