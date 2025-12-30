#define NOMINMAX  // Prevent Windows.h from defining min/max macros

#include "asio_host.h"
#include <combaseapi.h>
#include <initguid.h>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <cmath>

// ASIO Interface definitions (COM-based, no SDK needed)

#pragma pack(push, 4)

struct ASIODriverInfo {
    long asioVersion;
    long driverVersion;
    char name[32];
    char errorMessage[124];
    void* sysRef;
};

struct ASIOClockSource {
    long index;
    long associatedChannel;
    long associatedGroup;
    long isCurrentSource;
    char name[32];
};

struct ASIOChannelInfo {
    long channel;
    long isInput;
    long isActive;
    long channelGroup;
    ASIOSampleType type;
    char name[32];
};

struct ASIOBufferInfo {
    long isInput;
    long channelNum;
    void* buffers[2];
};

struct ASIOTime {
    long reserved[4];
    struct {
        double speed;
        long long timeCodeSamples;
        unsigned long flags;
        char future[64];
    } timeCode;
    struct {
        double samplePosition;
        double sampleRate;
        long long nanoSeconds;
        long long samples;
        unsigned long flags;
        char future[12];
    } timeInfo;
};

struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, long directProcess);
    void (*sampleRateDidChange)(double sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* params, long doubleBufferIndex, long directProcess);
};

#pragma pack(pop)

// ASIO message selectors
enum {
    kAsioSelectorSupported = 1,
    kAsioEngineVersion,
    kAsioResetRequest,
    kAsioBufferSizeChange,
    kAsioResyncRequest,
    kAsioLatenciesChanged,
    kAsioSupportsTimeInfo,
    kAsioSupportsTimeCode,
    kAsioSupportsInputMonitor
};

// IASIO interface
class IASIO : public IUnknown {
public:
    virtual long init(void* sysHandle) = 0;
    virtual void getDriverName(char* name) = 0;
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char* string) = 0;
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    virtual ASIOError getChannels(long* numInputChannels, long* numOutputChannels) = 0;
    virtual ASIOError getLatencies(long* inputLatency, long* outputLatency) = 0;
    virtual ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) = 0;
    virtual ASIOError canSampleRate(double sampleRate) = 0;
    virtual ASIOError getSampleRate(double* sampleRate) = 0;
    virtual ASIOError setSampleRate(double sampleRate) = 0;
    virtual ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) = 0;
    virtual ASIOError setClockSource(long reference) = 0;
    virtual ASIOError getSamplePosition(long long* sPos, long long* tStamp) = 0;
    virtual ASIOError getChannelInfo(ASIOChannelInfo* info) = 0;
    virtual ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) = 0;
    virtual ASIOError disposeBuffers() = 0;
    virtual ASIOError controlPanel() = 0;
    virtual ASIOError future(long selector, void* opt) = 0;
    virtual ASIOError outputReady() = 0;
};

// Static instance
ASIOHost* ASIOHost::instance = nullptr;

ASIOHost::ASIOHost() {
    CoInitialize(nullptr);
    instance = this;
}

ASIOHost::~ASIOHost() {
    stop();
    disposeBuffers();
    unloadDriver();
    CoUninitialize();
    if (instance == this) {
        instance = nullptr;
    }
}

std::vector<DriverInfo> ASIOHost::getDriverList() {
    std::vector<DriverInfo> drivers;
    
    HKEY asioKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &asioKey) != ERROR_SUCCESS) {
        return drivers;
    }

    char keyName[256];
    DWORD keyIndex = 0;
    DWORD keyNameSize;

    while (true) {
        keyNameSize = sizeof(keyName);
        if (RegEnumKeyExA(asioKey, keyIndex++, keyName, &keyNameSize, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }

        HKEY driverKey;
        if (RegOpenKeyExA(asioKey, keyName, 0, KEY_READ, &driverKey) == ERROR_SUCCESS) {
            char clsidStr[64];
            DWORD clsidSize = sizeof(clsidStr);
            DWORD type;
            
            if (RegQueryValueExA(driverKey, "CLSID", nullptr, &type, (LPBYTE)clsidStr, &clsidSize) == ERROR_SUCCESS) {
                DriverInfo info;
                info.name = keyName;
                
                wchar_t wclsid[64];
                MultiByteToWideChar(CP_ACP, 0, clsidStr, -1, wclsid, 64);
                CLSIDFromString(wclsid, &info.clsid);
                
                drivers.push_back(info);
            }
            RegCloseKey(driverKey);
        }
    }

    RegCloseKey(asioKey);
    return drivers;
}

