// ----------------------------
// FilterTopology.cpp  (AVStream 最小安全骨架 / WDK 10.0.22621 通过)
// ----------------------------

#define INITGUID // 仅在一个编译单元里定义，避免 GUID 重复定义

#include <ntddk.h>
#include <windef.h>
#include <ks.h>
#include <ksmedia.h>
#include "FilterTopology.h"

// 不要在内核里包含 <mmreg.h>（会引入 GDI/位图相关，导致 “bmi 未知” 报错）
// 若工程其他处使用了 WAVE_FORMAT_PCM，这里本地兜底定义即可：
#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM 0x0001
#endif

#include "./AudioProcessing/ApplyAudioProcessing.h"

#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif
// ---- PIN 名称 GUID 兜底（有就用系统的；没有就自定义）----
#ifndef PINNAME_RENDER
// {F0E9B9E1-0EAC-4CA0-9D1E-8EAF7A7B8A10}  任意稳定 GUID 即可
DEFINE_GUID(PINNAME_RENDER,
            0xf0e9b9e1, 0xeac, 0x4ca0, 0x9d, 0x1e, 0x8e, 0xaf, 0x7a, 0x7b, 0x8a, 0x10);
#endif

#ifndef PINNAME_CAPTURE
// {B8B2D6C3-2F0D-4C7B-8B44-5C7A8B1DDE21}
DEFINE_GUID(PINNAME_CAPTURE,
            0xb8b2d6c3, 0x2f0d, 0x4c7b, 0x8b, 0x44, 0x5c, 0x7a, 0x8b, 0x1d, 0xde, 0x21);
#endif

// ============================================================================
// 1) 支持的数据格式（范围）：48kHz / 16bit / 2ch 的 PCM
//    注意：这里用“非 const”，避免某些工具链对 const 聚合初始化报 C2737。
//    同时也便于直接用 PKSDATARANGE 指针数组而无需 const_cast。
// ============================================================================

// 48kHz / 16bit / 2ch PCM
static KSDATARANGE_AUDIO g_PinDataRange = {
    {
        sizeof(KSDATARANGE_AUDIO),                        // FormatSize
        0,                                                // Flags
        0,                                                // SampleSize (0=可变帧，协商更稳)
        0,                                                // Reserved
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),            // MajorType
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),           // SubType
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) // Specifier
    },
    2,     // MaximumChannels
    16,    // MinimumBitsPerSample
    16,    // MaximumBitsPerSample
    48000, // MinimumSampleFrequency
    48000  // MaximumSampleFrequency
};

// DataRanges 字段类型在你的 WDK 中为 “const PKSDATARANGE *”。
// 其中 PKSDATARANGE == KSDATARANGE*。
// 因此我们构造“指向 KSDATARANGE 的指针数组”，再传其退化后的指针。
static const PKSDATARANGE g_PinDataRanges[] = {
    (PKSDATARANGE)&g_PinDataRange};

// ============================================================================
// 2) Pin 处理回调（Render IN）：从 KSSTREAM_POINTER 取数据→调用你的处理→advance
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

    BYTE *p = (BYTE *)sp->StreamHeader->Data;
    ULONG n = sp->StreamHeader->DataUsed;

    ApplyAudioProcessing(p, n);

    DbgPrint("[Filter] PinWriteProcess: processed %lu bytes\n", n);

    KsStreamPointerAdvance(sp);
    return STATUS_SUCCESS;
}

// ============================================================================
// 3) KSPIN_DISPATCH（注意 8 个槽位顺序）
//    Create, Close, Process, Reset, SetDataFormat, SetDeviceState, Connect, Disconnect
// ============================================================================

static const KSPIN_DISPATCH g_PinDispatch = {
    /* Create         */ NULL,
    /* Close          */ NULL,
    /* Process        */ PinWriteProcess,
    /* Reset          */ NULL,
    /* SetDataFormat  */ NULL,
    /* SetDeviceState */ NULL,
    /* Connect        */ NULL,
    /* Disconnect     */ NULL};

// ============================================================================
// 4) Filter 类别（数组形式；常用就是 KSCATEGORY_AUDIO）
// ============================================================================

static const GUID g_FilterCategories[] = {KSCATEGORY_AUDIO};

