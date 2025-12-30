#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// Forward declarations for ASIO types
struct ASIODriverInfo;
struct ASIOChannelInfo;
struct ASIOBufferInfo;
struct ASIOCallbacks;

// ASIO sample types
enum ASIOSampleType {
    ASIOSTInt16MSB = 0,
    ASIOSTInt24MSB = 1,
    ASIOSTInt32MSB = 2,
    ASIOSTFloat32MSB = 3,
    ASIOSTFloat64MSB = 4,
    ASIOSTInt32MSB16 = 8,
    ASIOSTInt32MSB18 = 9,
    ASIOSTInt32MSB20 = 10,
    ASIOSTInt32MSB24 = 11,
    ASIOSTInt16LSB = 16,
    ASIOSTInt24LSB = 17,
    ASIOSTInt32LSB = 18,
    ASIOSTFloat32LSB = 19,
    ASIOSTFloat64LSB = 20,
    ASIOSTInt32LSB16 = 24,
    ASIOSTInt32LSB18 = 25,
    ASIOSTInt32LSB20 = 26,
    ASIOSTInt32LSB24 = 27,
};

// ASIO error codes
enum ASIOError {
    ASE_OK = 0,
    ASE_SUCCESS = 0x3f4847a0,
    ASE_NotPresent = -1000,
    ASE_HWMalfunction,
    ASE_InvalidParameter,
    ASE_InvalidMode,
    ASE_SPNotAdvancing,
    ASE_NoClock,
    ASE_NoMemory
};

// Simplified ASIO driver info
struct DriverInfo {
    std::string name;
    CLSID clsid;
};

// Channel routing: which inputs go to which outputs
struct ChannelRoute {
    int inputChannel;   // Source input channel
    int outputChannel;  // Destination output channel
};

class ASIOHost {
public:
    ASIOHost();
    ~ASIOHost();

    // Get list of available ASIO drivers
    static std::vector<DriverInfo> getDriverList();

    // Load a specific driver by name
    bool loadDriver(const std::string& driverName);
    
    // Unload current driver
    void unloadDriver();

    // Initialize the driver
    bool initialize(HWND hwnd);

    // Get channel counts
    int getInputChannels() const { return numInputs; }
    int getOutputChannels() const { return numOutputs; }

    // Get sample rate
    double getSampleRate() const { return sampleRate; }

    // Get buffer size
    int getBufferSize() const { return bufferSize; }

    // Create buffers and prepare for streaming
    bool createBuffers(int preferredSize = 0);

    // Start audio streaming
    bool start();

    // Stop audio streaming
    bool stop();

    // Dispose buffers
    void disposeBuffers();

    // Check if running
    bool isRunning() const { return running; }

    // Get driver name
    std::string getDriverName() const { return driverName; }

    // Get channel names
    const std::vector<std::string>& getInputChannelNames() const { return inputChannelNames; }
    const std::vector<std::string>& getOutputChannelNames() const { return outputChannelNames; }

    // Get routing info as string for display
    std::string getRoutingInfo() const;

    // Callback for buffer switch (called from ASIO driver)
    void bufferSwitch(long index, bool directProcess);

private:
    void* asioDriver = nullptr;
    std::string driverName;
    
    int numInputs = 0;
    int numOutputs = 0;
    double sampleRate = 44100.0;
    int bufferSize = 512;
    
    bool initialized = false;
    bool buffersCreated = false;
    bool running = false;

    // Channel info
    std::vector<std::string> inputChannelNames;
    std::vector<std::string> outputChannelNames;
    std::vector<ASIOSampleType> inputSampleTypes;
    std::vector<ASIOSampleType> outputSampleTypes;

    // Intelligent routing
    std::vector<ChannelRoute> routes;
    
    // Mixing buffer for combining multiple inputs
    std::vector<float> mixBuffer;

    // Buffer pointers
    std::vector<void*> inputBuffers[2];
    std::vector<void*> outputBuffers[2];

    // Detect and setup channel routing
    void detectRouting();
    
    // Helper: check if channel name looks like a SAR virtual endpoint
    bool isVirtualEndpointName(const std::string& name) const;
    
    // Helper: check if channel name looks like hardware I/O
    bool isHardwareChannelName(const std::string& name) const;

    // Helper: get bytes per sample for a sample type
    int getBytesPerSample(ASIOSampleType type) const;

    // Helper: convert sample to float
    float sampleToFloat(void* buffer, int sampleIndex, ASIOSampleType type) const;

    // Helper: convert float to sample
    void floatToSample(float value, void* buffer, int sampleIndex, ASIOSampleType type) const;

    // Static instance for callbacks
    static ASIOHost* instance;
    
    // ASIO callbacks
    static void bufferSwitchCallback(long index, long directProcess);
    static void sampleRateChangedCallback(double sRate);
    static long asioMessageCallback(long selector, long value, void* message, double* opt);
    static void* bufferSwitchTimeInfoCallback(void* timeInfo, long index, long directProcess);
};
