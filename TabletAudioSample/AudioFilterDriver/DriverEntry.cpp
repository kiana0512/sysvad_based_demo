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
    UNICODE_STRING devName;
    RtlInitUnicodeString(&devName, L"\\Device\\AudioFilter");

    DbgPrint("==[KSFilter] AddDevice Called==\n");

    // 创建 KS 过滤设备对象
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        sizeof(FILTER_DEVICE_EXTENSION),
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        0,
        FALSE,
        &g_FilterDeviceObject
    );

    if (!NT_SUCCESS(status)) {
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

    // 设置缓冲区 IO 模式并清除初始化标志
    g_FilterDeviceObject->Flags |= DO_BUFFERED_IO;
    g_FilterDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

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
