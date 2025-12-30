# ASIO Mini Host

A minimal, lightweight ASIO host designed originally for use with [Synchronous Audio Router (SAR)](https://github.com/eiz/SynchronousAudioRouter). This replaces the need to run a full DAW like REAPER just to keep SAR's virtual audio endpoints active.

## What It Does

SAR Mini Host does exactly one thing: it loads an ASIO driver (SAR by default), keeps it running, and routes audio from inputs to outputs. It sits in your system tray using minimal resources.

This enables SAR to work for system-wide audio routing without a DAW.

A FAIR WARNING: I made this for my own use. It's lazily built and is highly unlikely to work without modification for every single use case. PRs welcome!

Also, SAR is abandonware and hasn't been maintained in years. But this tool still works functionally as a bridge between two ASIO endpoints when the only alternative is to run it through a DAW. So maybe it'll help someone.

## Features

- **Minimal Resource Usage**: No GUI, no plugins, no mixer - just pure audio passthrough
- **System Tray**: Runs silently in the background
- **Driver Selection**: Switch between ASIO drivers from the tray menu
- **Auto-Start Ready**: Can be added to Windows startup

## Requirements

- Windows 10/11 (64-bit)
- (optional) [Synchronous Audio Router](https://github.com/eiz/SynchronousAudioRouter) installed
- Visual Studio 2019 or later or Cmake installed

## Building

### Using Visual Studio

1. Install Visual Studio 2019/2022 with C++ desktop development workload
2. Install CMake (or use the one bundled with Visual Studio)
3. Open a Developer Command Prompt and run:

```batch
cd asio-mini-host
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

The executable will be in `build/bin/SARMiniHost.exe`

### Using MinGW

```batch
cd sar-mini-host
mkdir build
cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Usage

### Basic Usage

1. Make sure SAR is installed and configured
2. Run `SARMiniHost.exe`
3. The app will appear in your system tray
4. It will automatically try to connect to "Synchronous Audio Router"

### Selecting a Different Driver

Right-click the tray icon → "Select Driver" → Choose your driver

### Command Line

You can specify a driver name on the command line:

```batch
SARMiniHost.exe "Synchronous Audio Router"
SARMiniHost.exe "ASIO4ALL v2"
SARMiniHost.exe "Your Audio Interface ASIO"
```

### Auto-Start with Windows

1. Press `Win+R`, type `shell:startup`, press Enter
2. Create a shortcut to `SARMiniHost.exe` in this folder
3. Optionally, right-click the shortcut → Properties → add your driver name to the Target field

## Tray Menu Options

- **Start/Stop**: Toggle audio streaming
- **Select Driver**: Choose from available ASIO drivers
- **Info**: Show current status and configuration
- **Exit**: Close the application

## How It Works

1. The app loads the SAR ASIO driver via COM (no ASIO SDK needed)
2. It creates audio buffers for all input/output channels
3. In the audio callback, it copies input buffers directly to output buffers
4. This keeps SAR's virtual audio endpoints active and functional

The passthrough mode means:
- SAR's virtual Windows audio devices receive audio from applications
- That audio appears on the ASIO input channels
- The mini host copies it to the ASIO output channels  
- SAR routes it to your physical audio device

## Troubleshooting

### "Driver not found: Synchronous Audio Router"

Make sure SAR is properly installed. Check if it appears in:
- Registry: `HKEY_LOCAL_MACHINE\SOFTWARE\ASIO\Synchronous Audio Router`
- Or run the Info dialog to see available drivers

### "Failed to create ASIO driver instance"

This usually means:
- The driver is already in use by another application
- SAR requires admin privileges (try running as admin)
- The driver isn't properly registered

### No Sound

1. Check that SAR is configured with virtual endpoints
2. Make sure your DAW (or this mini host) is set as the ASIO device in SAR
3. Verify Windows is using the SAR virtual device as default output

### High CPU Usage

This shouldn't happen with normal usage. If it does:
- Check for other audio applications
- Try a larger buffer size (modify source code)
- Run LatencyMon to check for system issues

## Technical Details

- **Buffer Size**: Uses the driver's preferred buffer size
- **Sample Rate**: Uses the driver's current sample rate  
- **Sample Format**: Assumes 32-bit samples (standard for most ASIO drivers)
- **Latency**: Adds zero latency beyond SAR's own buffering

## Building Without CMake

If you prefer not to use CMake, you can compile directly with MSVC:

```batch
cl /EHsc /O2 /MT /DUNICODE /D_UNICODE ^
   src/main.cpp src/asio_host.cpp ^
   /link /SUBSYSTEM:WINDOWS ^
   ole32.lib oleaut32.lib uuid.lib shell32.lib advapi32.lib ^
   /OUT:SARMiniHost.exe
```

## License

MIT License - feel free to modify and distribute.

## Credits

- [Synchronous Audio Router](https://github.com/eiz/SynchronousAudioRouter) by Mackenzie Straight
- ASIO technology by Steinberg Media Technologies GmbH

## Contributing

This is a minimal tool by design. If you need more features, consider using:
- REAPER (lightweight DAW)
- LightHost (minimal VST host)
- Voicemeeter (audio mixer with ASIO support)

But if you find bugs or have improvements that keep the tool minimal, PRs are welcome!
