//
// AudioControlDispatch.cpp —— 统一 IOCTL 分发入口（KS 驱动唯一通道）
// 强化：标准化 DBG 日志（进入/退出、参数长度、缓冲指针、校验、摘要）；测试缓冲 1MB。
// 方法：所有 IOCTL 均 METHOD_BUFFERED，使用 SystemBuffer；严防空指针/长度不符。
// 依赖：KianaControl.h / AudioVolumeControl.h / AudioEQControl.h
//

#include <ntddk.h>
#include "KianaControl.h"        // IOCTL 宏（用 <devioctl.h>）
#include "AudioVolumeControl.h"  // g_DeviceVolume / g_Muted + VolumeControl_*
#include "AudioEQControl.h"      // EQControl_*（含 EQ_TEST_BUFFER_SIZE 与测试缓冲 extern）
#include "AudioEQTypes.h"

// ===== Test-only IOCTL（仅内部调试）=========================================
#define IOCTL_SEND_PCM CTL_CODE(FILE_DEVICE_UNKNOWN, 0x911, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_RECV_PCM CTL_CODE(FILE_DEVICE_UNKNOWN, 0x912, METHOD_BUFFERED, FILE_READ_DATA)

// 测试缓冲（在 AudioEQControl.cpp 定义；此处仅引用）
extern UCHAR g_EqTestBuffer[EQ_TEST_BUFFER_SIZE];
extern ULONG g_EqTestSize;

// ===== 统一日志宏 ===========================================================
#ifndef LOG_TAG
#   define LOG_TAG "KIANA/IOCTL"
#endif
#ifndef LOG_DBG
#   define LOG_DBG(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,  "[" LOG_TAG "] " fmt "\n", __VA_ARGS__)
#endif
#ifndef LOG_WRN
#   define LOG_WRN(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,"[" LOG_TAG "] " fmt "\n", __VA_ARGS__)
#endif
#ifndef LOG_ERR
#   define LOG_ERR(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,  "[" LOG_TAG "] " fmt "\n", __VA_ARGS__)
#endif

// ===== 助手：完成 IRP =======================================================
static __forceinline NTSTATUS _Complete(_Inout_ PIRP Irp, _In_ NTSTATUS Status, _In_ ULONG_PTR Info = 0)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

// ===== 助手：IOCTL 名称（便于阅读日志） ======================================
static const char* _IoctlName(ULONG code)
{
    switch (code)
    {
    case IOCTL_KIANA_GET_VERSION:   return "GET_VERSION";
    case IOCTL_KIANA_SET_VOLUME:    return "SET_VOLUME";
    case IOCTL_KIANA_GET_VOLUME:    return "GET_VOLUME";
    case IOCTL_KIANA_MUTE:          return "MUTE";
    case IOCTL_KIANA_UNMUTE:        return "UNMUTE";
    case IOCTL_KIANA_SET_EQ_BIQUAD: return "SET_EQ_BIQUAD";
    case IOCTL_KIANA_GET_EQ_BIQUAD: return "GET_EQ_BIQUAD";
    case IOCTL_SEND_PCM:            return "SEND_PCM(test)";
    case IOCTL_RECV_PCM:            return "RECV_PCM(test)";
    default:                        return "UNKNOWN";
    }
}

// ===== 助手：范围/结构校验 ===================================================
static __forceinline BOOLEAN _CheckLen(_In_opt_ PVOID buf, _In_ ULONG actual, _In_ ULONG need, _In_ BOOLEAN isWrite)
{
    if (!buf) return FALSE;
    if (isWrite) return actual >= need;
    return actual >= need;
}

static __forceinline void _DumpEqHeader(_In_ const EQCoeffParams* p)
{
    if (!p) return;
    LOG_DBG("EQ hdr: BandCount=%ld (expect 1..%d), sizeof(EQCoeffParams)=%u",
            p->BandCount, EQ_BANDS, (unsigned)sizeof(EQCoeffParams));
}

