#include <ntddk.h>
#include "../AudioVolumeControl/AudioEQControl.h"
#include "../AudioVolumeControl/AudioVolumeControl.h"
#include "DriverEntry.h"
#include <wchar.h>
#include <windef.h>  // 包含 BOOL、DWORD 等定义
#include <ks.h>      // KS 框架基本结构
#include <ksmedia.h> // KSDATARANGE_AUDIO、KSCATEGORY_AUDIO 等音频定义
#include "FilterTopology.h"
#include "KsHelper.h"
#include "KsSanityCheck.h"
#include <ntstrsafe.h> //  安全字符串：RtlStringCchPrintfW / RTL_NUMBER_OF

extern "C" KSFILTER_DESCRIPTOR FilterDescriptor; // 仅声明，不能写等号
extern const GUID KSCATEGORY_AUDIO_GUID;         // 你定义的音频类别 GUID（就是 KSCATEGORY_AUDIO）
// ===== AVStream 兼容兜底：有的 WDK 不公开 KSDEVICE_HEADER =====
#ifndef PKSDEVICE_HEADER
typedef PVOID PKSDEVICE_HEADER;
#endif
// ===== KS 设备层描述子（用于把 KS Header 绑定到你的 FDO）=====
static const KSDEVICE_DISPATCH g_DeviceDispatch = {
    /*Add        */ NULL,
    /*Start      */ NULL,
    /*PostStart  */ NULL,
    /*Remove     */ NULL,
    /*QueryStop  */ NULL,
    /*CancelStop */ NULL,
    /*Stop       */ NULL,
    /*QueryRemove*/ NULL,
    /*CancelRemove*/ NULL};

static const KSDEVICE_DESCRIPTOR g_KsDeviceDescriptor = {
    &g_DeviceDispatch,
    0, NULL, // FilterDescriptorsCount / FilterDescriptors（我们用 Factory 注册，这里留 0/NULL）
    0, NULL  // ComponentsCount / Components
};

// 手动补充声明（C 接口）
extern "C" NTSTATUS KsRegisterFilter(
    PDEVICE_OBJECT DeviceObject,
    const KSFILTER_DESCRIPTOR *Descriptor,
    const GUID *Category);
#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))
extern "C" PDEVICE_OBJECT NTAPI IoGetLowerDeviceObject(PDEVICE_OBJECT DeviceObject);
// ============================================================================
// 控制设备对象（仅一个）
// ============================================================================
PDEVICE_OBJECT g_ControlDeviceObject = nullptr;

// 控制设备名和符号链接名
UNICODE_STRING g_DeviceName = RTL_CONSTANT_STRING(L"\\Device\\kiana_AudioControl");
UNICODE_STRING g_SymLinkName = RTL_CONSTANT_STRING(L"\\DosDevices\\kiana_AudioControl");

// ============================================================================
// 设备扩展结构体（每个 KS Filter 实例）—— 修正版
// 关键点：KSDEVICE_HEADER 必须是“内嵌且为首字段”，KS 框架通过它定位 KSDEVICE
// ============================================================================

#include <stddef.h> // for offsetof

typedef struct _FILTER_DEVICE_EXTENSION
{
    //
    // *** MUST BE FIRST ***
    // 由 KS 在 KsInitializeDevice 成功时初始化；绝对不要把它改成指针或手工赋值！
    //
    KSDEVICE_HEADER KsDeviceHeader;

    // 设备栈关系
    PDEVICE_OBJECT LowerDeviceObject; // 下层 FDO
    PDEVICE_OBJECT Pdo;               // 物理设备对象（PDO）

    // 运行/配置
    BOOLEAN EnableAudioProcessing; // 是否目标设备（匹配到 USB\VID_0A67&PID_30A2&MI_00）
    LONG DeviceIndex;              // 实例序号
    BOOLEAN KsInited;              // 是否已调用过 KsInitializeDevice
    BOOLEAN Removed;               // 是否已处理过 REMOVE

    // KS 相关
    PKSFILTERFACTORY FilterFactory; // 注册成功后保存，用于 Stop/Remove 清理

    // 可选：显示用名字 / 符号链接
    UNICODE_STRING DeviceName;   // 我们创建的 \Device\AudioFilter_N
    BOOLEAN SymbolicLinkCreated; // \DosDevices\KianaAudioEQ_N 是否已创建
} FILTER_DEVICE_EXTENSION, *PFILTER_DEVICE_EXTENSION;

