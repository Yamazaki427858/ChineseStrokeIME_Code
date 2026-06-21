// screen_manager.cpp - 螢幕偵測管理實作
#include "screen_manager.h"
#include <algorithm>

namespace ScreenManager {

std::vector<MonitorInfo> g_monitors;

static bool g_previousExtendedMode = false;
static int g_previousMonitorCount = -1;
static bool g_firstTimeCheck = true;

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    MONITORINFOEX mi;
    mi.cbSize = sizeof(MONITORINFOEX);
    
    if (GetMonitorInfo(hMonitor, &mi)) {
        MonitorInfo info;
        info.hMonitor = hMonitor;
        info.rect = mi.rcMonitor;
        info.workArea = mi.rcWork;
        info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        
        g_monitors.push_back(info);
    }
    return TRUE;
}

void updateMonitorInfo() {
    g_monitors.clear();
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
    
    if (g_monitors.empty()) {
        MonitorInfo defaultMonitor;
        defaultMonitor.rect.left = 0;
        defaultMonitor.rect.top = 0;
        defaultMonitor.rect.right = GetSystemMetrics(SM_CXSCREEN);
        defaultMonitor.rect.bottom = GetSystemMetrics(SM_CYSCREEN);
        defaultMonitor.workArea = defaultMonitor.rect;
        defaultMonitor.isPrimary = true;
        defaultMonitor.hMonitor = NULL;
        g_monitors.push_back(defaultMonitor);
    }
}

std::vector<MonitorInfo> getMonitors() {
    if (g_monitors.empty()) {
        updateMonitorInfo();
    }
    return g_monitors;
}

MonitorInfo getMonitorFromPoint(POINT pt) {
    if (g_monitors.empty()) {
        updateMonitorInfo();
    }
    
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    
    for (const auto& monitor : g_monitors) {
        if (monitor.hMonitor == hMon) {
            return monitor;
        }
    }
    
    return getPrimaryMonitor();
}

MonitorInfo getPrimaryMonitor() {
    if (g_monitors.empty()) {
        updateMonitorInfo();
    }
    
    for (const auto& monitor : g_monitors) {
        if (monitor.isPrimary) {
            return monitor;
        }
    }
    
    return g_monitors[0];
}

int getMonitorCount() {
    if (g_monitors.empty()) {
        updateMonitorInfo();
    }
    return static_cast<int>(g_monitors.size());
}

void initMonitorTracking() {
    updateMonitorInfo();
    g_previousMonitorCount = static_cast<int>(g_monitors.size());
    g_previousExtendedMode = g_previousMonitorCount > 1;
    g_firstTimeCheck = false;
}

void syncMonitorTracking() {
    updateMonitorInfo();
    g_previousMonitorCount = static_cast<int>(g_monitors.size());
    g_previousExtendedMode = g_previousMonitorCount > 1;
}

int getPreviousMonitorCount() {
    return g_previousMonitorCount;
}

bool isPointInAnyMonitor(POINT pt) {
    if (g_monitors.empty()) {
        updateMonitorInfo();
    }
    
    for (const auto& monitor : g_monitors) {
        if (pt.x >= monitor.rect.left && pt.x <= monitor.rect.right &&
            pt.y >= monitor.rect.top && pt.y <= monitor.rect.bottom) {
            return true;
        }
    }
    return false;
}

bool isRectVisibleOnAnyMonitor(const RECT& rect, int minVisiblePercent) {
    if (g_monitors.empty()) {
        updateMonitorInfo();
    }

    int windowArea = (rect.right - rect.left) * (rect.bottom - rect.top);
    if (windowArea <= 0) {
        return false;
    }

    for (const auto& monitor : g_monitors) {
        RECT intersection;
        if (IntersectRect(&intersection, &rect, &monitor.rect)) {
            int intersectionArea = (intersection.right - intersection.left) *
                                   (intersection.bottom - intersection.top);
            if (intersectionArea * 100 >= windowArea * minVisiblePercent) {
                return true;
            }
        }
    }
    return false;
}

