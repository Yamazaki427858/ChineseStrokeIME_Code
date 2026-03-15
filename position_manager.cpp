// position_manager.cpp - 位置記憶管理實作
// 以螢幕範圍（rect）為準，不依賴工作區（workArea）避免工作列；
// Windows 視窗層級由系統控制，工作列／開始選單為 Shell 級別，一般程式無法蓋過。
#include "position_manager.h"
#include "screen_manager.h"
#include <fstream>

namespace PositionManager {

Position g_toolbarPos;
ScreenModePositions g_screenModePositions;
bool g_useUserPosition = false;
Position g_userInputPos;
Position g_userCandPos;
int g_verticalOffset = 25;

static int g_retryCount = 0;

POINT getCurrentMousePosition() {
    POINT pos;
    GetCursorPos(&pos);
    bool inAnyScreen = false;
    auto monitors = ScreenManager::getMonitors();
    for (const auto& monitor : monitors) {
        if (pos.x >= monitor.rect.left && pos.x <= monitor.rect.right &&
            pos.y >= monitor.rect.top && pos.y <= monitor.rect.bottom) {
            inAnyScreen = true;
            break;
        }
    }
    if (!inAnyScreen) {
        ScreenManager::MonitorInfo primary = ScreenManager::getPrimaryMonitor();
        pos.x = primary.rect.left + (primary.rect.right - primary.rect.left) / 2;
        pos.y = primary.rect.top + (primary.rect.bottom - primary.rect.top) / 2;
    }
    pos.y += g_verticalOffset;
    return pos;
}

POINT getOptimalWindowPosition(const GlobalState& state) {
    if (g_useUserPosition && g_userInputPos.isValid) {
        POINT result = {g_userInputPos.x, g_userInputPos.y};
        return result;
    }
    POINT basePos = getCurrentMousePosition();
    ScreenManager::MonitorInfo currentMonitor = ScreenManager::getMonitorFromPoint(basePos);
    const RECT& rc = currentMonitor.rect;
    int totalHeight = state.windowHeight;
    if (state.showCand) totalHeight += 5 + state.candidateHeight;
    if (basePos.x + state.windowWidth > rc.right - 10)
        basePos.x = rc.right - state.windowWidth - 10;
    if (basePos.x < rc.left + 10)
        basePos.x = rc.left + 10;
    if (basePos.y + totalHeight > rc.bottom - 10) {
        basePos.y = basePos.y - totalHeight - g_verticalOffset;
        if (basePos.y < rc.top + 10) basePos.y = rc.top + 10;
    }
    return basePos;
}

bool isPositionVisible(const GlobalState& state) {
    if (!state.hWnd) return false;
    RECT windowRect;
    if (!GetWindowRect(state.hWnd, &windowRect)) return false;
    auto monitors = ScreenManager::getMonitors();
    for (const auto& monitor : monitors) {
        RECT intersection;
        if (IntersectRect(&intersection, &windowRect, &monitor.rect)) {
            int windowArea = (windowRect.right - windowRect.left) * (windowRect.bottom - windowRect.top);
            int intersectionArea = (intersection.right - intersection.left) * (intersection.bottom - intersection.top);
            if (intersectionArea >= windowArea / 2) return true;
        }
    }
    return false;
}

void forceResetToSafePosition(GlobalState& state) {
    ScreenManager::MonitorInfo primary = ScreenManager::getPrimaryMonitor();
    const RECT& rc = primary.rect;
    g_toolbarPos.x = rc.left + 50;
    g_toolbarPos.y = rc.bottom - state.windowHeight - 50;
    g_toolbarPos.isValid = true;
    if (state.hWnd) {
        SetWindowPos(state.hWnd, HWND_TOPMOST, g_toolbarPos.x, g_toolbarPos.y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        ShowWindow(state.hWnd, SW_SHOW);
        SetForegroundWindow(state.hWnd);
    }
    savePositions(state);
    Utils::updateStatus(state, L"工具列已重置到安全位置");
}

void loadPositions(GlobalState& state) {
    std::string path = Utils::wstrToUtf8(state.systemDir + L"positions.ini");
    std::ifstream config(path);
    if (!config.is_open()) {
        ScreenManager::MonitorInfo primary = ScreenManager::getPrimaryMonitor();
        const RECT& rc = primary.rect;
        g_toolbarPos.x = std::max<int>(rc.left + 50, 50);
        g_toolbarPos.y = std::max<int>(rc.bottom - state.windowHeight - 50, 50);
        g_toolbarPos.isValid = true;
        savePositions(state);
        return;
    }
    std::string line, section;
    while (std::getline(config, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.length() - 2);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        try {
            if (section == "Toolbar") {
                if (key == "x") g_toolbarPos.x = std::stoi(value);
                else if (key == "y") g_toolbarPos.y = std::stoi(value);
                g_toolbarPos.isValid = true;
            } else if (section == "UserPosition") {
                if (key == "enabled") g_useUserPosition = (value == "1");
                else if (key == "input_x") g_userInputPos.x = std::stoi(value);
                else if (key == "input_y") g_userInputPos.y = std::stoi(value);
                else if (key == "cand_x") g_userCandPos.x = std::stoi(value);
                else if (key == "cand_y") g_userCandPos.y = std::stoi(value);
                if (g_useUserPosition) { g_userInputPos.isValid = true; g_userCandPos.isValid = true; }
            } else if (section == "OptimizedPositioning") {
                if (key == "vertical_offset") g_verticalOffset = std::stoi(value);
            } else if (section == "ToolbarExtended") {
                if (key == "x") g_screenModePositions.extendedModePos.x = std::stoi(value);
                else if (key == "y") g_screenModePositions.extendedModePos.y = std::stoi(value);
                g_screenModePositions.extendedModePos.isValid = true;
                g_screenModePositions.hasExtendedPos = true;
            } else if (section == "ToolbarMirrored") {
                if (key == "x") g_screenModePositions.mirroredModePos.x = std::stoi(value);
                else if (key == "y") g_screenModePositions.mirroredModePos.y = std::stoi(value);
                g_screenModePositions.mirroredModePos.isValid = true;
                g_screenModePositions.hasMirroredPos = true;
            }
        } catch (...) {}
    }
    config.close();
    if (ScreenManager::isExtendedMode() && g_screenModePositions.hasExtendedPos)
        g_toolbarPos = g_screenModePositions.extendedModePos;
    else if (ScreenManager::isMirroredMode() && g_screenModePositions.hasMirroredPos)
        g_toolbarPos = g_screenModePositions.mirroredModePos;
    ensureVisiblePosition(state);
}

void savePositions(const GlobalState& state) {
    std::ofstream config(Utils::wstrToUtf8(state.systemDir + L"positions.ini"));
    if (!config.is_open()) return;
    config << "[Toolbar]\nx=" << g_toolbarPos.x << "\ny=" << g_toolbarPos.y << "\n";
    if (ScreenManager::isExtendedMode()) {
        config << "[ToolbarExtended]\nx=" << g_toolbarPos.x << "\ny=" << g_toolbarPos.y << "\n";
        g_screenModePositions.extendedModePos = g_toolbarPos;
        g_screenModePositions.hasExtendedPos = true;
    } else {
        config << "[ToolbarMirrored]\nx=" << g_toolbarPos.x << "\ny=" << g_toolbarPos.y << "\n";
        g_screenModePositions.mirroredModePos = g_toolbarPos;
        g_screenModePositions.hasMirroredPos = true;
    }
    config << "[UserPosition]\nenabled=" << (g_useUserPosition ? "1" : "0") << "\n";
    if (g_useUserPosition) {
        config << "input_x=" << g_userInputPos.x << "\ninput_y=" << g_userInputPos.y
              << "\ncand_x=" << g_userCandPos.x << "\ncand_y=" << g_userCandPos.y << "\n";
    }
    config << "[OptimizedPositioning]\nvertical_offset=" << g_verticalOffset << "\n";
    config.close();
}

void adjustPositionForScreenMode(GlobalState& state) {
    static bool previousExtended = ScreenManager::isExtendedMode();
    static bool firstTime = true;
    if (firstTime) {
        previousExtended = ScreenManager::isExtendedMode();
        firstTime = false;
    }
    ScreenManager::updateMonitorInfo();
    bool currentExtended = ScreenManager::isExtendedMode();
    bool modeChanged = (previousExtended != currentExtended);
    if (modeChanged || g_retryCount > 0) {
        ScreenManager::MonitorInfo primary = ScreenManager::getPrimaryMonitor();
        const RECT& rc = primary.rect;
        if (previousExtended && !currentExtended) {
            g_toolbarPos.x = rc.left + 50;
            g_toolbarPos.y = rc.bottom - state.windowHeight - 50;
            if (state.hWnd) {
                SetWindowPos(state.hWnd, HWND_TOPMOST, g_toolbarPos.x, g_toolbarPos.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                ShowWindow(state.hWnd, SW_SHOW);
                InvalidateRect(state.hWnd, nullptr, TRUE);
                UpdateWindow(state.hWnd);
            }
            g_retryCount = 0;
            if (modeChanged && state.showScreenModeNotification) {
                MessageBoxW(NULL, L"偵測到螢幕模式變更：延伸→同步\n工具列已自動移至主螢幕。", L"螢幕模式變更", MB_OK | MB_ICONINFORMATION);
            }
        } else if (!previousExtended && currentExtended) {
            if (g_screenModePositions.hasExtendedPos) {
                g_toolbarPos = g_screenModePositions.extendedModePos;
                ensureVisiblePosition(state);
            }
            g_retryCount = 0;
        }
        savePositions(state);
        previousExtended = currentExtended;
    }
    if (!isPositionVisible(state)) {
        g_retryCount++;
        if (g_retryCount <= 3)
            SetTimer(state.hWnd, 998, 200, NULL);
        else {
            forceResetToSafePosition(state);
            g_retryCount = 0;
        }
    } else {
        g_retryCount = 0;
    }
}

void ensureVisiblePosition(GlobalState& state) {
    bool positionValid = false;
    auto monitors = ScreenManager::getMonitors();
    for (const auto& monitor : monitors) {
        const RECT& rc = monitor.rect;
        if (g_toolbarPos.x >= rc.left && g_toolbarPos.x <= rc.right - state.windowWidth &&
            g_toolbarPos.y >= rc.top && g_toolbarPos.y <= rc.bottom - state.windowHeight) {
            positionValid = true;
            break;
        }
    }
    if (!positionValid) {
        ScreenManager::MonitorInfo primary = ScreenManager::getPrimaryMonitor();
        const RECT& rc = primary.rect;
        g_toolbarPos.x = rc.left + 50;
        g_toolbarPos.y = rc.bottom - state.windowHeight - 50;
        g_toolbarPos.isValid = true;
        if (state.hWnd)
            SetWindowPos(state.hWnd, NULL, g_toolbarPos.x, g_toolbarPos.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        savePositions(state);
    }
}

} // namespace PositionManager
