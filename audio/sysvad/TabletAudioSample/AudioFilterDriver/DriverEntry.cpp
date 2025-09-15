#include <ntddk.h>
#include "../AudioVolumeControl/AudioEQControl.h"
#include "../AudioVolumeControl/AudioVolumeControl.h"
#include "AudioFilterDriver.h"
#include <wchar.h>
#include <windef.h>  // 包含 BOOL、DWORD 等定义
#include <ks.h>      // KS 框架基本结构
#include <ksmedia.h> // KSDATARANGE_AUDIO、KSCATEGORY_AUDIO 等音频定义
#include "FilterTopology.h"
#include "KsHelper.h"
#include "KsSanityCheck.h"

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
    DbgPrint("==[KSFilter] DriverEntry Called==\n");
    //  先把 KS 的主分发表挂上（不会覆盖你自己的分发）
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_CREATE);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_CLOSE);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_DEVICE_CONTROL);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_READ);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_WRITE);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_FLUSH_BUFFERS);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_QUERY_SECURITY);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_SET_SECURITY);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_POWER);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_SYSTEM_CONTROL);

    // 注册控制设备的 IRP 分发函数（CREATE / CLOSE / IOCTL）
    DriverObject->MajorFunction[IRP_MJ_CREATE] = AudioVolumeControl_DispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = AudioVolumeControl_DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AudioVolumeControl_DispatchIoControl;

    // 注册 PnP 分发函数（处理 Filter 设备挂载 / 卸载）
    DriverObject->MajorFunction[IRP_MJ_PNP] = AudioFilter_PnPDispatch;

    // 注册 AddDevice（当有设备被枚举时系统会调用此函数）
    DriverObject->DriverExtension->AddDevice = AddDevice;

    // 注册驱动卸载函数
    DriverObject->DriverUnload = DriverUnload;

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
        DbgPrint("[kiana] IoCreateDevice failed: 0x%08X\n", status);
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymLinkName, &g_DeviceName);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[kiana] IoCreateSymbolicLink failed: 0x%08X\n", status);
        IoDeleteDevice(g_ControlDeviceObject);
        return status;
    }

    // 初始化 EQ / 音量控制模块
    EQControl_Init();
    VolumeControl_Init();

    DbgPrint("[kiana] DriverEntry complete.\n");
    return STATUS_SUCCESS;
}

