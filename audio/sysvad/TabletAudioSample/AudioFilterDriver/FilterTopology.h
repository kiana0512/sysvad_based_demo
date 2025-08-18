#pragma once

#include <ntddk.h>
#include <windef.h>
#include <ks.h>
#include <ksmedia.h>

// Pin ID 定义
enum
{
    PIN_ID_INPUT = 0,
    PIN_ID_OUTPUT,
    PIN_ID_COUNT
};

#ifdef __cplusplus
extern "C"
{
#endif

    // Pin 音频处理回调（对应 PFNKSPINPROCESS）
    NTSTATUS NTAPI PinWriteProcess(PKSPIN Pin);

    // Filter 描述符（驱动入口描述）
    extern KSFILTER_DESCRIPTOR FilterDescriptor;

    // 运行时构建 Pins（在注册工厂之前调用一次）
    void SetupPins(void);

#ifdef __cplusplus
}
#endif