// 断言：KSDEVICE_HEADER 必须在偏移 0（即作为首字段）
C_ASSERT(offsetof(FILTER_DEVICE_EXTENSION, KsDeviceHeader) == 0);

// ============================================================================
// DriverEntry：驱动程序入口，注册分发函数并创建控制设备
// ============================================================================
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[AFD] DriverEntry: drvObj=%p\n", DriverObject);

    // **** 关键变化：不要安装 KS 的全局派发处理器（会与分流冲突/吞掉 KS IRP）****
    // // KsSetMajorFunctionHandler(DriverObject, IRP_MJ_CREATE);
    // // KsSetMajorFunctionHandler(DriverObject, IRP_MJ_CLOSE);
    // // KsSetMajorFunctionHandler(DriverObject, IRP_MJ_DEVICE_CONTROL);
    // // KsSetMajorFunctionHandler(DriverObject, IRP_MJ_READ);
    // // KsSetMajorFunctionHandler(DriverObject, IRP_MJ_WRITE);
    // // KsSetMajorFunctionHandler(DriverObject, IRP_MJ_FLUSH_BUFFERS);
    // // KsSetMajorFunctionHandler(DriverObject, IRP_MJ_QUERY_SECURITY);
    // // KsSetMajorFunctionHandler(DriverObject, IRP_MJ_SET_SECURITY);
    // // KsSetMajorFunctionHandler(DriverObject, IRP_MJ_POWER);
    // // KsSetMajorFunctionHandler(DriverObject, IRP_MJ_SYSTEM_CONTROL);

    // ① 保留你原有 AddDevice/Unload
    DriverObject->DriverExtension->AddDevice = AddDevice;
    DriverObject->DriverUnload = DriverUnload;

    // ② PnP/Power
    DriverObject->MajorFunction[IRP_MJ_PNP] = AudioFilter_PnPDispatch;
    // 你已有 Power 分发就保留；若没有，可透传到下层
    // DriverObject->MajorFunction[IRP_MJ_POWER] = AudioFilter_PowerDispatch;

    // ③ 关键：三大派发改为“控制/过滤分流”包装器
    DriverObject->MajorFunction[IRP_MJ_CREATE] = FilterOrControl_DispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = FilterOrControl_DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = FilterOrControl_DispatchDeviceControl;

    // -----------------------------
    // 创建控制设备（用于应用层通信）
    // -----------------------------
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0, // 无设备扩展
        &g_DeviceName,
        FILE_DEVICE_KS,
        0,
        FALSE,
        &g_ControlDeviceObject);

    if (!NT_SUCCESS(status))
    {
        DbgPrint("[kiana] IoCreateDevice(ctrl) failed: 0x%08X\n", status);
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymLinkName, &g_DeviceName);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[kiana] IoCreateSymbolicLink(ctrl) failed: 0x%08X\n", status);
        IoDeleteDevice(g_ControlDeviceObject);
        g_ControlDeviceObject = nullptr;
        return status;
    }

    // 初始化你的控制模块（保持不变）
    EQControl_Init();
    VolumeControl_Init();

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[AFD] DriverEntry: done\n");
    return STATUS_SUCCESS;
}
// ===== 工具：判断控制设备 / 透传到下层 =====
static __forceinline bool IsControlDevice(PDEVICE_OBJECT dev)
{
    return dev == g_ControlDeviceObject;
}
static __forceinline NTSTATUS PassDown(PDEVICE_OBJECT dev, PIRP irp)
{
    auto ext = reinterpret_cast<PFILTER_DEVICE_EXTENSION>(dev->DeviceExtension);
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(ext->LowerDeviceObject, irp);
}

