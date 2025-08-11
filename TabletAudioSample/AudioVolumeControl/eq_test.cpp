#pragma pack(push, 1)
typedef struct _EQBandParam
{
    int FrequencyHz;
    float GainDb;
    float Q;
} EQBandParam;

typedef struct _EQControlParams
{
    EQBandParam Bands[12];
    int BandCount;
} EQControlParams;
#pragma pack(pop)

// eq_test.cpp
#include <windows.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <string>

// 内嵌 IOCTL 和 EQ 参数定义，替代引入头文件
#define IOCTL_SET_EQ_PARAMS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_EQ_PARAMS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_EQ_GAIN_ARRAY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x903, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SEND_PCM CTL_CODE(FILE_DEVICE_UNKNOWN, 0x911, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_RECV_PCM CTL_CODE(FILE_DEVICE_UNKNOWN, 0x912, METHOD_BUFFERED, FILE_READ_DATA)
#define EQ_BANDS 12
#define SAMPLE_RATE 48000
#define PI 3.14159265358979323846

#define DEVICE_NAME L"\\\\.\\SysvadControl"

// 调试打印
void Log(const std::string &msg)
{
    std::cout << "[LOG] " << msg << std::endl;
}

// 生成 1kHz 正弦波 PCM，16-bit 单声道
std::vector<SHORT> generateSineWave(int frequency, int durationSec)
{
    int sampleCount = SAMPLE_RATE * durationSec;
    std::vector<SHORT> pcm(sampleCount);

    for (int i = 0; i < sampleCount; ++i)
    {
        double t = static_cast<double>(i) / SAMPLE_RATE;
        pcm[i] = static_cast<SHORT>(32767 * sin(2 * PI * frequency * t));
    }

    Log("Generated sine wave: " + std::to_string(sampleCount) + " samples");
    return pcm;
}

// 写入 WAV 文件
void writeWav(const std::string &filename, const std::vector<SHORT> &data)
{
    std::ofstream file(filename, std::ios::binary);

    int dataSize = data.size() * sizeof(SHORT);
    int fileSize = 44 + dataSize;

    // 写 WAV 头
    file.write("RIFF", 4);
    int chunkSize = fileSize - 8;
    file.write(reinterpret_cast<char *>(&chunkSize), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);

    int sampleRate = SAMPLE_RATE;
    int subchunk1Size = 16; // PCM
    short audioFormat = 1;  // PCM
    short numChannels = 1;
    int byteRate = SAMPLE_RATE * numChannels * sizeof(SHORT);
    short blockAlign = numChannels * sizeof(SHORT);
    short bitsPerSample = 16;

    file.write(reinterpret_cast<char *>(&subchunk1Size), 4);
    file.write(reinterpret_cast<char *>(&audioFormat), 2);
    file.write(reinterpret_cast<char *>(&numChannels), 2);
    file.write(reinterpret_cast<char *>(&sampleRate), 4);
    file.write(reinterpret_cast<char *>(&byteRate), 4);
    file.write(reinterpret_cast<char *>(&blockAlign), 2);
    file.write(reinterpret_cast<char *>(&bitsPerSample), 2);

    file.write("data", 4);
    file.write(reinterpret_cast<char *>(&dataSize), 4);
    file.write(reinterpret_cast<const char *>(data.data()), dataSize);
    file.close();

    Log("Saved WAV: " + filename);
}

// 发送 EQ 参数
bool sendEQParams(HANDLE hDevice, const EQControlParams &params)
{
    DWORD bytesReturned;
    BOOL ok = DeviceIoControl(
        hDevice,
        IOCTL_SET_EQ_PARAMS,
        (LPVOID)&params,
        sizeof(params),
        nullptr,
        0,
        &bytesReturned,
        nullptr);
    Log("Send EQ params: " + std::string(ok ? "SUCCESS" : "FAILED"));
    std::cout << "sizeof(EQControlParams): " << sizeof(EQControlParams) << std::endl;
    return ok;
}

// 发送原始 PCM 到驱动
bool sendPCM(HANDLE hDevice, const std::vector<SHORT> &pcm)
{
    DWORD bytesReturned;
    BOOL ok = DeviceIoControl(
        hDevice,
        IOCTL_SEND_PCM,
        (LPVOID)pcm.data(),
        pcm.size() * sizeof(SHORT),
        nullptr,
        0,
        &bytesReturned,
        nullptr);
    Log("Send PCM: " + std::string(ok ? "SUCCESS" : "FAILED"));
    return ok;
}

// 从驱动接收 EQ 处理后的 PCM
bool recvPCM(HANDLE hDevice, std::vector<SHORT> &pcmOut, size_t expectedSamples)
{
    pcmOut.resize(expectedSamples);
    DWORD bytesReturned;
    BOOL ok = DeviceIoControl(
        hDevice,
        IOCTL_RECV_PCM,
        nullptr,
        0,
        (LPVOID)pcmOut.data(),
        pcmOut.size() * sizeof(SHORT),
        &bytesReturned,
        nullptr);
    Log("Receive PCM: " + std::string(ok ? "SUCCESS" : "FAILED"));
    if (ok)
    {
        Log("Received " + std::to_string(bytesReturned) + " bytes from driver");
        pcmOut.resize(bytesReturned / sizeof(SHORT)); // 实际样本数
    }
    return ok;
}

int main()
{
    Log("Opening device: \\\\.\\SysvadControl");
    HANDLE hDevice = CreateFileW(
        DEVICE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);

    if (hDevice == INVALID_HANDLE_VALUE)
    {
        Log("Failed to open device");
        return 1;
    }

    // 设置 EQ 参数（举例：第 5 段 +12dB，其它 0dB）
    EQControlParams eqParams = {};
    eqParams.BandCount = EQ_BANDS;
    // 和驱动 defaultFreqs 一致
    int defaultFreqs[EQ_BANDS] = {
        60, 170, 310, 600, 1000, 3000,
        6000, 12000, 14000, 16000, 18000, 20000};

    for (int i = 0; i < EQ_BANDS; ++i)
    {
        eqParams.Bands[i].FrequencyHz = defaultFreqs[i];
        eqParams.Bands[i].GainDb = (i == 4) ? 12.0f : -12.0f;
        eqParams.Bands[i].Q = 0.3f;
    }

    if (!sendEQParams(hDevice, eqParams))
        return 1;

    // 生成 1kHz 测试波
    auto originalPCM = generateSineWave(1000, 2);
    if (!sendPCM(hDevice, originalPCM))
        return 1;

    // 接收 EQ 处理后的 PCM
    std::vector<SHORT> processedPCM;
    if (!recvPCM(hDevice, processedPCM, originalPCM.size()))
        return 1;

    // 保存 WAV 文件
    writeWav("original.wav", originalPCM);
    writeWav("eq_result.wav", processedPCM);

    Log("All done. Compare the two .wav files in Audacity.");
    Log("All done. You should now see original.wav and eq_result.wav in the folder.");
    system("explorer ."); // 自动打开当前文件夹

    CloseHandle(hDevice);
    return 0;
}