// ============================================================================
// 5) KSPIN_DESCRIPTOR_EX
//    ⚠ 你的 WDK 中 KSPIN_DESCRIPTOR 的字段顺序是：
//       Interfaces, InterfaceCount,
//       Mediums,    MediumCount,
//       DataRangesCount, DataRanges,   ←←← 这两个位置按此顺序
//       DataFlow,   Communication,
//       Category,   Name,
//       Reserved
//    若顺序/类型写错，会出现 “无法从 const KSDATARANGE*const* 转换为 const PKSDATARANGE*” 等 C2440。
// ============================================================================
// 预留 2 个 Pin（Render/Capture），先零初始化
static KSPIN_DESCRIPTOR_EX g_PinDescriptors[2];

static VOID SetupOnePin_Render(KSPIN_DESCRIPTOR_EX *ex)
{
    RtlZeroMemory(ex, sizeof(*ex));
    ex->Dispatch = &g_PinDispatch;
    ex->AutomationTable = NULL;
    ex->IntersectHandler = NULL; // ★ 关键：置 NULL
    ex->Flags = KSPIN_FLAG_DO_NOT_INITIATE_PROCESSING;
    ex->InstancesNecessary = 0;
    ex->InstancesPossible = 1;
    ex->AllocatorFraming = NULL;

    KSPIN_DESCRIPTOR *pd = &ex->PinDescriptor;
    pd->Interfaces = NULL;
    pd->InterfacesCount = 0;
    pd->Mediums = NULL;
    pd->MediumsCount = 0;
    pd->DataRangesCount = ARRAYSIZE(g_PinDataRanges);
    pd->DataRanges = g_PinDataRanges;
    pd->DataFlow = KSPIN_DATAFLOW_IN; // 你的拓扑语义：Render IN→SINK
    pd->Communication = KSPIN_COMMUNICATION_SINK;
    pd->Category = &KSCATEGORY_RENDER; // 或 &KSCATEGORY_AUDIO
    pd->Name = &PINNAME_RENDER;        // 不能为 NULL
    pd->Reserved = 0;
}

static VOID SetupOnePin_Capture(KSPIN_DESCRIPTOR_EX *ex)
{
    RtlZeroMemory(ex, sizeof(*ex));
    ex->Dispatch = &g_PinDispatch;
    ex->AutomationTable = NULL;
    ex->IntersectHandler = NULL;
    ex->Flags = KSPIN_FLAG_DO_NOT_INITIATE_PROCESSING;
    ex->InstancesNecessary = 0;
    ex->InstancesPossible = 1;
    ex->AllocatorFraming = NULL;

    KSPIN_DESCRIPTOR *pd = &ex->PinDescriptor;
    pd->Interfaces = NULL;
    pd->InterfacesCount = 0;
    pd->Mediums = NULL;
    pd->MediumsCount = 0;
    pd->DataRangesCount = ARRAYSIZE(g_PinDataRanges);
    pd->DataRanges = g_PinDataRanges;
    pd->DataFlow = KSPIN_DATAFLOW_OUT; // Capture OUT→SOURCE
    pd->Communication = KSPIN_COMMUNICATION_SOURCE;
    pd->Category = &KSCATEGORY_CAPTURE; // 或 &KSCATEGORY_AUDIO
    pd->Name = &PINNAME_CAPTURE;
    pd->Reserved = 0;
}

extern "C" void SetupPins(void)
{
    SetupOnePin_Render(&g_PinDescriptors[0]);
    SetupOnePin_Capture(&g_PinDescriptors[1]);

    // 可选：现场确认关键指针和值，便于排错
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
// 6) 对外导出的 Filter 描述符（与外部 extern 名称完全一致）
// ============================================================================

extern "C" KSFILTER_DESCRIPTOR FilterDescriptor = {
    /* Dispatch              */ NULL, // 如需 Filter 级回调再补
    /* AutomationTable       */ NULL, // 暂无属性/方法/事件
    /* Version               */ KSFILTER_DESCRIPTOR_VERSION,
    /* Flags                 */ 0,
    /* ReferenceGuid         */ &KSCATEGORY_AUDIO, // 建议不要为 NULL
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
    /* ComponentId           */ NULL};
