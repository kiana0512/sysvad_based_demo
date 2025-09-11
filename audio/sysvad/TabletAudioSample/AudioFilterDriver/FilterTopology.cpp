// ----------------------------
// FilterTopology.cpp  (AVStream 最小安全骨架 / WDK 10.0.22621 通过)
// ----------------------------

#define INITGUID

#include <ntddk.h>
#include <windef.h>
#include <ks.h>
#include <ksmedia.h>
#include "FilterTopology.h"

// ⚠ 不在内核里包含 <mmreg.h>，自己定义最小版 WAVEFORMATEX，避免 GDI 依赖。
#ifndef _WAVEFORMATEX_DEFINED_IN_KERNEL_
#define _WAVEFORMATEX_DEFINED_IN_KERNEL_
typedef struct _WAVEFORMATEX {
    USHORT wFormatTag;        // WAVE_FORMAT_PCM 等
    USHORT nChannels;         // 通道数
    ULONG  nSamplesPerSec;    // 采样率
    ULONG  nAvgBytesPerSec;   // 码率（采样率 * 块对齐）
    USHORT nBlockAlign;       // 块对齐: (bits/8)*channels
    USHORT wBitsPerSample;    // 位深
    USHORT cbSize;            // 扩展大小(PCM=0)
} WAVEFORMATEX, *PWAVEFORMATEX;
#endif

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM 0x0001
#endif

#include "./AudioProcessing/ApplyAudioProcessing.h"

#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef PINNAME_RENDER
// {F0E9B9E1-0EAC-4CA0-9D1E-8EAF7A7B8A10}
DEFINE_GUID(PINNAME_RENDER,
            0xf0e9b9e1, 0x0eac, 0x4ca0, 0x9d, 0x1e, 0x8e, 0xaf, 0x7a, 0x7b, 0x8a, 0x10);
#endif

#ifndef PINNAME_CAPTURE
// {B8B2D6C3-2F0D-4C7B-8B44-5C7A8B1DDE21}
DEFINE_GUID(PINNAME_CAPTURE,
            0xb8b2d6c3, 0x2f0d, 0x4c7b, 0x8b, 0x44, 0x5c, 0x7a, 0x8b, 0x1d, 0xde, 0x21);
#endif

// ============================================================================
// 1) 支持格式范围：PCM, 48kHz / 16bit / 2ch
//    ⚠ 使用静态全局，避免生命周期问题（被 KS 框架晚期访问时已释放）
// ============================================================================
static KSDATARANGE_AUDIO g_PinDataRange = {
    {
        sizeof(KSDATARANGE_AUDIO),
        0,                              // Flags
        0,                              // SampleSize (0=可变；也可填块对齐)
        0,                              // Reserved
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
    },
    2,      // MaximumChannels
    16,     // MinimumBitsPerSample
    16,     // MaximumBitsPerSample
    48000,  // MinimumSampleFrequency
    48000   // MaximumSampleFrequency
};

static const PKSDATARANGE g_PinDataRanges[] = {
    (PKSDATARANGE)&g_PinDataRange
};

