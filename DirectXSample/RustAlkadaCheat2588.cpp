#include <windows.h>
#include <string>
#include <fstream>
#include <vector>
#include <cmath>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

// Global variables for Rust 2588 (Jungle Update, Alkad)
bool g_enableESP = false;
bool g_enableAimbot = false;
bool g_enableWallhack = false;
int g_espColorR = 255, g_espColorG = 0, g_espColorB = 0;
float g_aimbotFOV = 30.0f, g_aimbotSmooth = 5.0f;
float g_espDistanceMax = 200.0f;
int g_aimbotPriority = 0; // 0: Distance, 1: Health
float g_viewPitch = 0.0f, g_viewYaw = 0.0f;

// Offsets (default for Rust 2588)
DWORD g_entityListOffset = 0x4E2A1B0;
DWORD g_localPlayerOffset = 0x4E5D890;
DWORD g_viewMatrixOffset = 0x4E7F210;
DWORD g_healthOffset = 0x14;
float g_viewMatrix[16] = { 0 };

// Console handle
HANDLE g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

// Logging
void LogToFile(const std::string& message) {
    std::ofstream logFile("cheat_log.txt", std::ios::app);
    if (logFile.is_open()) {
        SYSTEMTIME time;
        GetLocalTime(&time);
        char timeStr[32];
        snprintf(timeStr, sizeof(timeStr), "[%02d:%02d:%02d.%03d] ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
        logFile << timeStr << message << std::endl;
        logFile.close();
    }
}

// Update console
void UpdateConsoleDisplay() {
    SetConsoleCursorPosition(g_hConsole, { 0, 0 });
    char display[256];
    snprintf(display, sizeof(display),
             "\033[1;32mRustAlkadCheat2588\033[0m\n"
             "Wallhack: %s\nAimbot: %s\nESP: %s\n"
             "F1: Wallhack | F2: Aimbot | F3: ESP\n"
             "F5: Save Config | F6/F7: ESP Color\n"
             "F8: Update Offsets | F9: FOV+ | F10: Smooth-\n"
             "NumPad: Adjust Settings\n",
             g_enableWallhack ? "\033[1;32mOn\033[0m" : "\033[1;31mOff\033[0m",
             g_enableAimbot ? "\033[1;32mOn\033[0m" : "\033[1;31mOff\033[0m",
             g_enableESP ? "\033[1;32mOn\033[0m" : "\033[1;31mOff\033[0m");
    WriteConsoleA(g_hConsole, display, strlen(display), NULL, NULL);
}

// Print message
void PrintConsoleMessage(const std::string& message, const std::string& status = "") {
    std::string fullMessage = message + (status.empty() ? "" : ": " + status) + "\n";
    WriteConsoleA(g_hConsole, fullMessage.c_str(), fullMessage.length(), NULL, NULL);
    LogToFile(fullMessage);
    UpdateConsoleDisplay();
}

// Check memory readability
bool IsMemoryReadable(uintptr_t address, size_t size) {
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) &&
           mbi.State == MEM_COMMIT && mbi.Protect != PAGE_NOACCESS;
}

// Pattern scanning
uintptr_t FindPattern(const char* moduleName, const char* pattern, const char* mask) {
    HMODULE hModule = GetModuleHandleA(moduleName);
    if (!hModule) return 0;

    MODULEINFO modInfo;
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(MODULEINFO))) return 0;

    uintptr_t base = reinterpret_cast<uintptr_t>(hModule);
    size_t size = modInfo.SizeOfImage;
    size_t patternLength = strlen(mask);

    for (size_t i = 0; i < size - patternLength; i++) {
        if (!IsMemoryReadable(base + i, patternLength)) continue;
        bool found = true;
        for (size_t j = 0; j < patternLength; j++) {
            found &= mask[j] == '?' || pattern[j] == *reinterpret_cast<char*>(base + i + j);
        }
        if (found) return base + i;
    }
    return 0;
}

// Validate offset
bool ValidateOffset(DWORD offset, const std::string& name) {
    if (offset < 0x100000 || offset > 0xFFFFFFF || !IsMemoryReadable(offset, sizeof(DWORD))) {
        LogToFile("Invalid offset for " + name + ": 0x" + std::to_string(offset));
        return false;
    }
    LogToFile(name + " offset validated: 0x" + std::to_string(offset));
    return true;
}

