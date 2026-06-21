// screen_manager.h - 螢幕偵測管理
#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#include "ime_core.h"
#include <vector>

namespace ScreenManager {
    // 螢幕資訊結構
    struct MonitorInfo {
        HMONITOR hMonitor;
        RECT rect;
        RECT workArea;
        bool isPrimary;
    };
    
    // 螢幕偵測函數
    void updateMonitorInfo();
    std::vector<MonitorInfo> getMonitors();
    MonitorInfo getMonitorFromPoint(POINT pt);
    MonitorInfo getPrimaryMonitor();
    int getMonitorCount();
    
    // 螢幕變更追蹤（支援 3 螢幕以上熱插拔）
    void initMonitorTracking();
    void syncMonitorTracking();
    int getPreviousMonitorCount();
    
    // 螢幕模式偵測
    bool isExtendedMode();
    bool isMirroredMode();
    bool isCoordinateValidInCurrentMode(int x, int y);
    bool isPointInAnyMonitor(POINT pt);
    bool isRectVisibleOnAnyMonitor(const RECT& rect, int minVisiblePercent = 50);
    bool isWindowVisibleOnAnyMonitor(HWND hwnd, int minVisiblePercent = 50);
    
    // 螢幕變更處理
    void handleDisplayChange();
    RECT getSafePrimaryScreen();
    
    // 安全視窗定位
    POINT ensureSafePosition(POINT pt, int windowWidth, int windowHeight);
    
    // 取得視窗所在顯示器的有效工作區（已排除工作列）
    RECT getWorkArea(HWND hwnd);
}

#endif // SCREEN_MANAGER_H