#include <ntddk.h>
#include "AudioEQControl.h"
#include "AudioVolumeControl.h"
#include "AudioFilterDriver.h"
#include <wchar.h>
#include <windef.h>  // 包含 BOOL、DWORD 等定义
#include <ks.h>      // KS 框架基本结构
#include <ksmedia.h> // KSDATARANGE_AUDIO、KSCATEGORY_AUDIO 等音频定义
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
// 设备扩展结构体（每个 KS Filter 实例都拥有）
// ============================================================================
typedef struct _FILTER_DEVICE_EXTENSION
{
    PDEVICE_OBJECT LowerDeviceObject; // 附加的下层设备（真实 USB 音频）
    UNICODE_STRING DeviceName;        // 当前设备名（用于释放）
    BOOLEAN EnableAudioProcessing;    // 是否启用 EQ/音量
    LONG DeviceIndex;                 //
} FILTER_DEVICE_EXTENSION, *PFILTER_DEVICE_EXTENSION;

// ============================================================================
// DriverEntry：驱动程序入口，注册分发函数并创建控制设备
// ============================================================================
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DbgPrint("==[KSFilter] DriverEntry Called==\n");

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
        FILE_DEVICE_UNKNOWN,
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

    // 步骤 1：生成唯一设备名
    static LONG deviceCount = 0;
    LONG index = InterlockedIncrement(&deviceCount);

    WCHAR nameBuffer[64];
    UNICODE_STRING deviceName;
    _snwprintf(nameBuffer, 64, L"\\Device\\AudioFilter_%d", index);
    RtlInitUnicodeString(&deviceName, nameBuffer);

    // 步骤 2：创建设备对象
    PDEVICE_OBJECT filterDevice = nullptr;
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        sizeof(FILTER_DEVICE_EXTENSION),
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        0,
        FALSE,
        &filterDevice);

    if (!NT_SUCCESS(status))
    {
        DbgPrint("[KSFilter] IoCreateDevice failed: 0x%08X\n", status);
        return status;
    }

    // 步骤 3：附加到设备栈
    PDEVICE_OBJECT lowerDevice = nullptr;
    status = IoAttachDeviceToDeviceStackSafe(filterDevice, PhysicalDeviceObject, &lowerDevice);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[KSFilter] Attach failed: 0x%08X\n", status);
        IoDeleteDevice(filterDevice);
        return status;
    }

    // 步骤 4：初始化扩展
    PFILTER_DEVICE_EXTENSION devExt = (PFILTER_DEVICE_EXTENSION)filterDevice->DeviceExtension;
    devExt->LowerDeviceObject = lowerDevice;
    devExt->EnableAudioProcessing = FALSE;
    RtlInitUnicodeString(&devExt->DeviceName, nullptr);
    devExt->DeviceIndex = index;

    // 步骤 5：获取 HardwareID（向下遍历设备栈至 PDO）
    PDEVICE_OBJECT pdo = GetLowestDevice(lowerDevice);
    if (!pdo)
    {
        DbgPrint("[KSFilter] GetLowestDevice failed\n");
        IoDetachDevice(lowerDevice);
        IoDeleteDevice(filterDevice);
        return STATUS_UNSUCCESSFUL;
    }

    // 查询 ID
    KEVENT event;
    PIRP irp;
    IO_STATUS_BLOCK ioStatus = {0};
    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_PNP,
        pdo,
        NULL,
        0,
        NULL,
        &event,
        &ioStatus);

    BOOLEAN matched = FALSE; //  修复：提升为整个函数作用域变量

    if (irp)
    {
        PIO_STACK_LOCATION irpSp = IoGetNextIrpStackLocation(irp);
        irpSp->MinorFunction = IRP_MN_QUERY_ID;
        irpSp->Parameters.QueryId.IdType = BusQueryHardwareIDs;
        irpSp->MajorFunction = IRP_MJ_PNP;

        irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        irp->IoStatus.Information = 0;
        irp->Flags |= IRP_SYNCHRONOUS_API;

        NTSTATUS hwStatus = IoCallDriver(pdo, irp);
        if (hwStatus == STATUS_PENDING)
        {
            KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
            hwStatus = ioStatus.Status;
        }

        if (NT_SUCCESS(hwStatus))
        {
            PWCHAR idList = (PWCHAR)ioStatus.Information;
            if (idList)
            {
                PWCHAR p = idList;
                while (*p)
                {
                    DbgPrint("[KSFilter] >> HardwareID entry: %ws\n", p);

                    //  匹配目标 USB 音频设备
                    if (wcsstr(p, L"USB\\VID_0A67") && wcsstr(p, L"PID_30A2") && wcsstr(p, L"MI_00"))
                    {
                        matched = TRUE;
                        devExt->EnableAudioProcessing = TRUE;
                        DbgPrint("[KSFilter] Matched target USB audio device\n");
                        break;
                    }

                    p += wcslen(p) + 1;
                }

                ExFreePool(idList);
            }
            else
            {
                DbgPrint("[KSFilter] IRP returned NULL HardwareID buffer\n");
            }
        }
        else
        {
            DbgPrint("[KSFilter] IRP_MN_QUERY_ID failed: 0x%08X\n", hwStatus);
        }
    }
    else
    {
        DbgPrint("[KSFilter] IoBuildSynchronousFsdRequest failed\n");
    }

    ObDereferenceObject(pdo); // 释放引用

    //  最终统一判断是否匹配
    if (!matched)
    {
        DbgPrint("[KSFilter] Device is not target. Abort AddDevice.\n");
        IoDetachDevice(lowerDevice);
        IoDeleteDevice(filterDevice);
        return STATUS_UNSUCCESSFUL;
    }

    // 步骤 6：保存设备名到扩展
    devExt->DeviceName.Buffer = (PWSTR)ExAllocatePoolWithTag(PagedPool, deviceName.Length + sizeof(WCHAR), 'devn');
    if (devExt->DeviceName.Buffer)
    {
        RtlCopyMemory(devExt->DeviceName.Buffer, deviceName.Buffer, deviceName.Length);
        devExt->DeviceName.Buffer[deviceName.Length / sizeof(WCHAR)] = L'\0';
        devExt->DeviceName.Length = deviceName.Length;
        devExt->DeviceName.MaximumLength = deviceName.Length + sizeof(WCHAR);
    }

    // 步骤 7：创建符号链接
    WCHAR linkNameBuffer[64];
    UNICODE_STRING devLink;
    _snwprintf(linkNameBuffer, 64, L"\\DosDevices\\KianaAudioEQ_%d", index);
    RtlInitUnicodeString(&devLink, linkNameBuffer);

    status = IoCreateSymbolicLink(&devLink, &deviceName);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[KSFilter] IoCreateSymbolicLink failed: 0x%08X\n", status);
    }
    else
    {
        DbgPrint("[KSFilter] Symbolic link created: %ws -> %ws\n", linkNameBuffer, deviceName.Buffer);
    }

    // 步骤 8：启用缓冲 IO，清除初始化标志
    filterDevice->Flags |= DO_BUFFERED_IO;
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
// AudioFilter_PnPDispatch：处理即插即用 IRP（包括卸载）
// ============================================================================
NTSTATUS AudioFilter_PnPDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILTER_DEVICE_EXTENSION devExt = (PFILTER_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    switch (irpSp->MinorFunction)
    {
    case IRP_MN_REMOVE_DEVICE:
        DbgPrint("[KSFilter] IRP_MN_REMOVE_DEVICE received\n");
        if (devExt->DeviceName.Buffer)
        {
            // 自动删除对应的符号链接
            WCHAR linkNameBuffer[64];
            UNICODE_STRING devLink;
            _snwprintf(linkNameBuffer, 64, L"\\DosDevices\\KianaAudioEQ_%d", devExt->DeviceIndex);
            RtlInitUnicodeString(&devLink, linkNameBuffer);
            IoDeleteSymbolicLink(&devLink);

            RtlFreeUnicodeString(&devExt->DeviceName);
        }

        // 卸载流程：从设备栈中分离并释放设备对象
        if (devExt->LowerDeviceObject)
        {
            IoDetachDevice(devExt->LowerDeviceObject);
            devExt->LowerDeviceObject = nullptr;
        }

        if (devExt->DeviceName.Buffer)
        {
            RtlFreeUnicodeString(&devExt->DeviceName);
        }

        IoDeleteDevice(DeviceObject);

        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;

    case IRP_MN_START_DEVICE:
    {
        DbgPrint("[KSFilter] IRP_MN_START_DEVICE received\n");

        // ===========================
        // 步骤 1：继续向下层驱动传递 IRP
        // ===========================
        IoCopyCurrentIrpStackLocationToNext(Irp);
        NTSTATUS status = IoCallDriver(devExt->LowerDeviceObject, Irp);

        // ===========================
        // 步骤 2：仅当匹配目标设备时，注册 AVStream Filter 拓扑
        // ===========================
        if (devExt->EnableAudioProcessing)
        {
            DbgPrint("[KSFilter] Registering AVStream filter topology...\n");
            // 自定义接口 GUID，用于标识此类音频设备
            static const GUID KSINTERFACE_CLASS_KIANA_AUDIO =
                {0xd4e9fc33, 0x95cb, 0x4724, {0xb5, 0xb1, 0x22, 0x15, 0xe5, 0x67, 0x88, 0x39}};

            // Pin 输入输出方向定义（FALSE: Render 输入，TRUE: Capture 输出）
            BOOL PinDirections[] = {
                FALSE,
                TRUE};

            // 中介介质（此处使用标准 DevIo）
            KSPIN_MEDIUM Mediums[] = {
                {STATICGUIDOF(KSMEDIUMSETID_Standard),
                 KSMEDIUM_STANDARD_DEVIO,
                 0},
                {STATICGUIDOF(KSMEDIUMSETID_Standard),
                 KSMEDIUM_STANDARD_DEVIO,
                 0}};

            // 类别（KSCATEGORY_AUDIO 可被系统识别为音频设备）
            GUID Categories[] = {
                KSCATEGORY_AUDIO,
                KSCATEGORY_AUDIO};

            // AVStream Filter 描述符（需要你在 FilterTopology.cpp 中定义好）
            extern const KSFILTER_DESCRIPTOR FilterDescriptor;

            // 调用 AVStream 提供的注册接口
            NTSTATUS regStatus = KsRegisterFilterWithNoKSPins(
                DeviceObject,
                &KSINTERFACE_CLASS_KIANA_AUDIO,
                COUNT_OF(PinDirections),
                PinDirections,
                Mediums,
                Categories);

            if (!NT_SUCCESS(regStatus))
            {
                DbgPrint("[KSFilter] KsRegisterFilterWithNoKSPins failed: 0x%08X\n", regStatus);
            }
            else
            {
                DbgPrint("[KSFilter] KsRegisterFilterWithNoKSPins succeeded\n");
            }
        }
        else
        {
            DbgPrint("[KSFilter] Audio processing not enabled for this device. Skipping topology registration.\n");
        }

        // 完成 IRP 并返回下层状态（确保系统能继续设备初始化）
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    case IRP_MN_QUERY_REMOVE_DEVICE:
        // 常见 PnP 请求直接 passthrough
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(devExt->LowerDeviceObject, Irp);

    default:
        // 其他 PnP 请求转发下层驱动处理
        if (devExt->LowerDeviceObject)
        {
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(devExt->LowerDeviceObject, Irp);
        }
        else
        {
            Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_INVALID_DEVICE_REQUEST;
        }
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
