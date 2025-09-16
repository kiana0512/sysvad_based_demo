#pragma once
#include <ntddk.h>

// -------------- 前置声明 --------------
typedef struct _FILTER_DEVICE_EXTENSION FILTER_DEVICE_EXTENSION, *PFILTER_DEVICE_EXTENSION;

// -------------- 全局控制设备（仅与用户态 IOCTL 通信） --------------
extern PDEVICE_OBJECT g_ControlDeviceObject;

// -------------- 驱动核心入口 --------------
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject);
void     DriverUnload(PDRIVER_OBJECT DriverObject);

// -------------- 控制设备（你已有的控制通道） --------------
NTSTATUS AudioVolumeControl_DispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS AudioVolumeControl_DispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS AudioVolumeControl_DispatchIoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);

// -------------- 过滤设备：PnP/Power 分发（透明透传） --------------
NTSTATUS AudioFilter_PnPDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS AudioFilter_PowerDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp);

// -------------- 新增：控制/过滤分流包装器（在 DriverEntry 注册） --------------
NTSTATUS FilterOrControl_DispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS FilterOrControl_DispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS FilterOrControl_DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);

// -------------- 可选工具 --------------
PDEVICE_OBJECT GetLowestDevice(PDEVICE_OBJECT startDevice);
NTSTATUS QueryHardwareIdWithIrp(PDEVICE_OBJECT targetDevice, WCHAR* buffer, ULONG bufferLength);