// ===== 分流包装器：CREATE/CLOSE/DEVICE_CONTROL =====
NTSTATUS FilterOrControl_DispatchCreate(PDEVICE_OBJECT dev, PIRP irp)
{
    if (IsControlDevice(dev))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[AFD][CTRL] CREATE\n");
        return AudioVolumeControl_DispatchCreate(dev, irp); // 你原有实现
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[AFD][FILT] CREATE -> pass\n");
    return PassDown(dev, irp); // 过滤设备：透明
}

NTSTATUS FilterOrControl_DispatchClose(PDEVICE_OBJECT dev, PIRP irp)
{
    if (IsControlDevice(dev))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[AFD][CTRL] CLOSE\n");
        return AudioVolumeControl_DispatchClose(dev, irp);
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[AFD][FILT] CLOSE -> pass\n");
    return PassDown(dev, irp);
}

NTSTATUS FilterOrControl_DispatchDeviceControl(PDEVICE_OBJECT dev, PIRP irp)
{
    auto irpSp = IoGetCurrentIrpStackLocation(irp);
    ULONG code = irpSp->Parameters.DeviceIoControl.IoControlCode;

    if (IsControlDevice(dev))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "[AFD][CTRL] IOCTL=0x%08X\n", code);
        return AudioVolumeControl_DispatchIoControl(dev, irp);
    }

    // 过滤设备：**关键保证** —— 不拦/不改任何 IOCTL_KS_*，100% 透传
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL,
               "[AFD][FILT] IOCTL=0x%08X -> pass\n", code);
    return PassDown(dev, irp);
}

