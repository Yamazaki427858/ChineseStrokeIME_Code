// config_loader.h - 設定檔載入
#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include "ime_core.h"

namespace ConfigLoader {
    // 載入介面設定
    void loadInterfaceConfig(GlobalState& state);
    
    // 保存介面設定
    void saveInterfaceConfig(const GlobalState& state);
    
    // 載入所有設定
    void loadAllConfigs(GlobalState& state);
    
    // 重新載入設定
    void refreshConfigs(GlobalState& state);
    
    // 從配置文件讀取transparency_alpha值（不修改其他配置）
    void updateTransparencyAlphaFromConfig(GlobalState& state);

    // 筆劃鍵恢復預設（記憶體 + user/interfaceconfig.ini 的 [InputSettings] 相關項）
    void resetStrokeKeysToDefaults(GlobalState& state);
}

#endif // CONFIG_LOADER_H
