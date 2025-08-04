#include <ntddk.h>
#include "AudioEQControl.h"

// ---------------------------
// 全局变量定义（对应 .h 中 extern）
// ---------------------------

UCHAR g_EqTestBuffer[EQ_TEST_BUFFER_SIZE] = {0};
ULONG g_EqTestSize = 0;
inline int int_abs(int x)
{
    return (x < 0) ? -x : x;
}

EQControlParams g_EqParams = {0}; // 当前 EQ 参数
int eqGainInt[EQ_BANDS] = {0};    // 映射后的整型增益（×10）

// ---------------------------
// 初始化默认 EQ 设置
// ---------------------------

void EQControl_Init()
{
    RtlZeroMemory(&g_EqParams, sizeof(g_EqParams));
    g_EqParams.BandCount = MAX_EQ_BANDS;

    // 默认中心频率（可根据需要修改）
    int defaultFreqs[MAX_EQ_BANDS] = {
        60, 170, 310, 600, 1000, 3000,
        6000, 12000, 14000, 16000, 18000, 20000};

    for (int i = 0; i < MAX_EQ_BANDS; ++i)
    {
        g_EqParams.Bands[i].FrequencyHz = (float)defaultFreqs[i];
        g_EqParams.Bands[i].GainDb = 0.0f;
        g_EqParams.Bands[i].Q = 1.0f;
        eqGainInt[i] = 0;
    }

    DbgPrint("[EQ] EQControl_Init complete.\n");
}

// ---------------------------
// 设置 EQ 参数（从用户态传入）
// ---------------------------

void EQControl_SetParams(const EQControlParams *newParams)
{
    if (!newParams || newParams->BandCount > MAX_EQ_BANDS)
    {
        DbgPrint("[EQ] Invalid EQControlParams pointer or BandCount\n");
        return;
    }

    RtlCopyMemory(&g_EqParams, newParams, sizeof(EQControlParams));

    // 映射 GainDb 为整型值 eqGainInt（×10，范围 ±100）
    for (int i = 0; i < g_EqParams.BandCount; ++i)
    {
        float db = newParams->Bands[i].GainDb;
        int gain = (int)(db * 10.0f);
        //  内核态安全地打印浮点值（模拟 %.2f）
        int db10 = (int)(db * 10.0f); // 保留1位小数
        DbgPrint("[EQ] Band[%d] GainDb = %d.%01d -> gainInt = %d\n",
                 i,
                 db10 / 10,
                 int_abs(db10 % 10),
                 gain);

        // 限制范围 [-100, 100]
        if (gain > 100)
            gain = 100;
        if (gain < -100)
            gain = -100;

        eqGainInt[i] = gain;
    }

    DbgPrint("[EQ] SetParams updated. BandCount=%d, Gains[0]=%d,Gains[1]=%d, Gains[3]=%d\n",
             g_EqParams.BandCount, eqGainInt[0], eqGainInt[MAX_EQ_BANDS - 1]);
}

// ---------------------------
// 获取 EQ 参数（返回给用户态）
// ---------------------------

void EQControl_GetParams(EQControlParams *outParams)
{
    if (!outParams)
        return;

    RtlCopyMemory(outParams, &g_EqParams, sizeof(EQControlParams));
    DbgPrint("[EQ] GetParams called. BandCount=%d\n", g_EqParams.BandCount);
}