// Scan game files
bool ScanGameFilesForOffsets(const std::string& gamePath) {
    std::vector<std::string> files;
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((gamePath + "\\*.*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        LogToFile("Failed to scan game directory: " + gamePath);
        return false;
    }

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            files.push_back(gamePath + "\\" + findData.cFileName);
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);

    std::vector<std::pair<const char*, const char*>> patterns = {
        {"\x48\x8B\x05\x00\x00\x00\x00\x48\x85\xC0\x0F\x84", "xxx????xxxxx"}, // entityList
        {"\x48\x8B\x15\x00\x00\x00\x00\x48\x85\xD2\x74", "xxx????xxxx"},     // localPlayer
        {"\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xF3\x0F\x10\x00", "xxx????x????xxxx"}, // viewMatrix
        {"\xF3\x0F\x10\x00\x00\x00\x00\xF3\x0F\x11\x00", "xxx????xxx?"},      // health
    };

    bool updated = false;
    for (const auto& file : files) {
        std::ifstream inFile(file, std::ios::binary);
        if (!inFile.is_open()) continue;

        std::vector<char> data((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        inFile.close();
        if (data.empty()) continue;

        for (size_t i = 0; i < patterns.size(); i++) {
            for (size_t j = 0; j < data.size() - strlen(patterns[i].second); j++) {
                bool found = true;
                for (size_t k = 0; k < strlen(patterns[i].second); k++) {
                    found &= patterns[i].second[k] == '?' || patterns[i].first[k] == data[j + k];
                }
                if (found) {
                    DWORD offset = *reinterpret_cast<DWORD*>(&data[j + 3]);
                    DWORD newOffset = static_cast<DWORD>(j + 7 + offset);
                    if (ValidateOffset(newOffset, i == 0 ? "entityList" : (i == 1 ? "localPlayer" : (i == 2 ? "viewMatrix" : "health")))) {
                        if (i == 0) g_entityListOffset = newOffset;
                        else if (i == 1) g_localPlayerOffset = newOffset;
                        else if (i == 2) g_viewMatrixOffset = newOffset;
                        else if (i == 3) g_healthOffset = newOffset;
                        updated = true;
                    }
                }
            }
        }
    }

    if (updated) {
        std::ofstream cacheFile("offset_cache.txt");
        if (cacheFile.is_open()) {
            cacheFile << "entity_list=0x" << std::hex << g_entityListOffset << "\n";
            cacheFile << "local_player=0x" << std::hex << g_localPlayerOffset << "\n";
            cacheFile << "view_matrix=0x" << std::hex << g_viewMatrixOffset << "\n";
            cacheFile << "health=0x" << std::hex << g_healthOffset << "\n";
            cacheFile.close();
            LogToFile("Offsets updated from game files: entityList=0x" + std::to_string(g_entityListOffset) +
                      ", localPlayer=0x" + std::to_string(g_localPlayerOffset) +
                      ", viewMatrix=0x" + std::to_string(g_viewMatrixOffset) +
                      ", health=0x" + std::to_string(g_healthOffset));
        }
    }
    return updated;
}

// Find offsets
bool FindOffsets(const std::string& gamePath = "") {
    LogToFile("Starting offset search...");

    // Load from cache
    std::ifstream cacheFile("offset_cache.txt");
    if (cacheFile.is_open()) {
        std::string line;
        while (std::getline(cacheFile, line)) {
            std::istringstream iss(line);
            std::string key, value;
            if (std::getline(iss, key, '=') && std::getline(iss, value)) {
                try {
                    if (key == "entity_list") g_entityListOffset = std::stoul(value, nullptr, 16);
                    else if (key == "local_player") g_localPlayerOffset = std::stoul(value, nullptr, 16);
                    else if (key == "view_matrix") g_viewMatrixOffset = std::stoul(value, nullptr, 16);
                    else if (key == "health") g_healthOffset = std::stoul(value, nullptr, 16);
                } catch (...) {
                    LogToFile("Error parsing offset cache: " + line);
                }
            }
        }
        cacheFile.close();
        if (ValidateOffset(g_entityListOffset, "entityList") &&
            ValidateOffset(g_localPlayerOffset, "localPlayer") &&
            ValidateOffset(g_viewMatrixOffset, "viewMatrix") &&
            ValidateOffset(g_healthOffset, "health")) {
            LogToFile("Offsets loaded from cache.");
            return true;
        }
    }

    // Scan memory
    std::vector<std::pair<const char*, const char*>> patterns = {
        {"\x48\x8B\x05\x00\x00\x00\x00\x48\x85\xC0\x0F\x84", "xxx????xxxxx"}, // entityList
        {"\x48\x8B\x15\x00\x00\x00\x00\x48\x85\xD2\x74", "xxx????xxxx"},     // localPlayer
        {"\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xF3\x0F\x10\x00", "xxx????x????xxxx"}, // viewMatrix
        {"\xF3\x0F\x10\x00\x00\x00\x00\xF3\x0F\x11\x00", "xxx????xxx?"},      // health
    };

    bool found = false;
    for (size_t i = 0; i < patterns.size(); i++) {
        uintptr_t baseAddr = FindPattern("GameAssembly.dll", patterns[i].first, patterns[i].second);
        if (baseAddr && IsMemoryReadable(baseAddr + 3, sizeof(DWORD))) {
            DWORD offset = *reinterpret_cast<DWORD*>(baseAddr + 3);
            baseAddr = (baseAddr + 7) + offset;
            DWORD newOffset = static_cast<DWORD>(baseAddr - reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll")));
            if (ValidateOffset(newOffset, i == 0 ? "entityList" : (i == 1 ? "localPlayer" : (i == 2 ? "viewMatrix" : "health")))) {
                if (i == 0) g_entityListOffset = newOffset;
                else if (i == 1) g_localPlayerOffset = newOffset;
                else if (i == 2) g_viewMatrixOffset = newOffset;
                else if (i == 3) g_healthOffset = newOffset;
                found = true;
            }
        }
    }

    // Scan game files if memory scan fails
    if (!found && !gamePath.empty()) {
        found = ScanGameFilesForOffsets(gamePath);
    }

    if (found) {
        std::ofstream cacheFileOut("offset_cache.txt");
        if (cacheFileOut.is_open()) {
            cacheFileOut << "entity_list=0x" << std::hex << g_entityListOffset << "\n";
            cacheFileOut << "local_player=0x" << std::hex << g_localPlayerOffset << "\n";
            cacheFileOut << "view_matrix=0x" << std::hex << g_viewMatrixOffset << "\n";
            cacheFileOut << "health=0x" << std::hex << g_healthOffset << "\n";
            cacheFileOut.close();
        }
    }

    LogToFile(found ? "Offsets found." : "Failed to find offsets.");
    return found;
}

// Extract view matrix
bool ExtractViewMatrix() {
    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll"));
    if (!baseAddr) return false;
    uintptr_t viewMatrixAddr = baseAddr + g_viewMatrixOffset;
    if (!IsMemoryReadable(viewMatrixAddr, 16 * sizeof(float))) return false;
    memcpy(g_viewMatrix, reinterpret_cast<float*>(viewMatrixAddr), 16 * sizeof(float));
    g_viewPitch = atan2(-g_viewMatrix[8], sqrt(g_viewMatrix[0] * g_viewMatrix[0] + g_viewMatrix[4] * g_viewMatrix[4])) * 180.0f / 3.14159f;
    g_viewYaw = atan2(g_viewMatrix[4], g_viewMatrix[0]) * 180.0f / 3.14159f;
    return true;
}

// Save configuration
void SaveConfig() {
    std::ofstream config("config.txt");
    if (config.is_open()) {
        config << "esp_enabled=" << (g_enableESP ? "true" : "false") << "\n";
        config << "esp_color_r=" << g_espColorR << "\n";
        config << "esp_color_g=" << g_espColorG << "\n";
        config << "esp_color_b=" << g_espColorB << "\n";
        config << "aimbot_enabled=" << (g_enableAimbot ? "true" : "false") << "\n";
        config << "aimbot_fov=" << g_aimbotFOV << "\n";
        config << "aimbot_smooth=" << g_aimbotSmooth << "\n";
        config << "esp_distance_max=" << g_espDistanceMax << "\n";
        config << "aimbot_priority=" << g_aimbotPriority << "\n";
        config << "wallhack_enabled=" << (g_enableWallhack ? "true" : "false") << "\n";
        config.close();
        PrintConsoleMessage("Settings", "Saved");
    }
}

// Load configuration
void LoadConfig() {
    std::ifstream file("config.txt");
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            try {
                if (key == "esp_enabled") g_enableESP = (value == "true");
                else if (key == "esp_color_r") g_espColorR = std::stoi(value);
                else if (key == "esp_color_g") g_espColorG = std::stoi(value);
                else if (key == "esp_color_b") g_espColorB = std::stoi(value);
                else if (key == "aimbot_enabled") g_enableAimbot = (value == "true");
                else if (key == "aimbot_fov") g_aimbotFOV = std::stof(value);
                else if (key == "aimbot_smooth") g_aimbotSmooth = std::stof(value);
                else if (key == "esp_distance_max") g_espDistanceMax = std::stof(value);
                else if (key == "aimbot_priority") g_aimbotPriority = std::stoi(value);
                else if (key == "wallhack_enabled") g_enableWallhack = (value == "true");
            } catch (...) {
                LogToFile("Error parsing config: " + line);
            }
        }
    }
    file.close();
}

// Wallhack (ESP)
void DrawWallhack() {
    if (!g_enableESP || !g_enableWallhack) return;

    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll"));
    if (!baseAddr) return;
    DWORD entityList = *reinterpret_cast<DWORD*>(baseAddr + g_entityListOffset);
    if (!entityList || !IsMemoryReadable(entityList, sizeof(DWORD))) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hConsole, &csbi);
    COORD pos = { 0, (SHORT)(csbi.dwCursorPosition.Y + 1) };

    for (int i = 0; i < 128; i++) { // Increased to 128 entities
        uintptr_t entityAddr = baseAddr + g_entityListOffset + (i * 0x8);
        if (!IsMemoryReadable(entityAddr, sizeof(DWORD))) continue;
        DWORD entity = *reinterpret_cast<DWORD*>(entityAddr);
        if (!entity || !IsMemoryReadable(entity, sizeof(float) * 3 + g_healthOffset)) continue;

        float x = *reinterpret_cast<float*>(entity + 0x4);
        float y = *reinterpret_cast<float*>(entity + 0x8);
        float z = *reinterpret_cast<float*>(entity + 0xC);
        float health = *reinterpret_cast<float*>(entity + g_healthOffset);

        float dist = std::sqrt(x * x + y * y + z * z);
        if (dist > g_espDistanceMax || health <= 0) continue;

        float screenX = g_viewMatrix[0] * x + g_viewMatrix[4] * y + g_viewMatrix[8] * z + g_viewMatrix[12];
        float screenY = g_viewMatrix[1] * x + g_viewMatrix[5] * y + g_viewMatrix[9] * z + g_viewMatrix[13];
        float w = g_viewMatrix[3] * x + g_viewMatrix[7] * y + g_viewMatrix[11] * z + g_viewMatrix[15];
        if (w < 0.1f) continue;
        screenX /= w; screenY /= w;

        char buffer[128];
        snprintf(buffer, sizeof(buffer), "Entity %d: X=%.1f Y=%.1f Z=%.1f HP=%.0f Dist=%.1f (Screen: %.0f, %.0f)",
                 i, x, y, z, health, dist, screenX, screenY);
        SetConsoleCursorPosition(g_hConsole, pos);
        SetConsoleTextAttribute(g_hConsole, (g_espColorR > 0 ? FOREGROUND_RED : 0) |
                                         (g_espColorG > 0 ? FOREGROUND_GREEN : 0) |
                                         (g_espColorB > 0 ? FOREGROUND_BLUE : 0) |
                                         FOREGROUND_INTENSITY);
        WriteConsoleA(g_hConsole, buffer, strlen(buffer), NULL, NULL);
        pos.Y++;
    }
}

