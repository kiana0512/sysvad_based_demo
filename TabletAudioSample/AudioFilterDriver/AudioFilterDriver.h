#pragma once


#include <ntddk.h>

// 控制设备对象（仅控制接口用）
extern PDEVICE_OBJECT g_ControlDeviceObject;

// 驱动核心函数
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject);
void DriverUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS DispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS DispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS DispatchIoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