// ============================================================================
// AddDevice：为每个被枚举的 USB 音频设备创建一个 KS Filter 实例
// ============================================================================
NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
    DbgPrint("[AFD] AddDevice: PDO=%p\n", PhysicalDeviceObject);

    // 1) 生成唯一设备名
    static LONG deviceCount = 0;
    LONG index = InterlockedIncrement(&deviceCount);

    WCHAR nameBuffer[64] = {0};
    RtlStringCchPrintfW(nameBuffer, RTL_NUMBER_OF(nameBuffer),
                        L"\\Device\\AudioFilter_%d", index);
    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, nameBuffer);

    // 2) 创建设备对象（作为上层过滤 FDO）
    PDEVICE_OBJECT filterDevice = nullptr;
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        sizeof(FILTER_DEVICE_EXTENSION),
        &deviceName,
        FILE_DEVICE_KS, // 保持你之前的类型；若想更“中性”可用 FILE_DEVICE_UNKNOWN
        0,
        FALSE,
        &filterDevice);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[AFD] IoCreateDevice failed: 0x%08X\n", status);
        return status;
    }

    // 3) 附着到设备栈
    PDEVICE_OBJECT lowerDevice = nullptr;
    status = IoAttachDeviceToDeviceStackSafe(filterDevice, PhysicalDeviceObject, &lowerDevice);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[AFD] IoAttachDeviceToDeviceStackSafe failed: 0x%08X\n", status);
        IoDeleteDevice(filterDevice);
        return status;
    }

    // 4) 初始化扩展
    auto ext = reinterpret_cast<PFILTER_DEVICE_EXTENSION>(filterDevice->DeviceExtension);
    RtlZeroMemory(ext, sizeof(*ext));
    ext->LowerDeviceObject = lowerDevice;
    ext->Pdo = PhysicalDeviceObject;
    ext->DeviceIndex = index;

    // 【可选】探测硬件 ID，只做记录，不决定“是否装”
    // 你可以在这里把 MI_00/MI_01 的判定写进 ext->EnableAudioProcessing 作为功能开关，
    // 但无论结果如何都不要返回失败。
    ext->EnableAudioProcessing = FALSE; // 先默认 FALSE，仅作为占位标志
    {
        ULONG len = 0;
        NTSTATUS st = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyHardwareID, 0, NULL, &len);
        if (st == STATUS_BUFFER_TOO_SMALL && len)
        {
            PWSTR ids = (PWSTR)ExAllocatePoolWithTag(PagedPool, len, 'HIDs');
            if (ids)
            {
                st = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyHardwareID, len, ids, &len);
                if (NT_SUCCESS(st))
                {
                    for (PWSTR p = ids; *p; p += wcslen(p) + 1)
                    {
                        DbgPrint("[AFD] HWID: %ws\n", p);
                        if (wcsstr(p, L"USB\\VID_0A67") && wcsstr(p, L"PID_30A2") && wcsstr(p, L"MI_00"))
                        {
                            ext->EnableAudioProcessing = TRUE; // 仅作为内部标记；不影响安装成败
                        }
                    }
                }
                ExFreePool(ids);
            }
        }
    }

    // 5) 保存设备名到扩展（可保留）
    ext->DeviceName.Buffer = (PWSTR)ExAllocatePoolWithTag(PagedPool,
                                                          deviceName.Length + sizeof(WCHAR), 'devn');
    if (ext->DeviceName.Buffer)
    {
        RtlCopyMemory(ext->DeviceName.Buffer, deviceName.Buffer, deviceName.Length);
        ext->DeviceName.Buffer[deviceName.Length / sizeof(WCHAR)] = L'\0';
        ext->DeviceName.Length = deviceName.Length;
        ext->DeviceName.MaximumLength = deviceName.Length + sizeof(WCHAR);
    }

    // 6) 创建符号链接（\DosDevices\KianaAudioEQ_%d）
    WCHAR linkNameBuffer[64] = {0};
    RtlStringCchPrintfW(linkNameBuffer, RTL_NUMBER_OF(linkNameBuffer),
                        L"\\DosDevices\\KianaAudioEQ_%d", index);
    UNICODE_STRING devLink;
    RtlInitUnicodeString(&devLink, linkNameBuffer);
    status = IoCreateSymbolicLink(&devLink, &deviceName);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[AFD] IoCreateSymbolicLink failed: 0x%08X\n", status);
        // 失败也不返回错误——保持设备可启动。可在 REMOVE 时尝试删除同名链接即可。
    }
    else
    {
        DbgPrint("[AFD] Symlink %ws -> %ws\n", linkNameBuffer, deviceName.Buffer);
    }

    // 7) 继承分页能力/IO 标志
    filterDevice->Flags |= DO_BUFFERED_IO;
    if (ext->LowerDeviceObject->Flags & DO_POWER_PAGABLE)
        filterDevice->Flags |= DO_POWER_PAGABLE;
    filterDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    DbgPrint("[AFD] AddDevice success: %wZ\n", &deviceName);
    return STATUS_SUCCESS;
}

// ============================================================================
// DriverUnload：驱动卸载，仅卸载控制设备（Filter 设备已由 PNP 移除）
// ============================================================================
void DriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    DbgPrint("==[KSFilter] DriverUnload Called==\n");

    // 删除符号链接
    IoDeleteSymbolicLink(&g_SymLinkName);

    // 删除控制设备
    if (g_ControlDeviceObject)
    {
        IoDeleteDevice(g_ControlDeviceObject);
        g_ControlDeviceObject = nullptr;
    }
    DbgPrint("==[KSFilter] DriverUnload complete==\n");
}
// ============================================================================
// AudioFilter_PnPDispatch：处理即插即用 IRP（包含 START / REMOVE 等）—— 修正版
// 要点：
//  1) START：先下发 → 成功后再 KsInitializeDevice（只做一次）→ 再 KsCreateFilterFactory
//  2) KsAcquireDevice 之前，必须通过 KsGetDeviceForDeviceObject 得到合法 PKSDEVICE
//  3) REMOVE：删除 FilterFactory、符号链接，Detach + Delete；不要手动“释放”内嵌 KSDEVICE_HEADER
// ============================================================================