// ============================================================================
// 2) 数据处理（Render IN → SINK）：非分页，避免 DISPATCH_LEVEL 触发页错
// ============================================================================
extern "C" NTSTATUS NTAPI PinWriteProcess(PKSPIN Pin)
{
    PKSSTREAM_POINTER sp =
        KsPinGetLeadingEdgeStreamPointer(Pin, KSSTREAM_POINTER_STATE_LOCKED);

    if (!sp || !sp->StreamHeader || !sp->StreamHeader->Data ||
        sp->StreamHeader->DataUsed == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    BYTE*  p = (BYTE*)sp->StreamHeader->Data;
    ULONG  n = sp->StreamHeader->DataUsed;

    // 你的数据处理（需保证非分页、无阻塞）
    ApplyAudioProcessing(p, n);

    DbgPrint("[Filter] PinWriteProcess: processed %lu bytes\n", n);

    KsStreamPointerAdvance(sp);
    return STATUS_SUCCESS;
}

// ============================================================================
// 3) KSPIN_DISPATCH（SetDataFormat 可后续实现缓存 WFX；先用默认）
// ============================================================================
static const KSPIN_DISPATCH g_PinDispatch = {
    /* Create         */ NULL,
    /* Close          */ NULL,
    /* Process        */ PinWriteProcess,
    /* Reset          */ NULL,
    /* SetDataFormat  */ NULL,
    /* SetDeviceState */ NULL,
    /* Connect        */ NULL,
    /* Disconnect     */ NULL
};

// ============================================================================
// 4) Filter 类别
// ============================================================================
static const GUID g_FilterCategories[] = { KSCATEGORY_AUDIO };

// ============================================================================
// 5) 交集处理（IntersectHandler）— 完整实现
//    - KS 在 Pin 创建/协商格式时调用此函数
//    - 这里返回 KSDATAFORMAT + WAVEFORMATEX（包含“长度探测”语义）
// ============================================================================

// 限制到驱动真实支持（与 g_PinDataRange 完全一致）
static VOID
SanitizeWaveFormat(_Inout_ USHORT* pCh, _Inout_ USHORT* pBits, _Inout_ ULONG* pRate)
{
    *pCh   = 2;
    *pBits = 16;
    *pRate = 48000;
}

// 将 KSDATAFORMAT + WAVEFORMATEX 打包到输出缓冲；支持“长度探测”流程
static NTSTATUS
FillKsDataFormatWfx(
    _Out_writes_bytes_(OutLen) PUCHAR OutBuf,
    _In_  ULONG  OutLen,
    _Out_ PULONG pResultLen,
    _In_  USHORT Channels,
    _In_  USHORT Bits,
    _In_  ULONG  Rate)
{
    const ULONG cb = sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEX);
    *pResultLen = cb;

    if (OutLen < cb)
        return STATUS_BUFFER_OVERFLOW;

    RtlZeroMemory(OutBuf, cb);

    // 1) KSDATAFORMAT
    PKSDATAFORMAT pks = (PKSDATAFORMAT)OutBuf;
    const USHORT blockAlign = (USHORT)(Channels * (Bits / 8));
    pks->FormatSize  = cb;
    pks->Flags       = 0;
    pks->SampleSize  = blockAlign; // PCM 建议给块对齐；给 0 也可
    pks->Reserved    = 0;
    pks->MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    pks->SubFormat   = KSDATAFORMAT_SUBTYPE_PCM;
    pks->Specifier   = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;

    // 2) WAVEFORMATEX 紧随其后
    PWAVEFORMATEX pwfx = (PWAVEFORMATEX)(OutBuf + sizeof(KSDATAFORMAT));
    pwfx->wFormatTag      = WAVE_FORMAT_PCM;
    pwfx->nChannels       = Channels;
    pwfx->nSamplesPerSec  = Rate;
    pwfx->wBitsPerSample  = Bits;
    pwfx->nBlockAlign     = blockAlign;
    pwfx->nAvgBytesPerSec = Rate * blockAlign;
    pwfx->cbSize          = 0;

    return STATUS_SUCCESS;
}