// ============================================================================
// AddDevice：为每个被枚举的 USB 音频设备创建一个 KS Filter 实例
// ============================================================================
NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
    DbgPrint("==[KSFilter] AddDevice Called==\n");

    // 1) 生成唯一设备名
    static LONG deviceCount = 0;
    LONG index = InterlockedIncrement(&deviceCount);

    WCHAR nameBuffer[64] = {0};
    _snwprintf(nameBuffer, 64, L"\\Device\\AudioFilter_%d", index);
    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, nameBuffer);

    // 2) 创建设备对象
    PDEVICE_OBJECT filterDevice = nullptr;
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        sizeof(FILTER_DEVICE_EXTENSION),
        &deviceName,
        FILE_DEVICE_KS,
        0,
        FALSE,
        &filterDevice);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[KSFilter] IoCreateDevice failed: 0x%08X\n", status);
        return status;
    }

    // 3) 附着到设备栈（Lower = FDO）
    PDEVICE_OBJECT lowerDevice = nullptr;
    status = IoAttachDeviceToDeviceStackSafe(filterDevice, PhysicalDeviceObject, &lowerDevice);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[KSFilter] Attach failed: 0x%08X\n", status);
        IoDeleteDevice(filterDevice);
        return status;
    }

    // 4) 初始化扩展（清零最稳）
    PFILTER_DEVICE_EXTENSION ext = (PFILTER_DEVICE_EXTENSION)filterDevice->DeviceExtension;
    RtlZeroMemory(ext, sizeof(*ext));
    ext->LowerDeviceObject = lowerDevice;
    ext->Pdo = PhysicalDeviceObject; // ★ 保存真正 PDO
    ext->EnableAudioProcessing = FALSE;
    ext->DeviceIndex = index;

    // 5) 读取 Hardware IDs —— 用 IoGetDeviceProperty(PDO, DevicePropertyHardwareID)
    ULONG len = 0;
    NTSTATUS st = IoGetDeviceProperty(
        PhysicalDeviceObject,
        DevicePropertyHardwareID,
        0,
        NULL,
        &len);

    if (st == STATUS_BUFFER_TOO_SMALL && len)
    {
        PWSTR ids = (PWSTR)ExAllocatePoolWithTag(PagedPool, len, 'HIDs');
        if (ids)
        {
            st = IoGetDeviceProperty(
                PhysicalDeviceObject,
                DevicePropertyHardwareID,
                len,
                ids,
                &len);

            if (NT_SUCCESS(st))
            {
                for (PWSTR p = ids; *p; p += wcslen(p) + 1)
                {
                    DbgPrint("[KSFilter] >> HardwareID entry: %ws\n", p);

                    if (wcsstr(p, L"USB\\VID_0A67") && wcsstr(p, L"PID_30A2") && wcsstr(p, L"MI_00"))
                    {
                        ext->EnableAudioProcessing = TRUE;
                        DbgPrint("[KSFilter] Matched target USB audio device\n");
                        break;
                    }
                }
            }
            ExFreePool(ids);
        }
    }

    if (!ext->EnableAudioProcessing)
    {
        DbgPrint("[KSFilter] Device is not target. Abort AddDevice.\n");
        IoDetachDevice(lowerDevice);
        IoDeleteDevice(filterDevice);
        return STATUS_UNSUCCESSFUL;
    }

    // 6) 保存设备名到扩展（可保留你的逻辑）
    ext->DeviceName.Buffer = (PWSTR)ExAllocatePoolWithTag(
        PagedPool, deviceName.Length + sizeof(WCHAR), 'devn');
    if (ext->DeviceName.Buffer)
    {
        RtlCopyMemory(ext->DeviceName.Buffer, deviceName.Buffer, deviceName.Length);
        ext->DeviceName.Buffer[deviceName.Length / sizeof(WCHAR)] = L'\0';
        ext->DeviceName.Length = deviceName.Length;
        ext->DeviceName.MaximumLength = deviceName.Length + sizeof(WCHAR);
    }

    // 7) 创建符号链接
    WCHAR linkNameBuffer[64] = {0};
    _snwprintf(linkNameBuffer, 64, L"\\DosDevices\\KianaAudioEQ_%d", index);
    UNICODE_STRING devLink;
    RtlInitUnicodeString(&devLink, linkNameBuffer);
    status = IoCreateSymbolicLink(&devLink, &deviceName);
    if (!NT_SUCCESS(status))
        DbgPrint("[KSFilter] IoCreateSymbolicLink failed: 0x%08X\n", status);
    else
        DbgPrint("[KSFilter] Symbolic link created: %ws -> %ws\n", linkNameBuffer, deviceName.Buffer);

    // 8) Flags（从下层继承分页能力更稳）
    filterDevice->Flags |= DO_BUFFERED_IO;
    if (ext->LowerDeviceObject->Flags & DO_POWER_PAGABLE)
        filterDevice->Flags |= DO_POWER_PAGABLE;
    filterDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    DbgPrint("[KSFilter] AddDevice success: %wZ\n", &deviceName);
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
NTSTATUS
AudioFilter_PnPDispatch(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILTER_DEVICE_EXTENSION devExt = (PFILTER_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    switch (irpSp->MinorFunction)
    {
    case IRP_MN_REMOVE_DEVICE:
    {
        DbgPrint("[KSFilter] IRP_MN_REMOVE_DEVICE received\n");

        // 建议在本层做清理前，先把 REMOVE 下发，让下层也做自己的清理
        IoSkipCurrentIrpStackLocation(Irp);
        NTSTATUS st = IoCallDriver(devExt->LowerDeviceObject, Irp);

        // 显式删除我们注册的 Filter Factory（我们使用了 KSCREATE_ITEM_FREEONSTOP，但显式删更稳妥）
        if (devExt->FilterFactory)
        {
            KsDeleteFilterFactory(devExt->FilterFactory);
            devExt->FilterFactory = NULL;
        }

        // 删除实例符号链接（你在 AddDevice 里按 "\DosDevices\KianaAudioEQ_%d" 创建的）
        {
            WCHAR linkNameBuffer[64] = {0};
            _snwprintf(linkNameBuffer, 64, L"\\DosDevices\\KianaAudioEQ_%ld", devExt->DeviceIndex);
            UNICODE_STRING devLink;
            RtlInitUnicodeString(&devLink, linkNameBuffer);
            IoDeleteSymbolicLink(&devLink);
        }

        // 释放我们手动分配的设备名缓冲（AddDevice 用 ExAllocatePool 分配的）
        if (devExt->DeviceName.Buffer)
        {
            ExFreePool(devExt->DeviceName.Buffer);
            devExt->DeviceName.Buffer = NULL;
            devExt->DeviceName.Length = devExt->DeviceName.MaximumLength = 0;
        }

        // 从设备栈脱离并删除本设备对象
        if (devExt->LowerDeviceObject)
        {
            IoDetachDevice(devExt->LowerDeviceObject);
            devExt->LowerDeviceObject = NULL;
        }
        IoDeleteDevice(DeviceObject);
        return st;
    }

    case IRP_MN_STOP_DEVICE:
    {
        DbgPrint("[KSFilter] IRP_MN_STOP_DEVICE received\n");

        // STOP 通常也要把工厂撤销（FREEONSTOP 会清理 KSCREATE_ITEM）。显式删以避免重复注册。
        if (devExt->FilterFactory)
        {
            KsDeleteFilterFactory(devExt->FilterFactory);
            devExt->FilterFactory = NULL;
        }

        // 继续传下层
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(devExt->LowerDeviceObject, Irp);
    }

    case IRP_MN_START_DEVICE:
    {
        DbgPrint("[KSFilter] IRP_MN_START_DEVICE received\n");

        // ---------- 1) 同步等待下层 START 完成 ----------
        KEVENT evt;
        KeInitializeEvent(&evt, NotificationEvent, FALSE);

        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(Irp, StartComplete, &evt, TRUE, TRUE, TRUE);

        NTSTATUS downStatus = IoCallDriver(devExt->LowerDeviceObject, Irp);
        if (downStatus == STATUS_PENDING)
        {
            KeWaitForSingleObject(&evt, Executive, KernelMode, FALSE, NULL);
            downStatus = Irp->IoStatus.Status;
        }
        if (!NT_SUCCESS(downStatus))
        {
            DbgPrint("[KSFilter] START failed in lower stack: 0x%08X\n", downStatus);
            Irp->IoStatus.Status = downStatus;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return downStatus;
        }

        // ---------- 2) 非目标设备：直接完成 START ----------
        if (!devExt->EnableAudioProcessing)
        {
            DbgPrint("[KSFilter] Not target device, skipping KS setup\n");
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }

        // ---------- 3) 初始化 KSDEVICE（老签名的正确参数顺序） ----------
        // KsInitializeDevice(FDO, PDO, Lower, &g_KsDeviceDescriptor)
        // 这一步会在“设备扩展首字段”处初始化 KSDEVICE_HEADER（前提是你把 KSDEVICE_HEADER 放在扩展的第 0 偏移）
        NTSTATUS ksSt = KsInitializeDevice(
            DeviceObject,              // FDO = 本层设备对象
            devExt->Pdo,               // PDO = AddDevice 保存的 PhysicalDeviceObject
            devExt->LowerDeviceObject, // Lower = IoAttachDeviceToDeviceStackSafe 的返回
            &g_KsDeviceDescriptor      // 设备级描述子（我们后续用 FilterFactory 注册，描述子里 FilterCount 可为 0）
        );
        DbgPrint("[KSFilter] KsInitializeDevice(FDO, PDO, Lower, Desc): st=0x%08X\n", ksSt);
        if (!NT_SUCCESS(ksSt))
        {
            Irp->IoStatus.Status = ksSt;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return ksSt;
        }

        // （可选）验证：此时 KSDEVICE 已可取，用于后续 Acquire/Release
        PKSDEVICE ksdev = KsGetDeviceForDeviceObject(DeviceObject);
        DbgPrint("[KSFilter] KsDevice=%p (after KsInitializeDevice)\n", ksdev);
        if (!ksdev)
        {
            Irp->IoStatus.Status = STATUS_DEVICE_DOES_NOT_EXIST;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_DEVICE_DOES_NOT_EXIST;
        }

        // ---------- 4) 注册 Filter Factory（只做一次） ----------
        if (!devExt->FilterFactory)
        {
            // 4.1 构造并校验 Filter 描述符（你已有的工具）
            SetupPins();
            NTSTATUS chk = SanityCheckKsFilterDesc(&FilterDescriptor);
            if (!NT_SUCCESS(chk))
            {
                DbgPrint("[KSFilter][ERR] FilterDescriptor sanity check failed: 0x%08X\n", chk);
                Irp->IoStatus.Status = chk;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return chk;
            }

            // 4.2 进入 KS 设备临界区 → 创建工厂 → 释放临界区
            KsAcquireDevice(ksdev);
            PKSFILTERFACTORY factory = NULL;
            NTSTATUS regStatus = KsCreateFilterFactory(
                DeviceObject,             // FDO（注意不是 PDO）
                &FilterDescriptor,        // 过滤器描述符（Pins/Nodes/Connections/Properties 等）
                NULL,                     // RefString
                NULL,                     // SecurityDescriptor
                KSCREATE_ITEM_FREEONSTOP, // STOP/REMOVE 自动清理 create-items
                NULL,                     // SleepCallback（可空）
                NULL,                     // WakeCallback（可空）
                &factory                  // 返回的工厂句柄
            );
            KsReleaseDevice(ksdev);

            if (!NT_SUCCESS(regStatus))
            {
                DbgPrint("[KSFilter] KsCreateFilterFactory failed: 0x%08X\n", regStatus);
                Irp->IoStatus.Status = regStatus;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return regStatus;
            }

            devExt->FilterFactory = factory;
            DbgPrint("[KSFilter] KsCreateFilterFactory OK, factory=%p\n", factory);
            DbgPrint("[KSFilter] FilterDescriptor=%p, Pins=%lu, Nodes=%lu\n",
                     &FilterDescriptor,
                     (ULONG)FilterDescriptor.PinDescriptorsCount,
                     (ULONG)FilterDescriptor.NodeDescriptorsCount);
        }

        // ---------- 5) 完成 START ----------
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    default:
        // 其他 PnP IRP 直接透传给下层
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