//
// 完成例程：用于把下层 START 的完成“同步化”，让我们能在本层继续处理并由本层完成 IRP
//
static NTSTATUS
StartComplete(
    PDEVICE_OBJECT DevObj,
    PIRP Irp,
    PVOID Context)
{
    UNREFERENCED_PARAMETER(DevObj);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    // 把 IRP 的控制权交还给上层（本驱动层）来完成，所以下面会 IoCompleteRequest
    return STATUS_MORE_PROCESSING_REQUIRED;
}

//
// ===================== PnP 分发（修正版）=====================
// 关键改动：
// 1) IRP_MN_START_DEVICE：
//    - 先同步等待下层 START 完成
//    - 再调用 KsInitializeDevice(FDO, PDO, Lower, &g_KsDeviceDescriptor)
//    - 再用 KsGetDeviceForDeviceObject() 拿到 PKSDEVICE 并 KsAcquireDevice() 包裹 KsCreateFilterFactory()
// 2) IRP_MN_REMOVE_DEVICE：
//    - 先把 IRP 下发
//    - 删除 FilterFactory；删除符号链接；Detach & Delete
//    - 不要去“释放”内嵌的 KSDEVICE_HEADER（没有这个 Free，且它在扩展里是内嵌对象）
// 3) 其他 PnP：直通
//
NTSTATUS AudioFilter_PnPDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    auto devExt = reinterpret_cast<PFILTER_DEVICE_EXTENSION>(DeviceObject->DeviceExtension);
    auto irpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (irpSp->MinorFunction)
    {
    case IRP_MN_START_DEVICE:
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "[AFD][PNP] START -> pass (no KS init/factory)\n");
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(devExt->LowerDeviceObject, Irp);

    case IRP_MN_STOP_DEVICE:
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "[AFD][PNP] STOP -> pass\n");
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(devExt->LowerDeviceObject, Irp);

    case IRP_MN_QUERY_CAPABILITIES:
    case IRP_MN_QUERY_PNP_DEVICE_STATE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
    case IRP_MN_SURPRISE_REMOVAL:
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "[AFD][PNP] Minor=0x%02X -> pass\n", irpSp->MinorFunction);
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(devExt->LowerDeviceObject, Irp);

    case IRP_MN_REMOVE_DEVICE:
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "[AFD][PNP] REMOVE: pass then detach/delete\n");

        // 先让下层做自己的清理
        IoSkipCurrentIrpStackLocation(Irp);
        NTSTATUS st = IoCallDriver(devExt->LowerDeviceObject, Irp);

        // 删除每实例符号链接（AddDevice 中创建的）
        WCHAR linkName[64] = {0};
        RtlStringCchPrintfW(linkName, RTL_NUMBER_OF(linkName),
                            L"\\DosDevices\\KianaAudioEQ_%ld", devExt->DeviceIndex);
        UNICODE_STRING usLink;
        RtlInitUnicodeString(&usLink, linkName);
        NTSTATUS dl = IoDeleteSymbolicLink(&usLink);
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   NT_SUCCESS(dl) ? DPFLTR_INFO_LEVEL : DPFLTR_ERROR_LEVEL,
                   "[AFD][PNP] IoDeleteSymbolicLink(%wZ) -> 0x%08X\n", &usLink, dl);

        // 释放 AddDevice 分配的名字缓冲（如有）
        if (devExt->DeviceName.Buffer)
        {
            ExFreePool(devExt->DeviceName.Buffer);
            devExt->DeviceName.Buffer = nullptr;
            devExt->DeviceName.Length = devExt->DeviceName.MaximumLength = 0;
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[AFD][PNP] Freed DeviceName\n");
        }

        // 脱附并删除本层设备
        if (devExt->LowerDeviceObject)
        {
            IoDetachDevice(devExt->LowerDeviceObject);
            devExt->LowerDeviceObject = nullptr;
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[AFD][PNP] Detached from lower\n");
        }
        IoDeleteDevice(DeviceObject);
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[AFD][PNP] Deleted self\n");
        return st;
    }

    default:
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(devExt->LowerDeviceObject, Irp);
    }
}

