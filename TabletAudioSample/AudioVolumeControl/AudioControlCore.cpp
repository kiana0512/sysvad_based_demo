#include "AudioVolumeControl.h"
#include "AudioEQControl.h"

//
// 全局变量初始化
//
ULONG g_DeviceVolume = 100;
BOOLEAN g_Muted = FALSE;
PDEVICE_OBJECT g_VolumeControlDevice = nullptr;
UNICODE_STRING g_SymbolicLinkName;

//
// 创建设备和符号链接
//
NTSTATUS AudioVolumeControl_CreateDeviceAndSymbolicLink(_In_ PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, L"\\Device\\SysvadControl");
    RtlInitUnicodeString(&g_SymbolicLinkName, L"\\??\\SysvadControl");

    // 删除旧符号链接
    IoDeleteSymbolicLink(&g_SymbolicLinkName);

    // 创建设备对象
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_VolumeControlDevice);

    if (!NT_SUCCESS(status))
    {
        DbgPrint("[AVC] IoCreateDevice failed: 0x%08X\n", status);
        return status;
    }

    g_VolumeControlDevice->Flags |= DO_BUFFERED_IO;

    // 分发函数设置
    DriverObject->MajorFunction[IRP_MJ_CREATE] = AudioVolumeControl_DispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = AudioVolumeControl_DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AudioVolumeControl_DispatchIoControl;

    // 注册卸载函数
    DriverObject->DriverUnload = AudioVolumeControl_Unload;

    g_VolumeControlDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    // 创建符号链接
    status = IoCreateSymbolicLink(&g_SymbolicLinkName, &deviceName);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[AVC] IoCreateSymbolicLink failed: 0x%08X\n", status);
        IoDeleteDevice(g_VolumeControlDevice);
        g_VolumeControlDevice = nullptr;
        return status;
    }

    DbgPrint("[AVC] Control device and symbolic link created OK.\n");
    return STATUS_SUCCESS;
}

//
// 卸载时清理设备和符号链接
//
VOID AudioVolumeControl_Unload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    IoDeleteSymbolicLink(&g_SymbolicLinkName);

    if (g_VolumeControlDevice)
    {
        IoDeleteDevice(g_VolumeControlDevice);
        g_VolumeControlDevice = nullptr;
    }

    DbgPrint("[AVC] Unload: device and symbolic link removed.\n");
}
