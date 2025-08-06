#pragma once
#include <ntddk.h>

// 全局设备对象（用作上下层 attach）
extern PDEVICE_OBJECT g_LowerDeviceObject;
extern PDEVICE_OBJECT g_FilterDeviceObject;

// 函数声明
NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject);
void DriverUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS DispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS DispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS DispatchIoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