bool ASIOHost::loadDriver(const std::string& name) {
    unloadDriver();
    
    auto drivers = getDriverList();
    CLSID clsid = {0};
    bool found = false;
    
    for (const auto& driver : drivers) {
        if (driver.name == name) {
            clsid = driver.clsid;
            found = true;
            break;
        }
    }
    
    if (!found) {
        return false;
    }
    
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, &asioDriver);
    if (FAILED(hr)) {
        return false;
    }
    
    driverName = name;
    return true;
}

void ASIOHost::unloadDriver() {
    if (asioDriver) {
        ((IUnknown*)asioDriver)->Release();
        asioDriver = nullptr;
    }
    driverName.clear();
    initialized = false;
    inputChannelNames.clear();
    outputChannelNames.clear();
    inputSampleTypes.clear();
    outputSampleTypes.clear();
    routes.clear();
}

bool ASIOHost::initialize(HWND hwnd) {
    if (!asioDriver) {
        return false;
    }
    
    IASIO* drv = (IASIO*)asioDriver;
    
    if (drv->init(hwnd) != 1) {
        return false;
    }
    
    // Get channel counts
    long inputs, outputs;
    if (drv->getChannels(&inputs, &outputs) != ASE_OK) {
        return false;
    }
    numInputs = inputs;
    numOutputs = outputs;
    
    // Get sample rate
    drv->getSampleRate(&sampleRate);
    
    // Get channel names and sample types
    inputChannelNames.resize(numInputs);
    outputChannelNames.resize(numOutputs);
    inputSampleTypes.resize(numInputs);
    outputSampleTypes.resize(numOutputs);
    
    for (int i = 0; i < numInputs; i++) {
        ASIOChannelInfo info;
        info.channel = i;
        info.isInput = 1;
        if (drv->getChannelInfo(&info) == ASE_OK) {
            inputChannelNames[i] = info.name;
            inputSampleTypes[i] = info.type;
        } else {
            inputChannelNames[i] = "Input " + std::to_string(i + 1);
            inputSampleTypes[i] = ASIOSTInt32LSB;
        }
    }
    
    for (int i = 0; i < numOutputs; i++) {
        ASIOChannelInfo info;
        info.channel = i;
        info.isInput = 0;
        if (drv->getChannelInfo(&info) == ASE_OK) {
            outputChannelNames[i] = info.name;
            outputSampleTypes[i] = info.type;
        } else {
            outputChannelNames[i] = "Output " + std::to_string(i + 1);
            outputSampleTypes[i] = ASIOSTInt32LSB;
        }
    }
    
    initialized = true;
    return true;
}