static __forceinline void _DumpEqBandShort(_In_ const EQCoeffParams* p, int showMax)
{
    if (!p) return;
    int n = (int)p->BandCount;
    if (n > EQ_BANDS) n = EQ_BANDS;
    if (showMax > n) showMax = n;
    for (int i = 0; i < showMax; ++i)
    {
        const EQBandCoeffs* c = &p->Bands[i];
        LOG_DBG("EQ band[%02d]: b0=%ld b1=%ld b2=%ld a1=%ld a2=%ld", i, c->b0, c->b1, c->b2, c->a1, c->a2);
    }
    if (n > showMax) LOG_DBG("EQ ... (%d more band(s) omitted)", n - showMax);
}

// ===== CREATE/CLOSE：简单成功 ================================================
NTSTATUS AudioVolumeControl_DispatchCreate(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    LOG_DBG("CREATE");
    return _Complete(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS AudioVolumeControl_DispatchClose(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    LOG_DBG("CLOSE");
    return _Complete(Irp, STATUS_SUCCESS, 0);
}

// ===== 统一 IOCTL 分发 ======================================================
NTSTATUS AudioVolumeControl_DispatchIoControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(Irp);
    const ULONG code   = sp->Parameters.DeviceIoControl.IoControlCode;
    const ULONG inLen  = sp->Parameters.DeviceIoControl.InputBufferLength;
    const ULONG outLen = sp->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID buf          = Irp->AssociatedIrp.SystemBuffer;

    NTSTATUS  status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR info   = 0;

    const KIRQL irql = KeGetCurrentIrql();
    LARGE_INTEGER ts; KeQuerySystemTimePrecise(&ts);

    LOG_DBG("IOCTL[enter]: 0x%08lX(%s) irql=%lu ts=%I64u in=%lu out=%lu buf=%p",
            code, _IoctlName(code), (ULONG)irql, (unsigned long long)ts.QuadPart, inLen, outLen, buf);

    switch (code)
    {
    case IOCTL_KIANA_GET_VERSION:
    {
        static const char ver[] = "Kiana KS EQ Driver v1.0";
        if (!_CheckLen(buf, outLen, (ULONG)sizeof(ver), FALSE)) {
            status = STATUS_BUFFER_TOO_SMALL;
            LOG_ERR("GET_VERSION: outLen=%lu need=%zu -> STATUS_BUFFER_TOO_SMALL", outLen, sizeof(ver));
            break;
        }
        RtlCopyMemory(buf, ver, sizeof(ver));
        info   = sizeof(ver);
        status = STATUS_SUCCESS;
        LOG_DBG("GET_VERSION: write %zu bytes", sizeof(ver));
        break;
    }

    case IOCTL_KIANA_SET_VOLUME:
    {
        if (!_CheckLen(buf, inLen, sizeof(ULONG), TRUE)) {
            status = STATUS_BUFFER_TOO_SMALL;
            LOG_ERR("SET_VOLUME: inLen=%lu need=%zu -> STATUS_BUFFER_TOO_SMALL", inLen, sizeof(ULONG));
            break;
        }
        ULONG v = *(ULONG*)buf;
        ULONG old = g_DeviceVolume;
        if (v > 100) v = 100;
        g_DeviceVolume = v;
        status = STATUS_SUCCESS;
        LOG_DBG("SET_VOLUME: %lu%% (old=%lu%%)", g_DeviceVolume, old);
        break;
    }

    case IOCTL_KIANA_GET_VOLUME:
    {
        if (!_CheckLen(buf, outLen, sizeof(ULONG), FALSE)) {
            status = STATUS_BUFFER_TOO_SMALL;
            LOG_ERR("GET_VOLUME: outLen=%lu need=%zu -> STATUS_BUFFER_TOO_SMALL", outLen, sizeof(ULONG));
            break;
        }
        *(ULONG*)buf = g_DeviceVolume;
        info   = sizeof(ULONG);
        status = STATUS_SUCCESS;
        LOG_DBG("GET_VOLUME: %lu%%", g_DeviceVolume);
        break;
    }

    case IOCTL_KIANA_MUTE:
    {
        BOOLEAN old = g_Muted;
        g_Muted = TRUE;
        status = STATUS_SUCCESS;
        LOG_DBG("MUTE: %s -> TRUE", old ? "TRUE" : "FALSE");
        break;
    }

    case IOCTL_KIANA_UNMUTE:
    {
        BOOLEAN old = g_Muted;
        g_Muted = FALSE;
        status = STATUS_SUCCESS;
        LOG_DBG("UNMUTE: %s -> FALSE", old ? "TRUE" : "FALSE");
        break;
    }

    // ===== EQ：12 段 biquad 系数（Q15）===================================
    case IOCTL_KIANA_SET_EQ_BIQUAD:
    {
        const ULONG need = sizeof(EQCoeffParams);
        if (!_CheckLen(buf, inLen, need, TRUE)) {
            status = STATUS_BUFFER_TOO_SMALL;
            LOG_ERR("SET_EQ: inLen=%lu need=%lu -> STATUS_BUFFER_TOO_SMALL", inLen, need);
            break;
        }
        EQCoeffParams* p = (EQCoeffParams*)buf;
        _DumpEqHeader(p);
        _DumpEqBandShort(p, 4); // 只预览前 4 段避免刷屏；需要可改大

        EQControl_SetBiquadCoeffs(p);
        status = STATUS_SUCCESS;
        LOG_DBG("SET_EQ: ok (BandCount=%ld)", p->BandCount);
        break;
    }

    case IOCTL_KIANA_GET_EQ_BIQUAD:
    {
        const ULONG need = sizeof(EQCoeffParams);
        if (!_CheckLen(buf, outLen, need, FALSE)) {
            status = STATUS_BUFFER_TOO_SMALL;
            LOG_ERR("GET_EQ: outLen=%lu need=%lu -> STATUS_BUFFER_TOO_SMALL", outLen, need);
            break;
        }
        EQCoeffParams* p = (EQCoeffParams*)buf;
        EQControl_GetBiquadCoeffs(p);
        info   = need;
        status = STATUS_SUCCESS;
        LOG_DBG("GET_EQ: ok (BandCount=%ld, write=%luB)", p->BandCount, need);
        break;
    }

    // ===== 内部调试：送/取原始 PCM =====================================
    case IOCTL_SEND_PCM:
    {
        if (!buf || inLen == 0) {
            status = STATUS_INVALID_PARAMETER;
            LOG_ERR("SEND_PCM: invalid buf/len (buf=%p inLen=%lu)", buf, inLen);
            break;
        }

        ULONG take = inLen;
        if (take > EQ_TEST_BUFFER_SIZE) {
            LOG_WRN("SEND_PCM: truncate inLen=%lu -> %u (EQ_TEST_BUFFER_SIZE)", inLen, (unsigned)EQ_TEST_BUFFER_SIZE);
            take = EQ_TEST_BUFFER_SIZE;
        }
        RtlCopyMemory(g_EqTestBuffer, buf, take);
        g_EqTestSize = take;
        status = STATUS_SUCCESS;
        // METHOD_BUFFERED/写：info 保持 0 即可；这里也打印记录
        LOG_DBG("SEND_PCM: stored=%luB (cap=%uB)", g_EqTestSize, (unsigned)EQ_TEST_BUFFER_SIZE);
        break;
    }

    case IOCTL_RECV_PCM:
    {
        if (!buf || outLen == 0) {
            status = STATUS_INVALID_PARAMETER;
            LOG_ERR("RECV_PCM: invalid buf/len (buf=%p outLen=%lu)", buf, outLen);
            break;
        }
        if (outLen < g_EqTestSize) {
            status = STATUS_BUFFER_TOO_SMALL;
            LOG_ERR("RECV_PCM: outLen=%lu < have=%lu -> STATUS_BUFFER_TOO_SMALL", outLen, g_EqTestSize);
            break;
        }
        RtlCopyMemory(buf, g_EqTestBuffer, g_EqTestSize);
        info   = g_EqTestSize;
        status = STATUS_SUCCESS;
        LOG_DBG("RECV_PCM: copied=%luB", g_EqTestSize);
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        LOG_ERR("UNKNOWN IOCTL: 0x%08lX", code);
        break;
    }

    LOG_DBG("IOCTL[done ]: 0x%08lX(%s) status=0x%08X info=%Iu",
            code, _IoctlName(code), status, info);

    return _Complete(Irp, status, info);
}
