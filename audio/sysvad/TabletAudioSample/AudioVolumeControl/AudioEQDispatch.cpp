#include "AudioEQControl.h"
#include <ntddk.h>
inline int int_abs(int x)
{
    return (x < 0) ? -x : x;
}

// 自定义 EQ 控制的 IOCTL 码
#define IOCTL_SET_EQ_PARAMS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_EQ_PARAMS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)
// #define IOCTL_SET_EQ_GAIN_ARRAY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x903, METHOD_BUFFERED, FILE_ANY_ACCESS)

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
    case IOCTL_SET_EQ_PARAMS:
        if (irpSp->Parameters.DeviceIoControl.InputBufferLength >= sizeof(EQControlParams))
        {
            EQControlParams *inParams = (EQControlParams *)Irp->AssociatedIrp.SystemBuffer;

            DbgPrint("[EQ] IOCTL_SET_EQ_PARAMS received, InputLength=%lu\n",
                     irpSp->Parameters.DeviceIoControl.InputBufferLength);

            //  内核兼容浮点打印：Gain ×10, Q ×1000
            int gain10 = (int)(inParams->Bands[0].GainDb * 10.0f); // 例如 12.3 -> 123
            int q1000 = (int)(inParams->Bands[0].Q * 1000.0f);     // 例如 0.707 -> 707

            DbgPrint("[EQ] Band[0]: Freq=%d, Gain=%d.%01d dB, Q=0.%03d\n",
                     inParams->Bands[0].FrequencyHz,
                     gain10 / 10,
                     int_abs(gain10 % 10),
                     q1000);

            EQControl_SetParams(inParams);
            status = STATUS_SUCCESS;
            info = sizeof(EQControlParams); // 可选设置
        }
        else
        {
            DbgPrint("[EQ] IOCTL_SET_EQ_PARAMS: Invalid InputBufferLength (%lu)\n",
                     irpSp->Parameters.DeviceIoControl.InputBufferLength);
            status = STATUS_INVALID_PARAMETER;
        }
        break;

    case IOCTL_GET_EQ_PARAMS:
        if (irpSp->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(EQControlParams))
        {
            EQControlParams *outParams = (EQControlParams *)Irp->AssociatedIrp.SystemBuffer;

            EQControl_GetParams(outParams);
            info = sizeof(EQControlParams);
            status = STATUS_SUCCESS;
        }
        else
        {
            DbgPrint("[EQ] IOCTL_GET_EQ_PARAMS: Invalid OutputBufferLength (%lu)\n",
                     irpSp->Parameters.DeviceIoControl.OutputBufferLength);
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;

        // case IOCTL_SET_EQ_GAIN_ARRAY:
        // if (irpSp->Parameters.DeviceIoControl.InputBufferLength >= sizeof(int) * EQ_BANDS)
        // {
        //     int* inputArray = (int*)Irp->AssociatedIrp.SystemBuffer;
        //     for (int i = 0; i < EQ_BANDS; ++i)
        //     {
        //         if (inputArray[i] > 100) inputArray[i] = 100;
        //         if (inputArray[i] < -100) inputArray[i] = -100;
        //         eqGainInt[i] = inputArray[i];
        //     }

        //     DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        //         "[EQ] Updated eqGainInt: {%d, %d, ..., %d}\n",
        //         eqGainInt[0], eqGainInt[1], eqGainInt[EQ_BANDS - 1]);

        //     status = STATUS_SUCCESS;
        //     info = 0;
        // }
        // break;

    default:
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}
