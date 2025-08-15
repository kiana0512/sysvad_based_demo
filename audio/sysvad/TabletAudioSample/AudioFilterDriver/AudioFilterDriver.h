#pragma once

#include <ntddk.h>

// 控制设备对象（仅控制接口用）
extern PDEVICE_OBJECT g_ControlDeviceObject;

// 驱动核心函数
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject);
void DriverUnload(PDRIVER_OBJECT DriverObject);

// 控制设备的 IRP 分发函数（控制接口）
NTSTATUS AudioVolumeControl_DispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS AudioVolumeControl_DispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS AudioVolumeControl_DispatchIoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);

// 过滤设备的 PnP 分发函数
NTSTATUS AudioFilter_PnPDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp);
PDEVICE_OBJECT GetLowestDevice(PDEVICE_OBJECT startDevice);
NTSTATUS QueryHardwareIdWithIrp(PDEVICE_OBJECT targetDevice, WCHAR *buffer, ULONG bufferLength);
NTSTATUS StartComplete(PDEVICE_OBJECT DevObj, PIRP Irp, PVOID Context);