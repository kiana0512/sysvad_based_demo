#pragma once
//
// AudioVolumeControl.h ——【统一的音量控制接口（Header-only 全局状态定义版）】
//
// - 在头文件里“定义”全局变量，避免为变量再专门建一个 .cpp。
// - 若启用 C++17：使用 inline 变量（标准做法）
// - 若未启用 C++17：回退为 __declspec(selectany)（MSVC 扩展，链接器择一保留）
//

#include <ntddk.h>

// ===== EXTERN_C 兜底：仅用于函数声明（变量这次改为头文件直接定义） =====
#ifndef EXTERN_C
#  ifdef __cplusplus
#    define EXTERN_C extern "C"
#  else
#    define EXTERN_C extern
#  endif
#endif

// ===== 运行时状态（头文件内“唯一定义”）==================================
// 说明：
//  * C++17 情况：inline 变量是 ODR-safe 的头文件定义，多 TU 只算一个实体。
//  * 非 C++17 情况：__declspec(selectany) 让链接器从多重定义里挑一个保留。
#if (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L) || (defined(__cplusplus) && __cplusplus >= 201703L)
    inline ULONG   g_DeviceVolume = 100;   // 默认 100%
    inline BOOLEAN g_Muted        = FALSE; // 默认不静音
#else
    __declspec(selectany) ULONG   g_DeviceVolume = 100;
    __declspec(selectany) BOOLEAN g_Muted        = FALSE;
#endif

// ===== 核心处理 API ========================================================
// 将缓冲区内的 16-bit PCM 做按 dB 的增益处理（内部用 Q15）
//  - pBuffer: PCM 缓冲区（UCHAR*，小端 16-bit）
//  - length : 字节数（内部按 2 字节对齐处理）
//  - gainDb : -40 ~ +40 dB（由上游映射；静音可上游处理）
EXTERN_C VOID
VolumeControl_Apply(_Inout_updates_bytes_(length) UCHAR* pBuffer,
                    _In_                           ULONG  length,
                    _In_                           int    gainDb);

// 初始化缺省音量状态（保留；虽然变量已带默认值，仍可在 DriverEntry 调用一次做日志等）
EXTERN_C VOID VolumeControl_Init(void);

// ===== 简单调试宏（可与全局日志风格统一）===================================
#ifndef LOG_DBG
#define LOG_DBG(fmt, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[VOL] " fmt "\n", __VA_ARGS__)
#endif

//
// 本头不导出任何 IOCTL 宏/分发原型；IOCTL 放 KianaControl.h，分发在 AudioControlDispatch.cpp。
// 注意：不要在其它地方再定义 g_DeviceVolume/g_Muted（否则会与这里冲突）。
//
