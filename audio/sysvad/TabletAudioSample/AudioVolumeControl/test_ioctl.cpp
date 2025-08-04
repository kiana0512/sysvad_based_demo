#include <windows.h>
#include <iostream>

#define IOCTL_SET_VOLUME    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_VOLUME    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MUTE_AUDIO    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_UNMUTE_AUDIO  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

int main()
{
    HANDLE hDevice = CreateFileW(L"\\\\.\\SysvadControl",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (hDevice == INVALID_HANDLE_VALUE)
    {
        std::cerr << "打开设备失败: " << GetLastError() << std::endl;
        return 1;
    }

    // 设置音量为 60
    ULONG volume = 60;
    DWORD returned = 0;
    if (DeviceIoControl(hDevice, IOCTL_SET_VOLUME, &volume, sizeof(volume),
                        nullptr, 0, &returned, nullptr))
    {
        std::cout << "设置音量成功\n";
    }
    else
    {
        std::cerr << "设置音量失败: " << GetLastError() << std::endl;
    }

    // 获取当前音量
    ULONG currVol = 0;
    if (DeviceIoControl(hDevice, IOCTL_GET_VOLUME,
                        nullptr, 0,
                        &currVol, sizeof(currVol),
                        &returned, nullptr))
    {
        std::cout << "当前音量: " << currVol << std::endl;
    }
    else
    {
        std::cerr << "获取音量失败: " << GetLastError() << std::endl;
    }

    // 设置静音
    if (DeviceIoControl(hDevice, IOCTL_MUTE_AUDIO,
                        nullptr, 0, nullptr, 0, &returned, nullptr))
    {
        std::cout << "静音成功\n";
    }

    // 取消静音
    if (DeviceIoControl(hDevice, IOCTL_UNMUTE_AUDIO,
                        nullptr, 0, nullptr, 0, &returned, nullptr))
    {
        std::cout << "取消静音成功\n";
    }

    CloseHandle(hDevice);
    return 0;
}
