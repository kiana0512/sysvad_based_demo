#include <windows.h>
#include <iostream>
#include <string>

#define IOCTL_SET_EQ_PARAMS  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_EQ_PARAMS  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define MAX_EQ_BANDS 12

// EQ 参数结构体（需与驱动中保持一致）
typedef struct _EQBandParam {
    int FrequencyHz;
    float GainDb;
    float Q;
} EQBandParam;

typedef struct _EQControlParams {
    EQBandParam Bands[MAX_EQ_BANDS];
    int BandCount;
} EQControlParams;

int main()
{
    HANDLE hDevice = CreateFileA(
        R"(\\.\SysvadControl)",           // 符号链接路径（与驱动一致）
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hDevice == INVALID_HANDLE_VALUE)
    {
        std::cerr << "[ERROR] 打开设备失败: " << GetLastError() << std::endl;
        return 1;
    }

    std::cout << "[INFO] 成功打开驱动 IOCTL 接口。\n";

    // 构造 EQ 设置参数
    EQControlParams eqParams = {};
    eqParams.BandCount = MAX_EQ_BANDS;

    int freqs[MAX_EQ_BANDS] = { 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000, 18000, 20000 };

    for (int i = 0; i < MAX_EQ_BANDS; ++i)
    {
        eqParams.Bands[i].FrequencyHz = freqs[i];
        eqParams.Bands[i].GainDb = (i % 2 == 0) ? 6.0f : -3.0f;  // 偶数段+6dB，奇数段-3dB
        eqParams.Bands[i].Q = 1.0f;
    }

    DWORD bytesReturned = 0;
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_SET_EQ_PARAMS,
        &eqParams,
        sizeof(eqParams),
        nullptr,
        0,
        &bytesReturned,
        nullptr
    );

    if (!success)
    {
        std::cerr << "[ERROR] 设置 EQ 失败: " << GetLastError() << std::endl;
        CloseHandle(hDevice);
        return 1;
    }

    std::cout << "[INFO] 成功发送 EQ 设置参数。\n";

    // 再读取回来校验
    EQControlParams eqReadback = {};
    success = DeviceIoControl(
        hDevice,
        IOCTL_GET_EQ_PARAMS,
        nullptr,
        0,
        &eqReadback,
        sizeof(eqReadback),
        &bytesReturned,
        nullptr
    );

    if (!success)
    {
        std::cerr << "[ERROR] 获取 EQ 失败: " << GetLastError() << std::endl;
        CloseHandle(hDevice);
        return 1;
    }

    std::cout << "[INFO] 驱动返回 EQ 设置如下：\n";
    for (int i = 0; i < eqReadback.BandCount; ++i)
    {
        std::cout << "  Band " << i << ": F=" << eqReadback.Bands[i].FrequencyHz
                  << " Hz, Gain=" << eqReadback.Bands[i].GainDb << " dB, Q=" << eqReadback.Bands[i].Q << "\n";
    }

    CloseHandle(hDevice);
    return 0;
}