bool isWindowVisibleOnAnyMonitor(HWND hwnd, int minVisiblePercent) {
    if (!hwnd) return false;
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) return false;
    return isRectVisibleOnAnyMonitor(rect, minVisiblePercent);
}

bool isExtendedMode() {
    updateMonitorInfo();
    return g_monitors.size() > 1;
}

bool isTrulyMirroredMode() {
    updateMonitorInfo();
    
    if (g_monitors.size() <= 1) return true;
    
    if (g_monitors.size() >= 2) {
        RECT firstRect = g_monitors[0].rect;
        int firstWidth = firstRect.right - firstRect.left;
        int firstHeight = firstRect.bottom - firstRect.top;
        
        for (size_t i = 1; i < g_monitors.size(); i++) {
            RECT currentRect = g_monitors[i].rect;
            int currentWidth = currentRect.right - currentRect.left;
            int currentHeight = currentRect.bottom - currentRect.top;
            
            if (currentWidth != firstWidth || currentHeight != firstHeight) {
                return false;
            }
        }
        return true;
    }
    
    return false;
}

bool isMirroredMode() {
    return isTrulyMirroredMode();
}

bool isCoordinateValidInCurrentMode(int x, int y) {
    updateMonitorInfo();
    
    for (const auto& monitor : g_monitors) {
        if (x >= monitor.workArea.left && x <= monitor.workArea.right &&
            y >= monitor.workArea.top && y <= monitor.workArea.bottom) {
            return true;
        }
    }
    return false;
}

void handleDisplayChange() {
    updateMonitorInfo();
    
    int currentCount = static_cast<int>(g_monitors.size());
    bool currentExtended = currentCount > 1;
    
    if (!g_firstTimeCheck) {
        bool modeChanged = (g_previousExtendedMode != currentExtended);
        bool countChanged = (g_previousMonitorCount >= 0 && g_previousMonitorCount != currentCount);
        
        if ((modeChanged || countChanged) && g_state.hWnd) {
            PostMessage(g_state.hWnd, WM_USER + 301, currentExtended ? 1 : 0, 0);
        }
    } else {
        g_firstTimeCheck = false;
    }
    // 追蹤基準由 adjustPositionForScreenMode() 處理完後透過 syncMonitorTracking() 更新
}

RECT getSafePrimaryScreen() {
    RECT safeRect = {0, 0, 1920, 1080};
    
    updateMonitorInfo();
    
    if (g_monitors.size() <= 1) {
        if (SystemParametersInfo(SPI_GETWORKAREA, 0, &safeRect, 0)) {
            return safeRect;
        }
    } else {
        for (const auto& monitor : g_monitors) {
            if (monitor.isPrimary) {
                return monitor.workArea;
            }
        }
    }
    
    safeRect.right = GetSystemMetrics(SM_CXSCREEN);
    safeRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    
    return safeRect;
}

POINT ensureSafePosition(POINT pt, int windowWidth, int windowHeight) {
    if (g_monitors.empty()) {
        updateMonitorInfo();
    }
    
    bool pointInScreen = false;
    for (const auto& monitor : g_monitors) {
        if (pt.x >= monitor.workArea.left && 
            pt.x + windowWidth <= monitor.workArea.right &&
            pt.y >= monitor.workArea.top &&
            pt.y + windowHeight <= monitor.workArea.bottom) {
            pointInScreen = true;
            break;
        }
    }
    
    if (!pointInScreen) {
        MonitorInfo primary = getPrimaryMonitor();
        pt.x = primary.workArea.left + (primary.workArea.right - primary.workArea.left - windowWidth) / 2;
        pt.y = primary.workArea.top + (primary.workArea.bottom - primary.workArea.top - windowHeight) / 2;
    }
    
    return pt;
}

RECT getWorkArea(HWND hwnd) {
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (GetMonitorInfo(hMonitor, &mi)) {
        return mi.rcWork;
    }
    RECT fallback = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (SystemParametersInfo(SPI_GETWORKAREA, 0, &fallback, 0)) {
        return fallback;
    }
    return fallback;
}

} // namespace ScreenManager