// Aimbot
void Aimbot() {
    if (!g_enableAimbot) return;

    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll"));
    if (!baseAddr) return;
    uintptr_t localPlayerAddr = baseAddr + g_localPlayerOffset;
    if (!IsMemoryReadable(localPlayerAddr, sizeof(DWORD))) return;
    DWORD localPlayer = *reinterpret_cast<DWORD*>(localPlayerAddr);
    if (!localPlayer || !IsMemoryReadable(localPlayer, sizeof(float) * 3)) return;

    DWORD entityList = *reinterpret_cast<DWORD*>(baseAddr + g_entityListOffset);
    if (!entityList || !IsMemoryReadable(entityList, sizeof(DWORD))) return;

    float closestDist = FLT_MAX;
    float targetX = 0, targetY = 0, targetZ = 0;
    DWORD targetEntity = 0;

    for (int i = 0; i < 128; i++) {
        uintptr_t entityAddr = baseAddr + g_entityListOffset + (i * 0x8);
        if (!IsMemoryReadable(entityAddr, sizeof(DWORD))) continue;
        DWORD entity = *reinterpret_cast<DWORD*>(entityAddr);
        if (!entity || !IsMemoryReadable(entity, sizeof(float) * 3 + g_healthOffset)) continue;

        float x = *reinterpret_cast<float*>(entity + 0x4);
        float y = *reinterpret_cast<float*>(entity + 0x8);
        float z = *reinterpret_cast<float*>(entity + 0xC) + 1.8f; // Headshot adjustment
        float health = *reinterpret_cast<float*>(entity + g_healthOffset);

        float dx = x - *reinterpret_cast<float*>(localPlayer + 0x4);
        float dy = y - *reinterpret_cast<float*>(localPlayer + 0x8);
        float dz = z - *reinterpret_cast<float*>(localPlayer + 0xC);
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (dist < closestDist && dist < g_aimbotFOV && health > 0) {
            if (g_aimbotPriority == 1 && targetEntity && *reinterpret_cast<float*>(targetEntity + g_healthOffset) > health) continue;
            closestDist = dist;
            targetX = x; targetY = y; targetZ = z;
            targetEntity = entity;
        }
    }

    if (closestDist != FLT_MAX) {
        float dx = targetX - *reinterpret_cast<float*>(localPlayer + 0x4);
        float dy = targetY - *reinterpret_cast<float*>(localPlayer + 0x8);
        float dz = targetZ - *reinterpret_cast<float*>(localPlayer + 0xC);
        float yaw = atan2(dy, dx) * 180.0f / 3.14159f - g_viewYaw;
        float pitch = atan2(-dz, std::sqrt(dx * dx + dy * dy)) * 180.0f / 3.14159f - g_viewPitch;

        float smoothYaw = yaw / g_aimbotSmooth;
        float smoothPitch = pitch / g_aimbotSmooth;
        if (std::abs(smoothYaw) > 0.05f || std::abs(smoothPitch) > 0.05f) {
            mouse_event(MOUSEEVENTF_MOVE, (DWORD)(smoothYaw * 15.0f), (DWORD)(smoothPitch * 15.0f), 0, 0);
        }
    }
}

