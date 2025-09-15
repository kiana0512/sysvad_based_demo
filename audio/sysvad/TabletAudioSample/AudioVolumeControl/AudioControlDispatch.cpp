//
// AudioControlDispatch.cpp —— 统一 IOCTL 分发入口（KS 驱动唯一通道）
//

#include <ntddk.h>
#include "KianaControl.h"          // IOCTL 宏（内部用 <devioctl.h>）
#include "AudioVolumeControl.h"    // g_DeviceVolume / g_Muted + VolumeControl_*
#include "AudioEQControl.h"        // EQControl_*（也提供 EQ_TEST_BUFFER_SIZE 与测试缓冲 extern）

// ===== Test-only IOCTL（仅内部调试）=========================================
#define IOCTL_SEND_PCM   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x911, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_RECV_PCM   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x912, METHOD_BUFFERED, FILE_READ_DATA)

// 测试缓冲（在 AudioEQControl.cpp 定义；这里仅引用）
extern UCHAR g_EqTestBuffer[EQ_TEST_BUFFER_SIZE];
extern ULONG g_EqTestSize;

static __forceinline NTSTATUS _Complete(PIRP Irp, NTSTATUS Status, ULONG_PTR Info = 0)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

// CREATE/CLOSE：简单成功
NTSTATUS AudioVolumeControl_DispatchCreate(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return _Complete(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS AudioVolumeControl_DispatchClose(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return _Complete(Irp, STATUS_SUCCESS, 0);
}

// 统一 IOCTL 分发
NTSTATUS AudioVolumeControl_DispatchIoControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(Irp);
    ULONG  code   = sp->Parameters.DeviceIoControl.IoControlCode;
    ULONG  inLen  = sp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG  outLen = sp->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID  buf    = Irp->AssociatedIrp.SystemBuffer;

    NTSTATUS   status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR  info   = 0;

    switch (code)
    {
    case IOCTL_KIANA_GET_VERSION:
    {
        static const char ver[] = "Kiana KS EQ Driver v1.0";
        if (!buf || outLen < sizeof(ver)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        RtlCopyMemory(buf, ver, sizeof(ver));
        info   = sizeof(ver);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_KIANA_SET_VOLUME:
    {
        if (!buf || inLen < sizeof(ULONG)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        ULONG v = *(ULONG*)buf;
        if (v > 100) v = 100;
        g_DeviceVolume = v;
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_KIANA_GET_VOLUME:
    {
        if (!buf || outLen < sizeof(ULONG)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        *(ULONG*)buf = g_DeviceVolume;
        info   = sizeof(ULONG);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_KIANA_MUTE:    g_Muted = TRUE;  status = STATUS_SUCCESS; break;
    case IOCTL_KIANA_UNMUTE:  g_Muted = FALSE; status = STATUS_SUCCESS; break;

    // ===== EQ：12 段 biquad 系数（Q15）=====================================
    case IOCTL_KIANA_SET_EQ_BIQUAD:
    {
        if (!buf || inLen < sizeof(EQCoeffParams)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        EQCoeffParams* p = (EQCoeffParams*)buf;
        EQControl_SetBiquadCoeffs(p);
        status = STATUS_SUCCESS;
        break;
    }
    case IOCTL_KIANA_GET_EQ_BIQUAD:
    {
        if (!buf || outLen < sizeof(EQCoeffParams)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        EQCoeffParams* p = (EQCoeffParams*)buf;
        EQControl_GetBiquadCoeffs(p);
        info   = sizeof(EQCoeffParams);
        status = STATUS_SUCCESS;
        break;
    }

    // ===== 内部调试：送/取原始 PCM =========================================
    case IOCTL_SEND_PCM:
    {
        if (!buf || inLen == 0) { status = STATUS_INVALID_PARAMETER; break; }
        if (inLen > EQ_TEST_BUFFER_SIZE) { status = STATUS_BUFFER_OVERFLOW; break; }
        RtlCopyMemory(g_EqTestBuffer, buf, inLen);
        g_EqTestSize = inLen;
        status = STATUS_SUCCESS;
        break;
    }
    case IOCTL_RECV_PCM:
    {
        if (!buf || outLen == 0) { status = STATUS_INVALID_PARAMETER; break; }
        if (outLen < g_EqTestSize) { status = STATUS_BUFFER_TOO_SMALL; break; }
        RtlCopyMemory(buf, g_EqTestBuffer, g_EqTestSize);
        info   = g_EqTestSize;
        status = STATUS_SUCCESS;
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    return _Complete(Irp, status, info);
}