bool ASIOHost::isHardwareChannelName(const std::string& name) const {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Common hardware/driver identifiers
    const char* hwPatterns[] = {
        "asio4all", "asio 4 all",
        "realtek", "nvidia", "amd", "intel",
        "usb", "hdmi", "spdif", "optical",
        "focusrite", "scarlett", "steinberg", "yamaha",
        "motu", "rme", "universal audio", "presonus",
        "behringer", "native instruments", "m-audio",
        "flexasio", "wasapi", "wdm",
        "speaker", "headphone", "line out", "line in",
        "microphone", "mic in", "aux",
        "topping", "fiio", "schiit", "jds", "geshelli",
        "not connected", "disconnected"
    };
    
    for (const auto& pattern : hwPatterns) {
        if (lower.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    // Check if it's just a number or "Ch X" pattern
    if (lower.find("ch ") == 0 || lower.find("ch") == 0) {
        return true;
    }
    
    // Check if mostly digits (like "01" or "1-2")
    int digitCount = 0;
    for (char c : name) {
        if (isdigit(c) || c == '-' || c == ' ') digitCount++;
    }
    if (name.length() > 0 && digitCount >= name.length() / 2) {
        return true;
    }
    
    return false;
}

bool ASIOHost::isVirtualEndpointName(const std::string& name) const {
    // If it's clearly hardware, it's not virtual
    if (isHardwareChannelName(name)) {
        return false;
    }
    
    // SAR virtual endpoints have user-defined names
    // They typically don't match hardware patterns
    // Common user names: "System Audio", "Game", "Discord", "Music", "Browser", etc.
    
    // If the name is descriptive and not hardware-like, assume it's a virtual endpoint
    return name.length() > 0;
}

void ASIOHost::detectRouting() {
    routes.clear();
    
    // Strategy:
    // 1. Find all input channels that look like virtual endpoints (SAR playback endpoints)
    // 2. Find all output channels that look like hardware outputs
    // 3. Route virtual inputs to hardware outputs in order
    
    std::vector<int> virtualInputs;
    std::vector<int> hardwareOutputs;
    
    // Categorize inputs
    for (int i = 0; i < numInputs; i++) {
        if (isVirtualEndpointName(inputChannelNames[i])) {
            virtualInputs.push_back(i);
        }
    }
    
    // If no virtual inputs detected, assume ALL inputs are virtual
    // (This happens when the underlying driver has no physical inputs, common for DACs)
    if (virtualInputs.empty()) {
        for (int i = 0; i < numInputs; i++) {
            virtualInputs.push_back(i);
        }
    }
    
    // Categorize outputs - prefer hardware-looking ones, but use all if unclear
    for (int i = 0; i < numOutputs; i++) {
        if (isHardwareChannelName(outputChannelNames[i])) {
            hardwareOutputs.push_back(i);
        }
    }
    
    // If no hardware outputs detected, use the first 2 (or all if less)
    // SAR typically puts hardware channels first
    if (hardwareOutputs.empty()) {
        int hwCount = std::min(numOutputs, 2);  // Assume stereo hardware
        for (int i = 0; i < hwCount; i++) {
            hardwareOutputs.push_back(i);
        }
    }
    
    // Create routing: pair virtual inputs with hardware outputs
    // For stereo: virtual L -> hw L, virtual R -> hw R
    // For multiple virtual endpoints: sum them
    
    // Group virtual inputs into stereo pairs and route to hardware outputs
    int numHwChannels = (int)hardwareOutputs.size();
    
    for (size_t i = 0; i < virtualInputs.size(); i++) {
        // Map to corresponding hardware output channel (with wraparound)
        int hwIdx = i % numHwChannels;
        
        ChannelRoute route;
        route.inputChannel = virtualInputs[i];
        route.outputChannel = hardwareOutputs[hwIdx];
        routes.push_back(route);
    }
}

std::string ASIOHost::getRoutingInfo() const {
    std::stringstream ss;
    
    ss << "Input Channels:\n";
    for (int i = 0; i < numInputs; i++) {
        ss << "  [" << i << "] " << inputChannelNames[i];
        if (isVirtualEndpointName(inputChannelNames[i])) {
            ss << " (virtual)";
        } else {
            ss << " (hardware)";
        }
        ss << "\n";
    }
    
    ss << "\nOutput Channels:\n";
    for (int i = 0; i < numOutputs; i++) {
        ss << "  [" << i << "] " << outputChannelNames[i];
        if (isHardwareChannelName(outputChannelNames[i])) {
            ss << " (hardware)";
        }
        ss << "\n";
    }
    
    ss << "\nRouting:\n";
    if (routes.empty()) {
        ss << "  (no routes configured)\n";
    } else {
        for (const auto& route : routes) {
            ss << "  In[" << route.inputChannel << "] \"" << inputChannelNames[route.inputChannel] 
               << "\" -> Out[" << route.outputChannel << "] \"" << outputChannelNames[route.outputChannel] << "\"\n";
        }
    }
    
    return ss.str();
}

bool ASIOHost::createBuffers(int preferredSize) {
    if (!initialized || !asioDriver) {
        return false;
    }
    
    IASIO* drv = (IASIO*)asioDriver;
    
    // Get buffer size range
    long minSize, maxSize, preferred, granularity;
    if (drv->getBufferSize(&minSize, &maxSize, &preferred, &granularity) != ASE_OK) {
        return false;
    }
    
    bufferSize = (preferredSize > 0) ? preferredSize : preferred;
    if (bufferSize < minSize) bufferSize = minSize;
    if (bufferSize > maxSize) bufferSize = maxSize;
    
    // Allocate mix buffer
    mixBuffer.resize(bufferSize);
    
    // Prepare buffer info structs
    int totalChannels = numInputs + numOutputs;
    std::vector<ASIOBufferInfo> bufferInfos(totalChannels);
    
    int idx = 0;
    for (int i = 0; i < numInputs; i++) {
        bufferInfos[idx].isInput = 1;
        bufferInfos[idx].channelNum = i;
        bufferInfos[idx].buffers[0] = nullptr;
        bufferInfos[idx].buffers[1] = nullptr;
        idx++;
    }
    for (int i = 0; i < numOutputs; i++) {
        bufferInfos[idx].isInput = 0;
        bufferInfos[idx].channelNum = i;
        bufferInfos[idx].buffers[0] = nullptr;
        bufferInfos[idx].buffers[1] = nullptr;
        idx++;
    }
    
    // Set up callbacks
    static ASIOCallbacks callbacks;
    callbacks.bufferSwitch = bufferSwitchCallback;
    callbacks.sampleRateDidChange = sampleRateChangedCallback;
    callbacks.asioMessage = asioMessageCallback;
    callbacks.bufferSwitchTimeInfo = (ASIOTime* (*)(ASIOTime*, long, long))bufferSwitchTimeInfoCallback;
    
    // Create buffers
    ASIOError err = drv->createBuffers(bufferInfos.data(), totalChannels, bufferSize, &callbacks);
    if (err != ASE_OK) {
        return false;
    }
    
    // Store buffer pointers
    inputBuffers[0].resize(numInputs);
    inputBuffers[1].resize(numInputs);
    outputBuffers[0].resize(numOutputs);
    outputBuffers[1].resize(numOutputs);
    
    idx = 0;
    for (int i = 0; i < numInputs; i++) {
        inputBuffers[0][i] = bufferInfos[idx].buffers[0];
        inputBuffers[1][i] = bufferInfos[idx].buffers[1];
        idx++;
    }
    for (int i = 0; i < numOutputs; i++) {
        outputBuffers[0][i] = bufferInfos[idx].buffers[0];
        outputBuffers[1][i] = bufferInfos[idx].buffers[1];
        idx++;
    }
    
    // Detect routing now that we have channel info
    detectRouting();
    
    buffersCreated = true;
    return true;
}

void ASIOHost::disposeBuffers() {
    if (buffersCreated && asioDriver) {
        IASIO* drv = (IASIO*)asioDriver;
        drv->disposeBuffers();
        buffersCreated = false;
    }
    
    inputBuffers[0].clear();
    inputBuffers[1].clear();
    outputBuffers[0].clear();
    outputBuffers[1].clear();
    mixBuffer.clear();
    routes.clear();
}

bool ASIOHost::start() {
    if (!buffersCreated || !asioDriver) {
        return false;
    }
    
    IASIO* drv = (IASIO*)asioDriver;
    if (drv->start() != ASE_OK) {
        return false;
    }
    
    running = true;
    return true;
}

bool ASIOHost::stop() {
    if (!running || !asioDriver) {
        return false;
    }
    
    IASIO* drv = (IASIO*)asioDriver;
    drv->stop();
    running = false;
    return true;
}

int ASIOHost::getBytesPerSample(ASIOSampleType type) const {
    switch (type) {
        case ASIOSTInt16MSB:
        case ASIOSTInt16LSB:
            return 2;
        case ASIOSTInt24MSB:
        case ASIOSTInt24LSB:
            return 3;
        case ASIOSTInt32MSB:
        case ASIOSTInt32LSB:
        case ASIOSTInt32MSB16:
        case ASIOSTInt32MSB18:
        case ASIOSTInt32MSB20:
        case ASIOSTInt32MSB24:
        case ASIOSTInt32LSB16:
        case ASIOSTInt32LSB18:
        case ASIOSTInt32LSB20:
        case ASIOSTInt32LSB24:
        case ASIOSTFloat32MSB:
        case ASIOSTFloat32LSB:
            return 4;
        case ASIOSTFloat64MSB:
        case ASIOSTFloat64LSB:
            return 8;
        default:
            return 4;
    }
}

float ASIOHost::sampleToFloat(void* buffer, int sampleIndex, ASIOSampleType type) const {
    switch (type) {
        case ASIOSTInt32LSB: {
            int32_t* buf = (int32_t*)buffer;
            return buf[sampleIndex] / 2147483648.0f;
        }
        case ASIOSTInt16LSB: {
            int16_t* buf = (int16_t*)buffer;
            return buf[sampleIndex] / 32768.0f;
        }
        case ASIOSTInt24LSB: {
            uint8_t* buf = (uint8_t*)buffer + sampleIndex * 3;
            int32_t val = (buf[0]) | (buf[1] << 8) | (buf[2] << 16);
            if (val & 0x800000) val |= 0xFF000000;  // Sign extend
            return val / 8388608.0f;
        }
        case ASIOSTFloat32LSB: {
            float* buf = (float*)buffer;
            return buf[sampleIndex];
        }
        case ASIOSTFloat64LSB: {
            double* buf = (double*)buffer;
            return (float)buf[sampleIndex];
        }
        default:
            // Assume 32-bit int LSB as fallback
            return ((int32_t*)buffer)[sampleIndex] / 2147483648.0f;
    }
}

void ASIOHost::floatToSample(float value, void* buffer, int sampleIndex, ASIOSampleType type) const {
    // Clamp to valid range
    value = std::max(-1.0f, std::min(1.0f, value));
    
    switch (type) {
        case ASIOSTInt32LSB: {
            int32_t* buf = (int32_t*)buffer;
            buf[sampleIndex] = (int32_t)(value * 2147483647.0f);
            break;
        }
        case ASIOSTInt16LSB: {
            int16_t* buf = (int16_t*)buffer;
            buf[sampleIndex] = (int16_t)(value * 32767.0f);
            break;
        }
        case ASIOSTInt24LSB: {
            int32_t val = (int32_t)(value * 8388607.0f);
            uint8_t* buf = (uint8_t*)buffer + sampleIndex * 3;
            buf[0] = val & 0xFF;
            buf[1] = (val >> 8) & 0xFF;
            buf[2] = (val >> 16) & 0xFF;
            break;
        }
        case ASIOSTFloat32LSB: {
            float* buf = (float*)buffer;
            buf[sampleIndex] = value;
            break;
        }
        case ASIOSTFloat64LSB: {
            double* buf = (double*)buffer;
            buf[sampleIndex] = value;
            break;
        }
        default:
            ((int32_t*)buffer)[sampleIndex] = (int32_t)(value * 2147483647.0f);
            break;
    }
}

void ASIOHost::bufferSwitch(long index, bool directProcess) {
    if (!running) {
        return;
    }
    
    // Clear all output buffers first
    for (int ch = 0; ch < numOutputs; ch++) {
        int bytes = getBytesPerSample(outputSampleTypes[ch]) * bufferSize;
        memset(outputBuffers[index][ch], 0, bytes);
    }
    
    // Process routes: copy and sum inputs to outputs
    for (const auto& route : routes) {
        int inCh = route.inputChannel;
        int outCh = route.outputChannel;
        
        if (inCh >= numInputs || outCh >= numOutputs) continue;
        
        ASIOSampleType inType = inputSampleTypes[inCh];
        ASIOSampleType outType = outputSampleTypes[outCh];
        
        void* inBuf = inputBuffers[index][inCh];
        void* outBuf = outputBuffers[index][outCh];
        
        // If sample types match and this is the only route to this output,
        // we could do a direct memcpy. But for mixing we need to sum.
        
        for (int i = 0; i < bufferSize; i++) {
            float inSample = sampleToFloat(inBuf, i, inType);
            float outSample = sampleToFloat(outBuf, i, outType);
            floatToSample(inSample + outSample, outBuf, i, outType);
        }
    }
    
    // Notify driver we're ready
    if (asioDriver) {
        ((IASIO*)asioDriver)->outputReady();
    }
}

// Static callbacks
void ASIOHost::bufferSwitchCallback(long index, long directProcess) {
    if (instance) {
        instance->bufferSwitch(index, directProcess != 0);
    }
}

void ASIOHost::sampleRateChangedCallback(double sRate) {
    if (instance) {
        instance->sampleRate = sRate;
    }
}

long ASIOHost::asioMessageCallback(long selector, long value, void* message, double* opt) {
    switch (selector) {
        case kAsioSelectorSupported:
            if (value == kAsioResetRequest || 
                value == kAsioEngineVersion ||
                value == kAsioResyncRequest ||
                value == kAsioLatenciesChanged ||
                value == kAsioSupportsTimeInfo ||
                value == kAsioSupportsTimeCode) {
                return 1;
            }
            return 0;
        case kAsioEngineVersion:
            return 2;
        case kAsioResetRequest:
        case kAsioResyncRequest:
        case kAsioLatenciesChanged:
            return 1;
        case kAsioBufferSizeChange:
            return 0;
        case kAsioSupportsTimeInfo:
            return 1;
        case kAsioSupportsTimeCode:
            return 0;
    }
    return 0;
}

void* ASIOHost::bufferSwitchTimeInfoCallback(void* timeInfo, long index, long directProcess) {
    if (instance) {
        instance->bufferSwitch(index, directProcess != 0);
    }
    return timeInfo;
}