// ⚠ static 限定到本编译单元，避免“已被定义”链接冲突；不使用 alloc_text，所以不要求 C 链接。
static NTSTATUS NTAPI
KsIntersect_AudioPcmWfx(
    _In_opt_ PVOID         Context,
    _In_     PIRP          Irp,
    _In_     PKSP_PIN      Pin,
    _In_     PKSDATARANGE  DataRange,          // 对端提出的范围（常为 KSDATARANGE_AUDIO）
    _In_     PKSDATARANGE  MatchingDataRange,  // 我方范围（此处就是 g_PinDataRange）
    _In_     ULONG         OutputBufferLength, // 输出缓冲长度（可能为 0：探测）
    _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultLen)
             PVOID         ResultBuffer,
    _Out_    PULONG        ResultLen
)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Pin);

    // 交集通常在 PASSIVE_LEVEL 调用（如果你担心，也可以去掉 PAGED_CODE 约束）
    PAGED_CODE();

    if (!ResultLen)
        return STATUS_INVALID_PARAMETER;

    // 基础类型检查：AUDIO / PCM / WFX
    auto IsAudioPcmWfx = [](const KSDATARANGE* pr)->bool {
        return pr &&
            IsEqualGUIDAligned(pr->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) &&
            IsEqualGUIDAligned(pr->SubFormat,   KSDATAFORMAT_SUBTYPE_PCM) &&
            IsEqualGUIDAligned(pr->Specifier,   KSDATAFORMAT_SPECIFIER_WAVEFORMATEX);
    };

    if (!IsAudioPcmWfx(DataRange) || !IsAudioPcmWfx(MatchingDataRange))
        return STATUS_NO_MATCH;

    // 按 AUDIO 范围解析
    const KSDATARANGE_AUDIO* pIn  = (const KSDATARANGE_AUDIO*)DataRange;
    const KSDATARANGE_AUDIO* pOur = (const KSDATARANGE_AUDIO*)MatchingDataRange;

    // 计算交集（这版只接受 2ch/16bit/48k，但逻辑写完整，便于未来扩展）
    USHORT ch   = (USHORT)min(pOur->MaximumChannels, pIn->MaximumChannels);
    USHORT bits = 16;
    ULONG  rate = 48000;

    const ULONG minRate = max(pOur->MinimumSampleFrequency, pIn->MinimumSampleFrequency);
    const ULONG maxRate = min(pOur->MaximumSampleFrequency, pIn->MaximumSampleFrequency);
    if (!(minRate <= rate && rate <= maxRate)) {
        // 对端不覆盖 48k 的话，取交集区间的中点做兜底（尽量给一个合法值）
        const ULONG mid = minRate + (maxRate > minRate ? (maxRate - minRate) / 2 : 0);
        rate = mid ? mid : 48000;
    }

    // 最终限制到你真实支持的集合（与 g_PinDataRange 一致）
    SanitizeWaveFormat(&ch, &bits, &rate);

    // 输出为 KSDATAFORMAT+WAVEFORMATEX，遵循“长度探测→再写入”的标准流程
    NTSTATUS st = FillKsDataFormatWfx(
        (PUCHAR)ResultBuffer,
        OutputBufferLength,
        ResultLen,
        ch, bits, rate);

    if (st == STATUS_BUFFER_OVERFLOW) {
        DbgPrint("[KSIntersect] Need %lu bytes, got %lu\n", *ResultLen, OutputBufferLength);
        return st; // 探测阶段：告诉调用方需要的长度
    }
    if (!NT_SUCCESS(st)) {
        return st;
    }

    DbgPrint("[KSIntersect] OK: %hu ch, %hu-bit, %lu Hz, out=%lu bytes\n",
             ch, bits, rate, *ResultLen);
    return STATUS_SUCCESS;
}

// ============================================================================
// 6) Pin 描述符（Render/Capture 各一）
// ============================================================================
static KSPIN_DESCRIPTOR_EX g_PinDescriptors[2];

static VOID SetupOnePin_Render(KSPIN_DESCRIPTOR_EX* ex)
{
    RtlZeroMemory(ex, sizeof(*ex));
    ex->Dispatch         = &g_PinDispatch;
    ex->AutomationTable  = NULL;
    ex->IntersectHandler = KsIntersect_AudioPcmWfx; //  关键：提供交集处理（不再是 NULL）

    ex->Flags              = KSPIN_FLAG_DO_NOT_INITIATE_PROCESSING;
    ex->InstancesNecessary = 0;
    ex->InstancesPossible  = 1;
    ex->AllocatorFraming   = NULL; // 先不指定（不同 WDK 结构差异较大；最小骨架不需要）

    KSPIN_DESCRIPTOR* pd = &ex->PinDescriptor;
    pd->Interfaces       = NULL;
    pd->InterfacesCount  = 0;
    pd->Mediums          = NULL;
    pd->MediumsCount     = 0;
    pd->DataRangesCount  = ARRAYSIZE(g_PinDataRanges);
    pd->DataRanges       = g_PinDataRanges;
    pd->DataFlow         = KSPIN_DATAFLOW_IN;           // Render IN → SINK
    pd->Communication    = KSPIN_COMMUNICATION_SINK;
    pd->Category         = &KSCATEGORY_RENDER;          // 或 &KSCATEGORY_AUDIO
    pd->Name             = &PINNAME_RENDER;             // 不能为 NULL
    pd->Reserved         = 0;
}