// 向下遍历设备栈，返回最底层设备（PDO）
PDEVICE_OBJECT GetLowestDevice(PDEVICE_OBJECT startDevice)
{
    PDEVICE_OBJECT current = startDevice;
    PDEVICE_OBJECT next = NULL;

    ObReferenceObject(current);

    while ((next = IoGetLowerDeviceObject(current)) != NULL)
    {
        ObDereferenceObject(current); // 释放旧设备
        current = next;
        ObReferenceObject(current); // 引用新设备
    }

    return current;
}
NTSTATUS QueryHardwareIdWithIrp(PDEVICE_OBJECT targetDevice, WCHAR *buffer, ULONG bufferLength)
{
    KEVENT event;
    PIRP irp;
    IO_STATUS_BLOCK ioStatus = {0};
    NTSTATUS status;

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_PNP,
        targetDevice,
        NULL,
        0,
        NULL,
        &event,
        &ioStatus);

    if (!irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    PIO_STACK_LOCATION irpSp = IoGetNextIrpStackLocation(irp);
    irpSp->MinorFunction = IRP_MN_QUERY_ID;
    irpSp->Parameters.QueryId.IdType = BusQueryHardwareIDs;
    irpSp->MajorFunction = IRP_MJ_PNP;

    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;

    // 标记我们是系统调用
    irp->Flags |= IRP_SYNCHRONOUS_API;

    status = IoCallDriver(targetDevice, irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }

    if (NT_SUCCESS(status))
    {
        PWCHAR idList = (PWCHAR)ioStatus.Information;
        if (idList)
        {
            // 拷贝第一个 ID 到传入的 buffer
            wcsncpy(buffer, idList, bufferLength / sizeof(WCHAR));
            ExFreePool(idList);
        }
    }

    return status;
}

//
// ======== Minimal & WDK-old-safe KS Filter Sanity Check ========
// 仅依赖 <ntddk.h>, <ks.h>, <ksmedia.h>，无 C++11/CRT；适配把字段放在 PinDescriptor 的头文件布局。
// 使用：KsCreateFilterFactory 之前调用 SanityCheckKsFilterDesc(&FilterDescriptor)
//

#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#endif
#ifndef STATUS_INVALID_PARAMETER_1
#define STATUS_INVALID_PARAMETER_1 ((NTSTATUS)0xC00000EFL)
#endif
#ifndef STATUS_INVALID_PARAMETER_2
#define STATUS_INVALID_PARAMETER_2 ((NTSTATUS)0xC00000F0L)
#endif
#ifndef STATUS_INVALID_PARAMETER_3
#define STATUS_INVALID_PARAMETER_3 ((NTSTATUS)0xC00000F1L)
#endif
#ifndef STATUS_INVALID_PARAMETER_4
#define STATUS_INVALID_PARAMETER_4 ((NTSTATUS)0xC00000F2L)
#endif

#define KSFCHK_PREFIX "[KSF][CHK] "

// 兼容 GUID* / GUID& / NULL 的比较（避免 IsEqualGUID 的签名差异）
static __inline BOOLEAN EqualGuidPtr(const GUID *a, const GUID *b)
{
    if (a == NULL || b == NULL)
        return FALSE;
    return RtlEqualMemory(a, b, sizeof(GUID)) ? TRUE : FALSE;
}

#define KSFCHK_FAIL(_st, _fmt, _a1)        \
    do                                     \
    {                                      \
        DbgPrint(KSFCHK_PREFIX _fmt, _a1); \
        return (_st);                      \
    } while (0)
#define KSFCHK_FAIL2(_st, _fmt, _a1, _a2)       \
    do                                          \
    {                                           \
        DbgPrint(KSFCHK_PREFIX _fmt, _a1, _a2); \
        return (_st);                           \
    } while (0)

