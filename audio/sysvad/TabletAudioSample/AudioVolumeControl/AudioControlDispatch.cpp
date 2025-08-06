#include "AudioVolumeControl.h"
#include "AudioEQControl.h"
#include <ntddk.h>
#include <wdm.h>

NTSTATUS AudioVolumeControl_DispatchIoControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR info = 0;

    ULONG inputLength = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outputLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

    switch (irpSp->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_SET_VOLUME:
        DbgPrint("IOCTL_SET_VOLUME called. InputLen = %lu", inputLength);

        if (Irp->AssociatedIrp.SystemBuffer == NULL)
        {
            DbgPrint("SystemBuffer is NULL!");
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (inputLength >= sizeof(ULONG))
        {
            ULONG newVolume = *(ULONG *)Irp->AssociatedIrp.SystemBuffer;

            // 调试打印原始 buffer（可选）
            UCHAR *p = (UCHAR *)Irp->AssociatedIrp.SystemBuffer;
            DbgPrint("Raw buffer: %02X %02X %02X %02X", p[0], p[1], p[2], p[3]);

            if (newVolume <= 100)
            {
                g_DeviceVolume = newVolume;
                DbgPrint("Set volume: %lu", g_DeviceVolume);
                status = STATUS_SUCCESS;
            }
            else
            {
                DbgPrint("Invalid volume: %lu", newVolume);
                status = STATUS_INVALID_PARAMETER;
            }
        }
        else
        {
            DbgPrint("Input buffer too small for IOCTL_SET_VOLUME. Expected: %lu, Got: %lu", sizeof(ULONG), inputLength);
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;

    case IOCTL_GET_VOLUME:
        if (outputLength >= sizeof(ULONG))
        {
            *(ULONG *)Irp->AssociatedIrp.SystemBuffer = g_DeviceVolume;
            info = sizeof(ULONG);
            DbgPrint("Get volume: %lu", g_DeviceVolume);
            status = STATUS_SUCCESS;
        }
        break;

    case IOCTL_MUTE_AUDIO:
        g_Muted = TRUE;
        DbgPrint("Audio muted.");
        status = STATUS_SUCCESS;
        break;

    case IOCTL_UNMUTE_AUDIO:
        g_Muted = FALSE;
        DbgPrint("Audio unmuted.");
        status = STATUS_SUCCESS;
        break;

    // case IOCTL_SET_EQ_PARAMS:
    //     DbgPrint("[EQ] IOCTL_SET_EQ_PARAMS received, inputLength=%lu", inputLength);

    //     if (Irp->AssociatedIrp.SystemBuffer == NULL)
    //     {
    //         DbgPrint("[ERR] EQ Set Params: SystemBuffer is NULL");
    //         status = STATUS_INVALID_PARAMETER;
    //         break;
    //     }

    //     if (inputLength >= sizeof(EQControlParams))
    //     {
    //         EQControlParams *inParams = (EQControlParams *)Irp->AssociatedIrp.SystemBuffer;

    //         DbgPrint("[EQ] BandCount = %d", inParams->BandCount);
    //         for (int i = 0; i < inParams->BandCount && i < EQ_BANDS; ++i)
    //         {
    //             DbgPrint("[EQ] Band[%d]: Freq=%d Hz, Gain=%.2f dB, Q=%.2f",
    //                      i,
    //                      inParams->Bands[i].FrequencyHz,
    //                      inParams->Bands[i].GainDb,
    //                      inParams->Bands[i].Q);
    //         }

    //         EQControl_SetParams(inParams);
    //         status = STATUS_SUCCESS;
    //     }
    //     else
    //     {
    //         DbgPrint("[ERR] EQ Set Params: Buffer too small. Got %lu, need %lu",
    //                  inputLength, sizeof(EQControlParams));
    //         status = STATUS_INVALID_PARAMETER;
    //     }
    //     break;

    // case IOCTL_GET_EQ_PARAMS:
    //     if (outputLength >= sizeof(EQControlParams))
    //     {
    //         EQControlParams *outParams = (EQControlParams *)Irp->AssociatedIrp.SystemBuffer;
    //         EQControl_GetParams(outParams);
    //         info = sizeof(EQControlParams);
    //         DbgPrint("EQ params read. BandCount=%d\n", outParams->BandCount);
    //         status = STATUS_SUCCESS;
    //     }
    //     else
    //     {
    //         DbgPrint("Invalid EQ get params buffer\n");
    //         status = STATUS_INVALID_PARAMETER;
    //     }
    //     break;
    case IOCTL_SET_EQ_BIQUAD_COEFFS:
        if (inputLength >= sizeof(EQCoeffParams))
        {
            EQCoeffParams *inCoeffs = (EQCoeffParams *)Irp->AssociatedIrp.SystemBuffer;

            DbgPrint("[EQ] === IOCTL_SET_EQ_BIQUAD_COEFFS ===\n");
            DbgPrint("[EQ] BandCount = %d\n", inCoeffs->BandCount);

            for (int i = 0; i < inCoeffs->BandCount && i < EQ_BANDS; ++i)
            {
                DbgPrint("[EQ] Band[%02d] Coeffs:\n", i);
                DbgPrint("      b0 = %8d\tb1 = %8d\tb2 = %8d\n",
                         inCoeffs->Bands[i].b0,
                         inCoeffs->Bands[i].b1,
                         inCoeffs->Bands[i].b2);
                DbgPrint("      a1 = %8d\ta2 = %8d\n",
                         inCoeffs->Bands[i].a1,
                         inCoeffs->Bands[i].a2);
            }

            EQControl_SetBiquadCoeffs(inCoeffs);
            DbgPrint("[EQ] => EQ Coefficients Applied Successfully\n");

            status = STATUS_SUCCESS;
            info = 0;
        }
        else
        {
            DbgPrint("[EQ] [ERROR] IOCTL_SET_EQ_BIQUAD_COEFFS: Buffer too small (%lu < %lu)\n",
                     inputLength, sizeof(EQCoeffParams));
            status = STATUS_INVALID_PARAMETER;
        }
        break;
    case IOCTL_GET_EQ_BIQUAD_COEFFS:
        if (outputLength >= sizeof(EQCoeffParams))
        {
            EQCoeffParams *outCoeffs = (EQCoeffParams *)Irp->AssociatedIrp.SystemBuffer;

            EQControl_GetBiquadCoeffs(outCoeffs);
            DbgPrint("[EQ] === IOCTL_GET_EQ_BIQUAD_COEFFS ===\n");
            DbgPrint("[EQ] Returning BandCount = %d\n", outCoeffs->BandCount);

            for (int i = 0; i < outCoeffs->BandCount && i < EQ_BANDS; ++i)
            {
                DbgPrint("[EQ] Band[%02d] Coeffs:\n", i);
                DbgPrint("      b0 = %8d\tb1 = %8d\tb2 = %8d\n",
                         outCoeffs->Bands[i].b0,
                         outCoeffs->Bands[i].b1,
                         outCoeffs->Bands[i].b2);
                DbgPrint("      a1 = %8d\ta2 = %8d\n",
                         outCoeffs->Bands[i].a1,
                         outCoeffs->Bands[i].a2);
            }

            status = STATUS_SUCCESS;
            info = sizeof(EQCoeffParams);
        }
        else
        {
            DbgPrint("[EQ] [ERROR] IOCTL_GET_EQ_BIQUAD_COEFFS: Buffer too small (%lu < %lu)\n",
                     outputLength, sizeof(EQCoeffParams));
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;

    case IOCTL_SEND_PCM:
        if (Irp->AssociatedIrp.SystemBuffer == NULL || inputLength == 0)
        {
            DbgPrint("[EQTEST] IOCTL_SEND_PCM: Invalid input buffer.");
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (inputLength > EQ_TEST_BUFFER_SIZE)
        {
            DbgPrint("[EQTEST] IOCTL_SEND_PCM: Buffer too large (%lu bytes)", inputLength);
            status = STATUS_BUFFER_OVERFLOW;
            break;
        }

        // 保存数据供调试或用户态回读
        RtlCopyMemory(g_EqTestBuffer, Irp->AssociatedIrp.SystemBuffer, inputLength);
        g_EqTestSize = inputLength;

        DbgPrint("[EQTEST] IOCTL_SEND_PCM saved raw PCM (%lu bytes)", inputLength);
        status = STATUS_SUCCESS;
        break;

    case IOCTL_RECV_PCM:
        if (Irp->AssociatedIrp.SystemBuffer == NULL || outputLength == 0)
        {
            DbgPrint("[EQTEST] IOCTL_RECV_PCM: Invalid output buffer.");
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (outputLength < g_EqTestSize)
        {
            DbgPrint("[EQTEST] IOCTL_RECV_PCM: Output buffer too small. Need %lu bytes", g_EqTestSize);
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, g_EqTestBuffer, g_EqTestSize);
        info = g_EqTestSize;

        DbgPrint("[EQTEST] IOCTL_RECV_PCM: Returned raw PCM (%lu bytes)", g_EqTestSize);
        status = STATUS_SUCCESS;
        break;

    default:
        DbgPrint("Unknown IOCTL: 0x%X", irpSp->Parameters.DeviceIoControl.IoControlCode);
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    DbgPrint("[IOCTL] Done. Status=0x%08X, Info=%lu", status, info);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}
NTSTATUS AudioVolumeControl_DispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    DbgPrint("DispatchCreate called.");
    return STATUS_SUCCESS;
}

NTSTATUS AudioVolumeControl_DispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    DbgPrint("DispatchClose called.");
    return STATUS_SUCCESS;
}