// Input thread
DWORD WINAPI InputThread(LPVOID lpParam) {
    AllocConsole();
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTitleA("RustAlkadCheat2588 Console");

    // Enable ANSI colors
    DWORD consoleMode;
    GetConsoleMode(g_hConsole, &consoleMode);
    SetConsoleMode(g_hConsole, consoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    UpdateConsoleDisplay();

    // Find game path
    std::string gamePath;
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = std::string(exePath);
    size_t pos = exeDir.find_last_of("\\");
    if (pos != std::string::npos) {
        gamePath = exeDir.substr(0, pos);
    }

    LoadConfig();
    if (!FindOffsets(gamePath)) {
        PrintConsoleMessage("Offsets", "Failed to initialize");
    } else if (!ExtractViewMatrix()) {
        PrintConsoleMessage("View Matrix", "Failed to initialize");
    } else {
        PrintConsoleMessage("Offsets and View Matrix", "Initialized");
    }

    while (true) {
        // F1: Toggle Wallhack
        if (GetAsyncKeyState(VK_F1) & 0x8000) {
            g_enableWallhack = !g_enableWallhack;
            PrintConsoleMessage("Wallhack", g_enableWallhack ? "On" : "Off");
            SaveConfig();
            Sleep(200);
        }
        // F2: Toggle Aimbot
        if (GetAsyncKeyState(VK_F2) & 0x8000) {
            g_enableAimbot = !g_enableAimbot;
            PrintConsoleMessage("Aimbot", g_enableAimbot ? "On" : "Off");
            SaveConfig();
            Sleep(200);
        }
        // F3: Toggle ESP
        if (GetAsyncKeyState(VK_F3) & 0x8000) {
            g_enableESP = !g_enableESP;
            PrintConsoleMessage("ESP", g_enableESP ? "On" : "Off");
            SaveConfig();
            Sleep(200);
        }
        // F5: Save config
        if (GetAsyncKeyState(VK_F5) & 0x8000) {
            SaveConfig();
            Sleep(200);
        }
        // F6: Adjust ESP color R
        if (GetAsyncKeyState(VK_F6) & 0x8000) {
            g_espColorR = std::min(255, g_espColorR + 10);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "ESP R: %d", g_espColorR);
            PrintConsoleMessage("Settings", buffer);
            SaveConfig();
            Sleep(200);
        }
        // F7: Adjust ESP color G
        if (GetAsyncKeyState(VK_F7) & 0x8000) {
            g_espColorG = std::min(255, g_espColorG + 10);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "ESP G: %d", g_espColorG);
            PrintConsoleMessage("Settings", buffer);
            SaveConfig();
            Sleep(200);
        }
        // F8: Update offsets
        if (GetAsyncKeyState(VK_F8) & 0x8000) {
            PrintConsoleMessage("Offsets", "Updating...");
            if (FindOffsets(gamePath)) {
                PrintConsoleMessage("Offsets", "Updated");
                if (ExtractViewMatrix()) {
                    PrintConsoleMessage("View Matrix", "Updated");
                } else {
                    PrintConsoleMessage("View Matrix", "Failed to update");
                }
            } else {
                PrintConsoleMessage("Offsets", "Failed to update");
            }
            Sleep(200);
        }
        // F9: Adjust aimbot FOV
        if (GetAsyncKeyState(VK_F9) & 0x8000) {
            g_aimbotFOV = std::min(90.0f, g_aimbotFOV + 5.0f);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Aimbot FOV: %.1f", g_aimbotFOV);
            PrintConsoleMessage("Settings", buffer);
            SaveConfig();
            Sleep(200);
        }
        // F10: Adjust aimbot smooth
        if (GetAsyncKeyState(VK_F10) & 0x8000) {
            g_aimbotSmooth = std::max(1.0f, g_aimbotSmooth + 1.0f); // Inverted for intuitiveness
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Aimbot Smooth: %.1f", g_aimbotSmooth);
            PrintConsoleMessage("Settings", buffer);
            SaveConfig();
            Sleep(200);
        }
        // NumPad settings
        if (GetAsyncKeyState(VK_NUMPAD0) & 0x8000) {
            SaveConfig();
            Sleep(200);
        }
        if (GetAsyncKeyState(VK_NUMPAD1) & 0x8000) {
            g_enableESP = !g_enableESP;
            PrintConsoleMessage("ESP", g_enableESP ? "On" : "Off");
            SaveConfig();
            Sleep(200);
        }
        if (GetAsyncKeyState(VK_NUMPAD2) & 0x8000) {
            g_espColorR = std::min(255, g_espColorR + 10);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "ESP R: %d", g_espColorR);
            PrintConsoleMessage("Settings", buffer);
            SaveConfig();
            Sleep(200);
        }
        if (GetAsyncKeyState(VK_NUMPAD3) & 0x8000) {
            g_espColorG = std::min(255, g_espColorG + 10);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "ESP G: %d", g_espColorG);
            PrintConsoleMessage("Settings", buffer);
            SaveConfig();
            Sleep(200);
        }
        if (GetAsyncKeyState(VK_NUMPAD4) & 0x8000) {
            g_espColorB = std::min(255, g_espColorB + 10);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "ESP B: %d", g_espColorB);
            PrintConsoleMessage("Settings", buffer);
            SaveConfig();
            Sleep(200);
        }
        if (GetAsyncKeyState(VK_NUMPAD5) & 0x8000) {
            g_aimbotFOV = std::min(90.0f, g_aimbotFOV + 5.0f);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Aimbot FOV: %.1f", g_aimbotFOV);
            PrintConsoleMessage("Settings", buffer);
            SaveConfig();
            Sleep(200);
        }
        if (GetAsyncKeyState(VK_NUMPAD6) & 0x8000) {
            g_aimbotSmooth = std::max(1.0f, g_aimbotSmooth + 1.0f);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Aimbot Smooth: %.1f", g_aimbotSmooth);
            PrintConsoleMessage("Settings", buffer);
            SaveConfig();
            Sleep(200);
        }
        if (GetAsyncKeyState(VK_NUMPAD7) & 0x8000) {
            g_espDistanceMax = std::min(1000.0f, g_espDistanceMax + 50.0f);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "ESP Distance: %.1f", g_espDistanceMax);
            PrintConsoleMessage("Settings", buffer);
            SaveConfig();
            Sleep(200);
        }
        if (GetAsyncKeyState(VK_NUMPAD8) & 0x8000) {
            g_aimbotPriority = (g_aimbotPriority + 1) % 2;
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Aimbot Priority: %s", g_aimbotPriority ? "Health" : "Distance");
            PrintConsoleMessage("Settings", buffer);
            SaveConfig();
            Sleep(200);
        }

        Aimbot();
        DrawWallhack();
        Sleep(2 + (rand() % 3)); // Random delay to reduce detection
    }
    return 0;
}

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        srand(GetTickCount());
        CreateThread(NULL, 0, InputThread, NULL, 0, NULL);
    }
    return TRUE;
}