static NTSTATUS _CheckDataRanges_EX(const KSPIN_DESCRIPTOR_EX *ex, ULONG pinIndex)
{
    const KSPIN_DESCRIPTOR *pd = &ex->PinDescriptor;

    if (pd->DataRangesCount == 0)
    {
        KSFCHK_FAIL2(STATUS_INVALID_PARAMETER_3, "Pin[%lu]: DataRangesCount=0\n", pinIndex, 0);
    }
    if (pd->DataRanges == NULL)
    {
        KSFCHK_FAIL2(STATUS_INVALID_PARAMETER_3, "Pin[%lu]: DataRanges=NULL\n", pinIndex, 0);
    }

    // 逐项检查基本尺寸
    {
        ULONG i;
        for (i = 0; i < pd->DataRangesCount; ++i)
        {
            PKSDATARANGE p = pd->DataRanges[i];
            if (p == NULL)
            {
                KSFCHK_FAIL2(STATUS_INVALID_PARAMETER_3, "Pin[%lu]: DataRanges[%lu]=NULL\n", pinIndex, i);
            }
            if (p->FormatSize < sizeof(KSDATARANGE))
            {
                DbgPrint(KSFCHK_PREFIX "Pin[%lu]: DataRanges[%lu].FormatSize too small: %lu\n",
                         pinIndex, i, (ULONG)p->FormatSize);
                return STATUS_INVALID_PARAMETER_3;
            }
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS _CheckOnePin_EX(const KSPIN_DESCRIPTOR_EX *ex, ULONG pinIndex)
{
    if (ex == NULL)
    {
        KSFCHK_FAIL2(STATUS_INVALID_PARAMETER_2, "Pin[%lu]: descriptor=NULL\n", pinIndex, 0);
    }

    const KSPIN_DESCRIPTOR *pd = (const KSPIN_DESCRIPTOR *)&ex->PinDescriptor;

    // 常见字段都在 PinDescriptor 里：Category/Name/Communication/DataRanges...
    if (pd->Category == NULL)
    {
        KSFCHK_FAIL2(STATUS_INVALID_PARAMETER_2, "Pin[%lu]: Category=NULL\n", pinIndex, 0);
    }
    if (pd->Name == NULL)
    {
        KSFCHK_FAIL2(STATUS_INVALID_PARAMETER_2, "Pin[%lu]: Name=NULL\n", pinIndex, 0);
    }

    // DataRanges
    {
        NTSTATUS st = _CheckDataRanges_EX(ex, pinIndex);
        if (!NT_SUCCESS(st))
            return st;
    }

    // 通信/流向的“合理性”提示（不强制）
    if (pd->Communication == KSPIN_COMMUNICATION_SINK && pd->DataFlow != KSPIN_DATAFLOW_IN)
    {
        DbgPrint(KSFCHK_PREFIX "Pin[%lu]: Communication=SINK but DataFlow!=IN (warn)\n", pinIndex);
    }
    if (pd->Communication == KSPIN_COMMUNICATION_SOURCE && pd->DataFlow != KSPIN_DATAFLOW_OUT)
    {
        DbgPrint(KSFCHK_PREFIX "Pin[%lu]: Communication=SOURCE but DataFlow!=OUT (warn)\n", pinIndex);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS _CheckConnection(const KSFILTER_DESCRIPTOR *d, const KSTOPOLOGY_CONNECTION *c, ULONG idx)
{
    if (c == NULL)
    {
        KSFCHK_FAIL2(STATUS_INVALID_PARAMETER_4, "Connection[%lu]=NULL\n", idx, 0);
    }

    // KSFILTER_NODE 表示 Filter 级别的 Pin，下标需 < PinDescriptorsCount
    if (c->FromNode == KSFILTER_NODE)
    {
        if (c->FromNodePin >= d->PinDescriptorsCount)
        {
            KSFCHK_FAIL2(STATUS_INVALID_PARAMETER_4, "Conn[%lu]: FromPin=%lu OOR\n", idx, (ULONG)c->FromNodePin);
        }
    }
    else
    {
        if (d->NodeDescriptorsCount == 0 || c->FromNode >= d->NodeDescriptorsCount)
        {
            KSFCHK_FAIL2(STATUS_INVALID_PARAMETER_4, "Conn[%lu]: FromNode=%lu OOR\n", idx, (ULONG)c->FromNode);
        }
    }

    if (c->ToNode == KSFILTER_NODE)
    {
        if (c->ToNodePin >= d->PinDescriptorsCount)
        {
            KSFCHK_FAIL2(STATUS_INVALID_PARAMETER_4, "Conn[%lu]: ToPin=%lu OOR\n", idx, (ULONG)c->ToNodePin);
        }
    }
    else
    {
        if (d->NodeDescriptorsCount == 0 || c->ToNode >= d->NodeDescriptorsCount)
        {
            KSFCHK_FAIL2(STATUS_INVALID_PARAMETER_4, "Conn[%lu]: ToNode=%lu OOR\n", idx, (ULONG)c->ToNode);
        }
    }

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS SanityCheckKsFilterDesc(const KSFILTER_DESCRIPTOR *d)
{
    if (d == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // Pins
    if (d->PinDescriptorsCount == 0 || d->PinDescriptors == NULL)
    {
        DbgPrint(KSFCHK_PREFIX "Pins missing (count=%lu, ptr=%p)\n",
                 (ULONG)d->PinDescriptorsCount, d->PinDescriptors);
        return STATUS_INVALID_PARAMETER_1;
    }
    if (d->PinDescriptorSize != sizeof(KSPIN_DESCRIPTOR_EX))
    {
        DbgPrint(KSFCHK_PREFIX "PinDescriptorSize mismatch: %lu vs %lu\n",
                 (ULONG)d->PinDescriptorSize, (ULONG)sizeof(KSPIN_DESCRIPTOR_EX));
        return STATUS_INVALID_PARAMETER_1;
    }

    // Categories（可空，做弱提示）
    if (d->CategoriesCount != 0 && d->Categories == NULL)
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    // Nodes（可为 0；若非 0 则数组不可空且尺寸匹配）
    if (d->NodeDescriptorsCount != 0)
    {
        if (d->NodeDescriptors == NULL)
        {
            return STATUS_INVALID_PARAMETER_2;
        }
        if (d->NodeDescriptorSize != sizeof(KSNODE_DESCRIPTOR))
        {
            DbgPrint(KSFCHK_PREFIX "NodeDescriptorSize mismatch\n");
            return STATUS_INVALID_PARAMETER_2;
        }
    }

    // Connections（可为 0；若非 0 则数组不可空）
    if (d->ConnectionsCount != 0 && d->Connections == NULL)
    {
        return STATUS_INVALID_PARAMETER_4;
    }

    // 逐 Pin 检查
    {
        ULONG i;
        for (i = 0; i < d->PinDescriptorsCount; ++i)
        {
            const KSPIN_DESCRIPTOR_EX *ex = &d->PinDescriptors[i];
            NTSTATUS st = _CheckOnePin_EX(ex, i);
            if (!NT_SUCCESS(st))
                return st;
        }
    }

    // 逐 Connection 检查
    {
        ULONG i;
        for (i = 0; i < d->ConnectionsCount; ++i)
        {
            NTSTATUS st = _CheckConnection(d, &d->Connections[i], i);
            if (!NT_SUCCESS(st))
                return st;
        }
    }

    // 参考 GUID（有的头是 GUID*）
    // 仅提示，不强制失败
    __try
    {
        if (d->ReferenceGuid && EqualGuidPtr(d->ReferenceGuid, &GUID_NULL))
        {
            DbgPrint(KSFCHK_PREFIX "ReferenceGuid is GUID_NULL (warn)\n");
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DbgPrint(KSFCHK_PREFIX "ReferenceGuid access raised (ignored)\n");
    }

    DbgPrint(KSFCHK_PREFIX "Descriptor validation passed.\n");
    return STATUS_SUCCESS;
}
