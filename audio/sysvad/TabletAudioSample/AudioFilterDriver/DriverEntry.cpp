#include <ntddk.h>
#include "AudioEQControl.h"
#include "AudioVolumeControl.h"
#include "AudioFilterDriver.h"

// -----------------------------
// 全局设备对象
// -----------------------------

PDEVICE_OBJECT g_FilterDeviceObject = nullptr;    // KS 过滤设备（附加到真实 USB 音频设备）
PDEVICE_OBJECT g_LowerDeviceObject  = nullptr;    // 被附加的下层设备对象（USB 音频驱动）

PDEVICE_OBJECT g_ControlDeviceObject = nullptr;   // 控制设备（提供符号链接供应用访问）

// 符号链接路径
UNICODE_STRING g_DeviceName   = RTL_CONSTANT_STRING(L"\\Device\\kiana_AudioControl");
UNICODE_STRING g_SymLinkName  = RTL_CONSTANT_STRING(L"\\DosDevices\\kiana_AudioControl");

// ============================================================================
// DriverEntry：驱动程序入口函数（初始化 KS 滤波器和控制设备）
// ============================================================================
extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DbgPrint("==[KSFilter] DriverEntry Called==\n");

    // 注册分发函数（适用于控制设备）
    DriverObject->MajorFunction[IRP_MJ_CREATE] = AudioVolumeControl_DispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = AudioVolumeControl_DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AudioVolumeControl_DispatchIoControl;

    // 注册 AddDevice 用于 KS 滤波器挂接
    DriverObject->DriverExtension->AddDevice = AddDevice;

    // 设置卸载函数
    DriverObject->DriverUnload = DriverUnload;

    // -----------------------------
    // 创建控制设备并建立符号链接
    // -----------------------------
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &g_DeviceName,
        FILE_DEVICE_UNKNOWN,
        0,
        FALSE,
        &g_ControlDeviceObject
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[kiana] IoCreateDevice failed: 0x%08X\n", status);
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymLinkName, &g_DeviceName);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[kiana] IoCreateSymbolicLink failed: 0x%08X\n", status);
        IoDeleteDevice(g_ControlDeviceObject);
        return status;
    }

    // 初始化 EQ 和音量控制模块
    EQControl_Init();
    VolumeControl_Init();

    DbgPrint("[kiana] DriverEntry complete.\n");
    return STATUS_SUCCESS;
}

// ============================================================================
// AddDevice：设备枚举时由系统调用，用于挂接到USB音频设备栈
// ============================================================================
NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
    UNICODE_STRING devName;
    RtlInitUnicodeString(&devName, L"\\Device\\AudioFilter");

    DbgPrint("==[KSFilter] AddDevice Called==\n");

    // 创建 KS 过滤设备对象
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &devName,
        FILE_DEVICE_UNKNOWN,
        0,
        FALSE,
        &g_FilterDeviceObject
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[KSFilter] IoCreateDevice failed: 0x%08X\n", status);
        return status;
    }

    // 附加到目标 USB 音频设备栈
    status = IoAttachDeviceToDeviceStackSafe(
        g_FilterDeviceObject,
        PhysicalDeviceObject,
        &g_LowerDeviceObject
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[KSFilter] Attach failed: 0x%08X\n", status);
        IoDeleteDevice(g_FilterDeviceObject);
        return status;
    }

    // 设置缓冲区 IO 模式并清除初始化标志
    g_FilterDeviceObject->Flags |= DO_BUFFERED_IO;
    g_FilterDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    DbgPrint("==[KSFilter] AddDevice success==\n");
    return STATUS_SUCCESS;
}

// ============================================================================
// DriverUnload：驱动卸载时清理控制设备、符号链接、过滤设备
// ============================================================================
void DriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    DbgPrint("==[KSFilter] DriverUnload Called==\n");

    // 删除控制设备的符号链接和设备对象
    IoDeleteSymbolicLink(&g_SymLinkName);

    if (g_ControlDeviceObject) {
        IoDeleteDevice(g_ControlDeviceObject);
        g_ControlDeviceObject = nullptr;
    }

    // 卸载 KS 设备对象
    if (g_LowerDeviceObject) {
        IoDetachDevice(g_LowerDeviceObject);
        g_LowerDeviceObject = nullptr;
    }

    if (g_FilterDeviceObject) {
        IoDeleteDevice(g_FilterDeviceObject);
        g_FilterDeviceObject = nullptr;
    }

    DbgPrint("==[KSFilter] DriverUnload complete==\n");
}