static VOID SetupOnePin_Capture(KSPIN_DESCRIPTOR_EX* ex)
{
    RtlZeroMemory(ex, sizeof(*ex));
    ex->Dispatch         = &g_PinDispatch;
    ex->AutomationTable  = NULL;
    ex->IntersectHandler = KsIntersect_AudioPcmWfx;

    ex->Flags              = KSPIN_FLAG_DO_NOT_INITIATE_PROCESSING;
    ex->InstancesNecessary = 0;
    ex->InstancesPossible  = 1;
    ex->AllocatorFraming   = NULL;

    KSPIN_DESCRIPTOR* pd = &ex->PinDescriptor;
    pd->Interfaces       = NULL;
    pd->InterfacesCount  = 0;
    pd->Mediums          = NULL;
    pd->MediumsCount     = 0;
    pd->DataRangesCount  = ARRAYSIZE(g_PinDataRanges);
    pd->DataRanges       = g_PinDataRanges;
    pd->DataFlow         = KSPIN_DATAFLOW_OUT;          // Capture OUT → SOURCE
    pd->Communication    = KSPIN_COMMUNICATION_SOURCE;
    pd->Category         = &KSCATEGORY_CAPTURE;         // 或 &KSCATEGORY_AUDIO
    pd->Name             = &PINNAME_CAPTURE;
    pd->Reserved         = 0;
}

extern "C" void SetupPins(void)
{
    // ⚠ 必须在 KsCreateFilterFactory 之前调用（你当前流程已满足）
    SetupOnePin_Render(&g_PinDescriptors[0]);
    SetupOnePin_Capture(&g_PinDescriptors[1]);

    // 现场确认关键指针和值，便于排错
    DbgPrint("[PIN0] Dispatch=%p Intersect=%p DataRanges=%p Flow=%lu Comm=%lu\n",
             g_PinDescriptors[0].Dispatch,
             g_PinDescriptors[0].IntersectHandler,
             g_PinDescriptors[0].PinDescriptor.DataRanges,
             (ULONG)g_PinDescriptors[0].PinDescriptor.DataFlow,
             (ULONG)g_PinDescriptors[0].PinDescriptor.Communication);

    DbgPrint("[PIN1] Dispatch=%p Intersect=%p DataRanges=%p Flow=%lu Comm=%lu\n",
             g_PinDescriptors[1].Dispatch,
             g_PinDescriptors[1].IntersectHandler,
             g_PinDescriptors[1].PinDescriptor.DataRanges,
             (ULONG)g_PinDescriptors[1].PinDescriptor.DataFlow,
             (ULONG)g_PinDescriptors[1].PinDescriptor.Communication);
}

// ============================================================================
// 7) Filter 描述符（extern 符号名保持不变）
// ============================================================================
extern "C" KSFILTER_DESCRIPTOR FilterDescriptor = {
    /* Dispatch              */ NULL,
    /* AutomationTable       */ NULL,
    /* Version               */ KSFILTER_DESCRIPTOR_VERSION,
    /* Flags                 */ 0,
    /* ReferenceGuid         */ &KSCATEGORY_AUDIO,
    /* PinDescriptorsCount   */ ARRAYSIZE(g_PinDescriptors),
    /* PinDescriptorSize     */ sizeof(KSPIN_DESCRIPTOR_EX),
    /* PinDescriptors        */ g_PinDescriptors,
    /* CategoriesCount       */ ARRAYSIZE(g_FilterCategories),
    /* Categories            */ g_FilterCategories,
    /* NodeDescriptorsCount  */ 0,
    /* NodeDescriptorSize    */ 0,
    /* NodeDescriptors       */ NULL,
    /* ConnectionsCount      */ 0,
    /* Connections           */ NULL,
    /* ComponentId           */ NULL
};
