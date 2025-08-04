#pragma once

#include <ntddk.h> // Windows 核心驱动头文件

//
// IOCTL 定义
//
#define IOCTL_SET_VOLUME CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_VOLUME CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MUTE_AUDIO CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_UNMUTE_AUDIO CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

//
// 全局音量变量与静音标志
//
extern ULONG g_DeviceVolume; // 0~100
extern BOOLEAN g_Muted;

//
// 用于注册符号链接和设备对象
//
NTSTATUS AudioVolumeControl_CreateDeviceAndSymbolicLink(_In_ PDRIVER_OBJECT DriverObject);

//
// IOCTL 分发处理函数
//
VOID AudioVolumeControl_Unload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS AudioVolumeControl_DispatchIoControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);
NTSTATUS AudioVolumeControl_DispatchCreate(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS AudioVolumeControl_DispatchClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);

//
// 简单调试宏（你也可以替换成更复杂的带等级输出）
//
#define LOG_DBG(fmt, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[AVC] " fmt "\n", __VA_ARGS__)
