#include "asio_host.h"
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <string>
#include <sstream>

// Application constants
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_TOGGLE 1002
#define ID_TRAY_INFO 1003
#define ID_TRAY_ROUTING 1004
#define ID_TRAY_DRIVERS 1100

// Global variables
HWND g_hwnd = nullptr;
NOTIFYICONDATAA g_nid = {0};
HMENU g_menu = nullptr;
ASIOHost g_asioHost;
bool g_running = false;
std::string g_selectedDriver = "Synchronous Audio Router";

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateTrayIcon(HWND hwnd);
void RemoveTrayIcon();
void ShowContextMenu(HWND hwnd);
void UpdateTrayTooltip();
bool StartAudio();
void StopAudio();
void ShowInfo();
void ShowRouting();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Parse command line for driver name
    if (lpCmdLine && strlen(lpCmdLine) > 0) {
        g_selectedDriver = lpCmdLine;
        if (g_selectedDriver.front() == '"') g_selectedDriver.erase(0, 1);
        if (g_selectedDriver.back() == '"') g_selectedDriver.pop_back();
    }

    // Create hidden window for message processing
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "ASIOMiniHostClass";
    
    if (!RegisterClassA(&wc)) {
        MessageBoxA(nullptr, "Failed to register window class", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    g_hwnd = CreateWindowExA(
        0, "ASIOMiniHostClass", "ASIO Mini Host",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        nullptr, nullptr, hInstance, nullptr
    );
    
    if (!g_hwnd) {
        MessageBoxA(nullptr, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    CreateTrayIcon(g_hwnd);
    
    // Try to start audio
    if (!StartAudio()) {
        auto drivers = ASIOHost::getDriverList();
        std::stringstream ss;
        ss << "Could not start with driver: " << g_selectedDriver << "\n\n";
        ss << "Available ASIO drivers:\n";
        for (const auto& drv : drivers) {
            ss << "  - " << drv.name << "\n";
        }
        ss << "\nRight-click the tray icon to select a driver.";
        MessageBoxA(nullptr, ss.str().c_str(), "ASIO Mini Host", MB_OK | MB_ICONINFORMATION);
    }
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    StopAudio();
    RemoveTrayIcon();
    
    return (int)msg.wParam;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                ShowContextMenu(hwnd);
            }
            return 0;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_EXIT:
                    PostQuitMessage(0);
                    return 0;
                    
                case ID_TRAY_TOGGLE:
                    if (g_running) {
                        StopAudio();
                    } else {
                        StartAudio();
                    }
                    UpdateTrayTooltip();
                    return 0;
                    
                case ID_TRAY_INFO:
                    ShowInfo();
                    return 0;
                    
                case ID_TRAY_ROUTING:
                    ShowRouting();
                    return 0;
                    
                default:
                    if (LOWORD(wParam) >= ID_TRAY_DRIVERS) {
                        int driverIndex = LOWORD(wParam) - ID_TRAY_DRIVERS;
                        auto drivers = ASIOHost::getDriverList();
                        if (driverIndex < (int)drivers.size()) {
                            StopAudio();
                            g_selectedDriver = drivers[driverIndex].name;
                            StartAudio();
                            UpdateTrayTooltip();
                        }
                    }
                    break;
            }
            break;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

void CreateTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    strcpy_s(g_nid.szTip, "ASIO Mini Host - Initializing...");
    
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
}

void UpdateTrayTooltip() {
    std::stringstream ss;
    ss << "ASIO Mini Host\n";
    if (g_running) {
        ss << "Running: " << g_asioHost.getDriverName() << "\n";
        ss << g_asioHost.getInputChannels() << " in / " << g_asioHost.getOutputChannels() << " out\n";
        ss << (int)g_asioHost.getSampleRate() << " Hz";
    } else {
        ss << "Stopped";
    }
    
    strncpy_s(g_nid.szTip, ss.str().c_str(), sizeof(g_nid.szTip) - 1);
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU menu = CreatePopupMenu();
    
    std::string statusText = g_running ? 
        ("Running: " + g_asioHost.getDriverName()) : 
        "Stopped";
    AppendMenuA(menu, MF_STRING | MF_DISABLED, 0, statusText.c_str());
    AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
    
    AppendMenuA(menu, MF_STRING, ID_TRAY_TOGGLE, g_running ? "Stop" : "Start");
    
    // Driver submenu
    HMENU driverMenu = CreatePopupMenu();
    auto drivers = ASIOHost::getDriverList();
    for (size_t i = 0; i < drivers.size(); i++) {
        UINT flags = MF_STRING;
        if (g_running && drivers[i].name == g_asioHost.getDriverName()) {
            flags |= MF_CHECKED;
        }
        AppendMenuA(driverMenu, flags, ID_TRAY_DRIVERS + i, drivers[i].name.c_str());
    }
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)driverMenu, "Select Driver");
    
    AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(menu, MF_STRING, ID_TRAY_INFO, "Info...");
    AppendMenuA(menu, MF_STRING, ID_TRAY_ROUTING, "Show Routing...");
    AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(menu, MF_STRING, ID_TRAY_EXIT, "Exit");
    
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

