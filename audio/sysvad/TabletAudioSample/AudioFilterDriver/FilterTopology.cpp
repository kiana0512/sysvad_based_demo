// ----------------------------
// FilterTopology.cpp  (AVStream 最小安全骨架 / WDK 10.0.22621 通过)
// ----------------------------

#define INITGUID  // 仅在一个编译单元里定义，避免 GUID 重复定义

#include <ntddk.h>
#include <windef.h>
#include <ks.h>
#include <ksmedia.h>
#include "FilterTopology.h"

// ⚠ 不要在内核里包含 <mmreg.h>（会引入 GDI/位图相关，导致 “bmi 未知” 报错）
// 若工程其他处使用了 WAVE_FORMAT_PCM，这里本地兜底定义即可：
#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM 0x0001
#endif

#include "./AudioProcessing/ApplyAudioProcessing.h"

#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

// ============================================================================
// 1) 支持的数据格式（范围）：48kHz / 16bit / 2ch 的 PCM
//    注意：这里用“非 const”，避免某些工具链对 const 聚合初始化报 C2737。
//    同时也便于直接用 PKSDATARANGE 指针数组而无需 const_cast。
// ============================================================================

static KSDATARANGE_AUDIO g_PinDataRange = {
    {
        sizeof(KSDATARANGE_AUDIO),               // FormatSize
        0,                                       // Flags
        0x7FFFFFFF,                              // SampleSize (max)
        0,                                       // Reserved
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),   // MajorType
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),  // SubType
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) // Specifier
    },
    2,        // Channels
    48000,    // MinSampleRate
    48000,    // MaxSampleRate
    16        // MinBitsPerSample == MaxBitsPerSample
};

// DataRanges 字段类型在你的 WDK 中为 “const PKSDATARANGE *”。
// 其中 PKSDATARANGE == KSDATARANGE*。
// 因此我们构造“指向 KSDATARANGE 的指针数组”，再传其退化后的指针。
static const PKSDATARANGE g_PinDataRanges[] = {
    (PKSDATARANGE)&g_PinDataRange
};

// ============================================================================
// 2) Pin 处理回调（Render IN）：从 KSSTREAM_POINTER 取数据→调用你的处理→advance
// ============================================================================

extern "C"
NTSTATUS NTAPI PinWriteProcess(PKSPIN Pin)
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
    /* Disconnect     */ NULL
};

// ============================================================================
// 4) Filter 类别（数组形式；常用就是 KSCATEGORY_AUDIO）
// ============================================================================

static const GUID g_FilterCategories[] = { KSCATEGORY_AUDIO };

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

static const KSPIN_DESCRIPTOR_EX g_PinDescriptors[] =
{
    // ---- Render（IN → SINK）----
    {
        /* Dispatch         */ &g_PinDispatch,
        /* AutomationTable  */ NULL,

        /* PinDescriptor    */ {
            /* Interfaces       */ NULL,
            /* InterfaceCount   */ 0,
            /* Mediums          */ NULL,
            /* MediumCount      */ 0,

            /* DataRangesCount  */ ARRAYSIZE(g_PinDataRanges),
            /* DataRanges       */ g_PinDataRanges,  // 类型：const PKSDATARANGE* ✅

            /* DataFlow         */ KSPIN_DATAFLOW_IN,
            /* Communication    */ KSPIN_COMMUNICATION_SINK,
            /* Category         */ &KSCATEGORY_AUDIO,
            /* Name             */ NULL,
            /* Reserved         */ 0
        },

        /* Flags              */ KSPIN_FLAG_DO_NOT_INITIATE_PROCESSING,
        /* InstancesPossible  */ 1,
        /* InstancesNecessary */ 1,
        /* AllocatorFraming   */ NULL,
        /* IntersectHandler   */ NULL
    },

    // ---- Capture（OUT → SOURCE）----
    {
        /* Dispatch         */ &g_PinDispatch,
        /* AutomationTable  */ NULL,

        /* PinDescriptor    */ {
            /* Interfaces       */ NULL,
            /* InterfaceCount   */ 0,
            /* Mediums          */ NULL,
            /* MediumCount      */ 0,

            /* DataRangesCount  */ ARRAYSIZE(g_PinDataRanges),
            /* DataRanges       */ g_PinDataRanges,  // 类型：const PKSDATARANGE* ✅

            /* DataFlow         */ KSPIN_DATAFLOW_OUT,
            /* Communication    */ KSPIN_COMMUNICATION_SOURCE,
            /* Category         */ &KSCATEGORY_AUDIO,
            /* Name             */ NULL,
            /* Reserved         */ 0
        },

        /* Flags              */ KSPIN_FLAG_DO_NOT_INITIATE_PROCESSING,
        /* InstancesPossible  */ 1,
        /* InstancesNecessary */ 1,
        /* AllocatorFraming   */ NULL,
        /* IntersectHandler   */ NULL
    }
};

// ============================================================================
// 6) 对外导出的 Filter 描述符（与外部 extern 名称完全一致）
// ============================================================================

extern "C"
const KSFILTER_DESCRIPTOR FilterDescriptor = {
    /* Dispatch              */ NULL,                        // 如需 Filter 级回调再补
    /* AutomationTable       */ NULL,                        // 暂无属性/方法/事件
    /* Version               */ KSFILTER_DESCRIPTOR_VERSION,
    /* Flags                 */ 0,
    /* ReferenceGuid         */ &KSCATEGORY_AUDIO,           // 建议不要为 NULL
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
