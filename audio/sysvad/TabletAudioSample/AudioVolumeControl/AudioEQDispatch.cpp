#include "AudioEQControl.h"
#include <ntddk.h>

#define IOCTL_SET_EQ_BIQUAD_COEFFS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x910, METHOD_BUFFERED, FILE_ANY_ACCESS)

NTSTATUS EQDispatch_HandleIoControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG code = irpSp->Parameters.DeviceIoControl.IoControlCode;

    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR info = 0;

    switch (code)
    {
    case IOCTL_SET_EQ_BIQUAD_COEFFS:
        if (irpSp->Parameters.DeviceIoControl.InputBufferLength >= sizeof(EQCoeffParams))
        {
            EQCoeffParams* inCoeffs = (EQCoeffParams*)Irp->AssociatedIrp.SystemBuffer;

            DbgPrint("[EQ] IOCTL_SET_EQ_BIQUAD_COEFFS received, BandCount=%d\n", inCoeffs->BandCount);

            EQControl_SetBiquadCoeffs(inCoeffs);
            status = STATUS_SUCCESS;
            info = 0;
        }
        else
        {
            DbgPrint("[EQ] IOCTL_SET_EQ_BIQUAD_COEFFS: Invalid InputBufferLength (%lu)\n",
                     irpSp->Parameters.DeviceIoControl.InputBufferLength);
            status = STATUS_INVALID_PARAMETER;
        }
        break;

    default:
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}