bool StartAudio() {
    if (g_running) {
        return true;
    }
    
    if (!g_asioHost.loadDriver(g_selectedDriver)) {
        return false;
    }
    
    if (!g_asioHost.initialize(g_hwnd)) {
        g_asioHost.unloadDriver();
        return false;
    }
    
    if (!g_asioHost.createBuffers()) {
        g_asioHost.unloadDriver();
        return false;
    }
    
    if (!g_asioHost.start()) {
        g_asioHost.disposeBuffers();
        g_asioHost.unloadDriver();
        return false;
    }
    
    g_running = true;
    UpdateTrayTooltip();
    return true;
}

void StopAudio() {
    if (!g_running) {
        return;
    }
    
    g_asioHost.stop();
    g_asioHost.disposeBuffers();
    g_asioHost.unloadDriver();
    g_running = false;
    UpdateTrayTooltip();
}

void ShowInfo() {
    std::stringstream ss;
    ss << "ASIO Mini Host v1.1\n";
    ss << "==================\n\n";
    ss << "A minimal ASIO host for Synchronous Audio Router.\n";
    ss << "Routes virtual audio endpoints to hardware outputs.\n\n";
    
    if (g_running) {
        ss << "Status: RUNNING\n";
        ss << "Driver: " << g_asioHost.getDriverName() << "\n";
        ss << "Inputs: " << g_asioHost.getInputChannels() << "\n";
        ss << "Outputs: " << g_asioHost.getOutputChannels() << "\n";
        ss << "Sample Rate: " << (int)g_asioHost.getSampleRate() << " Hz\n";
        ss << "Buffer Size: " << g_asioHost.getBufferSize() << " samples\n";
        
        double latencyMs = (g_asioHost.getBufferSize() * 1000.0) / g_asioHost.getSampleRate();
        ss << "Buffer Latency: " << latencyMs << " ms\n";
    } else {
        ss << "Status: STOPPED\n";
    }
    
    ss << "\nAvailable Drivers:\n";
    auto drivers = ASIOHost::getDriverList();
    for (const auto& drv : drivers) {
        ss << "  - " << drv.name;
        if (g_running && drv.name == g_asioHost.getDriverName()) {
            ss << " (active)";
        }
        ss << "\n";
    }
    
    MessageBoxA(g_hwnd, ss.str().c_str(), "ASIO Mini Host", MB_OK | MB_ICONINFORMATION);
}

void ShowRouting() {
    if (!g_running) {
        MessageBoxA(g_hwnd, "Not running. Start audio first to see routing.", 
                    "Routing Info", MB_OK | MB_ICONINFORMATION);
        return;
    }
    
    std::string routingInfo = g_asioHost.getRoutingInfo();
    
    std::stringstream ss;
    ss << "Channel Routing\n";
    ss << "===============\n\n";
    ss << routingInfo;
    ss << "\nVirtual inputs are ASIO playback endpoints.\n";
    ss << "Hardware outputs go to your audio device.\n";
    
    MessageBoxA(g_hwnd, ss.str().c_str(), "Routing Info", MB_OK | MB_ICONINFORMATION);
}
