#include "AudioFilterDriver.h"

// 全局设备对象指针
PDEVICE_OBJECT g_FilterDeviceObject = nullptr;    // 当前驱动创建的设备对象
PDEVICE_OBJECT g_LowerDeviceObject = nullptr;     // 被附加的下层设备对象（真实 USB 驱动）

// 驱动入口函数
extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DbgPrint("==[KSFilter] DriverEntry Called==\n");

    DriverObject->DriverUnload = DriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchIoControl;
    DriverObject->DriverExtension->AddDevice = AddDevice;

    return STATUS_SUCCESS;
}

NTSTATUS DispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS DispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// AddDevice：系统枚举设备并加载你的驱动时调用
NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
    UNICODE_STRING devName;
    RtlInitUnicodeString(&devName, L"\\Device\\AudioFilter");  // 创建一个逻辑设备名称

    DbgPrint("==[KSFilter] AddDevice Called==\n");

    // 创建设备对象（注意：此对象并不会真正出现在设备管理器中）
    NTSTATUS status = IoCreateDevice(
        DriverObject,           // 当前驱动对象
        0,                      // 不需要扩展数据
        &devName,               // 内核设备名称
        FILE_DEVICE_UNKNOWN,   // 类型未知（可自定义）
        0,                      // 设备特性
        FALSE,                  // 非独占设备
        &g_FilterDeviceObject   // 返回创建的设备对象
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[KSFilter] IoCreateDevice failed: 0x%08X\n", status);
        return status;
    }

    // 将我们创建的设备对象附加到目标 USB 音频设备上（挂在设备栈上）
    status = IoAttachDeviceToDeviceStackSafe(
        g_FilterDeviceObject,      // 我们的设备对象（上层）
        PhysicalDeviceObject,      // 系统传入的目标设备对象（下层）
        &g_LowerDeviceObject       // 返回我们附加到的对象指针
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[KSFilter] Attach to USB audio device failed: 0x%08X\n", status);
        IoDeleteDevice(g_FilterDeviceObject);
        return status;
    }

    // 设置缓冲区 I/O 模式（一般建议开启）
    g_FilterDeviceObject->Flags |= DO_BUFFERED_IO;

    // 清除初始化标志，表示设备已准备好
    g_FilterDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    DbgPrint("==[KSFilter] AddDevice success: Attached to USB audio device stack==\n");

    return STATUS_SUCCESS;
}

NTSTATUS DispatchIoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG code = irpSp->Parameters.DeviceIoControl.IoControlCode;

    DbgPrint("[KSFilter] DispatchIoControl received: 0x%08X\n", code);

    // 暂时默认返回不支持
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}


