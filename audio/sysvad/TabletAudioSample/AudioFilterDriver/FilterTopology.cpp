// ----------------------------
// FilterTopology.cpp
// 支持新版本 WDK（10.0.22621）
// 实现一个基础音频 KS Filter：带输入输出 Pin，处理音频流
// ----------------------------

#define INITGUID // 避免重复定义 GUID
#undef KS_LIB
#undef KS_PROXY
#undef _KSUSER_

#include <ntddk.h>   // NT 驱动核心头文件
#include <windef.h>  // GUID、DWORD 等类型
#include <portcls.h> // Port Class 接口（定义 PKSPIN、KSPIN_DISPATCH 等）
#include <ks.h>      // KS 框架基本结构
#include <ksmedia.h> // KSDATARANGE_AUDIO、KSCATEGORY_AUDIO 等音频定义
// ============================================================================
// 数据格式定义：我们支持 PCM，48KHz，16bit，2声道
// ============================================================================

const KSDATARANGE_AUDIO PinDataRange = {
    {
        sizeof(KSDATARANGE_AUDIO),                        // 格式结构体大小
        0,                                                // Flags
        0x7FFFFFFF,                                       // 最大样本大小（任意）
        0,                                                // Reserved
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),            // 主类型：音频
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),           // 子类型：PCM
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) // WaveFormatEx 说明符
    },
    2,     // 通道数：2（立体声）
    48000, // 最小采样率：48KHz
    48000, // 最大采样率：48KHz
    16     // 位深度：16bit
};

// 替代 ARRAYSIZE 宏
#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))

// ============================================================================
// Pin 回调函数（新版 PFNKSPIN）：处理音频数据（输入Pin）
// ============================================================================

NTSTATUS NTAPI PinWriteProcess(PKSPIN Pin)
{
    // 获取当前的流指针（前沿指针，表示当前数据包）
    PKSSTREAM_POINTER streamPointer = KsPinGetLeadingEdgeStreamPointer(Pin, KSSTREAM_POINTER_STATE_LOCKED);
    if (!streamPointer || !streamPointer->StreamHeader || !streamPointer->StreamHeader->Data || streamPointer->StreamHeader->DataUsed == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // 获取数据指针与长度
    BYTE *pBuffer = (BYTE *)streamPointer->StreamHeader->Data;
    ULONG length = streamPointer->StreamHeader->DataUsed;

    //  调用你自己的 EQ / 音量处理函数（此处可替换）
    // ApplyAudioProcessing(pBuffer, length);

    DbgPrint("[Filter] PinWriteProcess: processed %lu bytes\n", length);

    // 移动流指针，表示已处理当前数据
    KsStreamPointerAdvance(streamPointer);

    return STATUS_SUCCESS;
}

// ============================================================================
// Pin 回调函数表（KSPIN_DISPATCH）：只定义了 Process 回调
// ============================================================================

const KSPIN_DISPATCH MyPinDispatch = {
    NULL,            // Create（可选）
    NULL,            // Close（可选）
    PinWriteProcess, // Process 回调（新版 PFNKSPIN：只接受 PKSPIN）
    NULL,            // Reset
    NULL,            // Flush
    NULL,            // SetFormat（可扩展）
    NULL,            // SetDeviceState（可扩展）
    NULL             // Connect
};

// ============================================================================
//  Pin 描述符数组：包含输入Pin（Render）和输出Pin（Capture）
// ============================================================================

const KSPIN_DESCRIPTOR_EX PinDescriptors[] = {
    {
        &MyPinDispatch,                // 回调函数表
        NULL,                          // AutomationTable（支持属性时可定义）
        0,                             // Version
        0,                             // Flags
        NULL, NULL,                    // Interface & InterfaceCount（可留空）
        1,                             // 支持格式数量
        (PKSDATARANGE *)&PinDataRange, // 支持的格式数组
        KSPIN_DATAFLOW_IN,             // 输入流（Render）
        KSPIN_COMMUNICATION_SINK,      // Sink（接收数据）
        NULL, NULL, NULL, NULL         // Framing & Allocator：默认
    },
    {&MyPinDispatch,
     NULL,
     0,
     0,
     NULL, NULL,
     1,
     (PKSDATARANGE *)&PinDataRange,
     KSPIN_DATAFLOW_OUT,         // 输出流（Capture）
     KSPIN_COMMUNICATION_SOURCE, // Source（发送数据）
     NULL, NULL, NULL, NULL}};

// ============================================================================
//  Filter 类别定义：声明为音频设备（KSCATEGORY_AUDIO）
// ============================================================================

const GUID KSCATEGORY_AUDIO_GUID = KSCATEGORY_AUDIO;
// ===========================================
// 定义 Filter 类别（只使用音频类别）
// ===========================================
const GUID* const Categories = &KSCATEGORY_AUDIO;

// ===========================================
// 初始化 Filter 总体描述符（KSFILTER_DESCRIPTOR）
// 顺序初始化，避免 C++20 限制
// ===========================================
const KSFILTER_DESCRIPTOR FilterDescriptor = {
    NULL,                        // Dispatch：Filter 的回调函数表（如 Create、Process 等），当前设为 NULL（未实现）
    NULL,                        // AutomationTable：属性自动处理表，用于定义自定义属性接口，这里不使用
    KSFILTER_DESCRIPTOR_VERSION, // Version：结构体版本号，通常设为 ((ULONG)-1) 表示当前版本
    0,                           // Flags：Filter 特性标志，常见的如 DISPATCH_LEVEL，当前无特殊标志

    NULL, // ReferenceGuid：引用 GUID，用于识别设备或 Filter，一般设为 NULL

    COUNT_OF(PinDescriptors),    // PinDescriptorsCount：定义的 Pin 数量
    sizeof(KSPIN_DESCRIPTOR_EX), // PinDescriptorSize：每个 Pin 描述符的字节大小（必须指定，否则系统不知道如何读取）
    PinDescriptors,              // PinDescriptors：指向 Pin 描述符数组的指针

    1,          // CategoriesCount：类别数量（这里只有一个音频类别）
    Categories, // Categories：类别 GUID 数组（这里只包含 KSCATEGORY_AUDIO）

    0,    // NodeDescriptorsCount：拓扑节点数量（当前未使用）
    0,    // NodeDescriptorSize：每个 Node 描述符大小（未使用设为 0）
    NULL, // NodeDescriptors：节点数组指针，未使用设为 NULL

    0,    // ConnectionsCount：连接关系数量（用于描述 Pin 与 Node 的内部连线）
    NULL, // Connections：连接结构体数组，当前未定义连接关系

    NULL // ComponentId：组件标识（用于设备唯一性标识，一般用于硬件设备），当前设为 NULL
};
