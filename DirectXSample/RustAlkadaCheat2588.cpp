#include <windows.h>
#include <wininet.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <cfloat>
#include <psapi.h>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "psapi.lib")

// Глобальные переменные
bool g_showMenu = false;
bool g_enableESP = false;
bool g_enableAimbot = false;
int g_espColorR = 255, g_espColorG = 0, g_espColorB = 0;
float g_aimbotFOV = 30.0f, g_aimbotSmooth = 5.0f;
float g_espDistanceMax = 200.0f;
int g_aimbotPriority = 0; // 0 - ближайший, 1 - с наименьшим HP
float g_viewPitch = 0.0f, g_viewYaw = 0.0f;

// Оффсеты для версии 2588
DWORD g_entityListOffset = 0x4D19345;
DWORD g_localPlayerOffset = 0x4D5C321;
DWORD g_viewMatrixOffset = 0x4D6F890;

// View Matrix (4x4)
float g_viewMatrix[16] = { 0 };

// Консоль
HANDLE g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

// Логирование
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

// Проверка памяти
bool IsMemoryReadable(uintptr_t address, size_t size) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi))) {
        return mbi.State == MEM_COMMIT && mbi.Protect != PAGE_NOACCESS;
    }
    return false;
}

// Поиск сигнатуры
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

// Валидация оффсета
bool ValidateOffset(DWORD offset, const std::string& name) {
    if (offset < 0x1000000 || offset > 0xFFFFFFF) return false;
    if (!IsMemoryReadable(offset, sizeof(DWORD))) return false;
    LogToFile(name + " offset validated: 0x" + std::to_string(offset));
    return true;
}

// Оптимизированный поиск оффсетов
bool FindOffsets() {
    LogToFile("Starting optimized offset search for Rust 2588 Jungle Update...");

    // HTTP обновление
    HINTERNET hInternet = InternetOpenA("CheatUpdater", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (hInternet) {
        HINTERNET hConnect = InternetOpenUrlA(hInternet, "http://example.com/offsets.txt", NULL, 0, INTERNET_FLAG_RELOAD, 0);
        if (hConnect) {
            char buffer[1024];
            DWORD bytesRead;
            std::string data;
            while (InternetReadFile(hConnect, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                data += buffer;
            }
            InternetCloseHandle(hConnect);
            std::istringstream iss(data);
            std::string line;
            while (std::getline(iss, line)) {
                std::istringstream lineStream(line);
                std::string key, value;
                if (std::getline(lineStream, key, '=') && std::getline(lineStream, value)) {
                    if (key == "entity_list") g_entityListOffset = std::stoul(value, nullptr, 16);
                    else if (key == "local_player") g_localPlayerOffset = std::stoul(value, nullptr, 16);
                    else if (key == "view_matrix") g_viewMatrixOffset = std::stoul(value, nullptr, 16);
                }
            }
            InternetCloseHandle(hInternet);
            if (ValidateOffset(g_entityListOffset, "entityList") &&
                ValidateOffset(g_localPlayerOffset, "localPlayer") &&
                ValidateOffset(g_viewMatrixOffset, "viewMatrix")) {
                LogToFile("Offsets updated via HTTP: entityList=0x" + std::to_string(g_entityListOffset) +
                          ", localPlayer=0x" + std::to_string(g_localPlayerOffset) +
                          ", viewMatrix=0x" + std::to_string(g_viewMatrixOffset));
                return true;
            }
        }
    }

    // Сигнатуры
    std::vector<std::pair<const char*, const char*>> patterns = {
        {"\x48\x8B\x05\x00\x00\x00\x00\x48\x85\xC0\x0F\x84", "xxx????xxxxx"}, // entityList
        {"\x48\x8B\x15\x00\x00\x00\x00\x48\x85\xD2\x74", "xxx????xxxx"},     // localPlayer
        {"\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xF3\x0F\x10\x00", "xxx????x????xxxx"} // viewMatrix
    };

    for (int i = 0; i < 3; i++) {
        uintptr_t baseAddr = FindPattern("GameAssembly.dll", patterns[i].first, patterns[i].second);
        if (baseAddr) {
            DWORD offset = *reinterpret_cast<DWORD*>(baseAddr + 3);
            baseAddr = (baseAddr + 7) + offset;
            DWORD newOffset = static_cast<DWORD>(baseAddr - reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll")));
            if (ValidateOffset(newOffset, i == 0 ? "entityList" : (i == 1 ? "localPlayer" : "viewMatrix"))) {
                if (i == 0) g_entityListOffset = newOffset;
                else if (i == 1) g_localPlayerOffset = newOffset;
                else g_viewMatrixOffset = newOffset;
            }
        }
    }

    if (ValidateOffset(g_entityListOffset, "entityList") &&
        ValidateOffset(g_localPlayerOffset, "localPlayer") &&
        ValidateOffset(g_viewMatrixOffset, "viewMatrix")) {
        LogToFile("Offsets found via signatures: entityList=0x" + std::to_string(g_entityListOffset) +
                  ", localPlayer=0x" + std::to_string(g_localPlayerOffset) +
                  ", viewMatrix=0x" + std::to_string(g_viewMatrixOffset));
        return true;
    }

    LogToFile("Failed to find offsets.");
    return false;
}

// Чтение настроек
void LoadConfig() {
    std::ifstream file("config.txt");
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            if (key == "esp_enabled") g_enableESP = (value == "true");
            else if (key == "esp_color_r") g_espColorR = std::stoi(value);
            else if (key == "esp_color_g") g_espColorG = std::stoi(value);
            else if (key == "esp_color_b") g_espColorB = std::stoi(value);
            else if (key == "aimbot_enabled") g_enableAimbot = (value == "true");
            else if (key == "aimbot_fov") g_aimbotFOV = std::stof(value);
            else if (key == "aimbot_smooth") g_aimbotSmooth = std::stof(value);
            else if (key == "esp_distance_max") g_espDistanceMax = std::stof(value);
            else if (key == "aimbot_priority") g_aimbotPriority = std::stoi(value);
        }
    }
    file.close();
}

// Извлечение view matrix
bool ExtractViewMatrix() {
    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll"));
    uintptr_t viewMatrixAddr = baseAddr + g_viewMatrixOffset;
    if (!IsMemoryReadable(viewMatrixAddr, 16 * sizeof(float))) return false;
    memcpy(g_viewMatrix, reinterpret_cast<float*>(viewMatrixAddr), 16 * sizeof(float));
    g_viewPitch = atan2(-g_viewMatrix[8], sqrt(g_viewMatrix[0] * g_viewMatrix[0] + g_viewMatrix[4] * g_viewMatrix[4])) * 180.0f / 3.14159f;
    g_viewYaw = atan2(g_viewMatrix[4], g_viewMatrix[0]) * 180.0f / 3.14159f;
    return true;
}

// ESP (F2)
void DrawESP() {
    if (!g_enableESP) return;

    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll"));
    DWORD entityList = *reinterpret_cast<DWORD*>(baseAddr + g_entityListOffset);
    if (!entityList) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hConsole, &csbi);
    COORD pos = { 0, (SHORT)(csbi.dwCursorPosition.Y + 1) };

    for (int i = 0; i < 32; i++) {
        uintptr_t entityAddr = baseAddr + g_entityListOffset + (i * 0x4);
        DWORD entity = *reinterpret_cast<DWORD*>(entityAddr);
        if (!entity) continue;

        float x = *reinterpret_cast<float*>(entity + 0x4); // Пример координат
        float y = *reinterpret_cast<float*>(entity + 0x8);
        float z = *reinterpret_cast<float*>(entity + 0xC); // Добавим Z
        float health = *reinterpret_cast<float*>(entity + 0x10); // Пример HP

        float dist = std::sqrt(x * x + y * y + z * z);
        if (dist > g_espDistanceMax) continue;

        // 2D позиция на экране через view matrix
        float screenX = g_viewMatrix[0] * x + g_viewMatrix[4] * y + g_viewMatrix[8] * z + g_viewMatrix[12];
        float screenY = g_viewMatrix[1] * x + g_viewMatrix[5] * y + g_viewMatrix[9] * z + g_viewMatrix[13];
        float w = g_viewMatrix[3] * x + g_viewMatrix[7] * y + g_viewMatrix[11] * z + g_viewMatrix[15];
        if (w < 0.1f) continue; // За экраном
        screenX /= w; screenY /= w;

        char buffer[128];
        snprintf(buffer, sizeof(buffer), "Player %d: X=%.1f Y=%.1f Z=%.1f HP=%.0f Dist=%.1f (Screen: %.0f, %.0f)",
                 i, x, y, z, health, dist, screenX, screenY);
        SetConsoleCursorPosition(g_hConsole, pos);
        SetConsoleTextAttribute(g_hConsole, (g_espColorR << 8) | (g_espColorG << 4) | g_espColorB);
        WriteConsoleA(g_hConsole, buffer, strlen(buffer), NULL, NULL);
        pos.Y++;
    }
}

// Аимбот (F1)
void Aimbot() {
    if (!g_enableAimbot) return;

    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll"));
    uintptr_t localPlayerAddr = baseAddr + g_localPlayerOffset;
    DWORD localPlayer = *reinterpret_cast<DWORD*>(localPlayerAddr);
    if (!localPlayer) return;

    DWORD entityList = *reinterpret_cast<DWORD*>(baseAddr + g_entityListOffset);
    if (!entityList) return;

    float closestDist = FLT_MAX;
    float targetX = 0, targetY = 0, targetZ = 0;
    DWORD targetEntity = 0;

    for (int i = 0; i < 32; i++) {
        uintptr_t entityAddr = baseAddr + g_entityListOffset + (i * 0x4);
        DWORD entity = *reinterpret_cast<DWORD*>(entityAddr);
        if (!entity) continue;

        float x = *reinterpret_cast<float*>(entity + 0x4);
        float y = *reinterpret_cast<float*>(entity + 0x8);
        float z = *reinterpret_cast<float*>(entity + 0xC);
        float health = *reinterpret_cast<float*>(entity + 0x10);

        float dx = x - *reinterpret_cast<float*>(localPlayer + 0x4);
        float dy = y - *reinterpret_cast<float*>(localPlayer + 0x8);
        float dz = z - *reinterpret_cast<float*>(localPlayer + 0xC);
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (dist < closestDist && dist < g_aimbotFOV) {
            if (g_aimbotPriority == 1 && health > 0) {
                if (targetEntity && *reinterpret_cast<float*>(targetEntity + 0x10) > health) continue;
            }
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

        mouse_event(MOUSEEVENTF_MOVE, (DWORD)(yaw / g_aimbotSmooth), (DWORD)(pitch / g_aimbotSmooth), 0, 0);
    }
}

// Поток для обработки клавиш
DWORD WINAPI InputThread(LPVOID lpParam) {
    AllocConsole();
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTitleA("RustAlkadCheat2588 Console");

    if (!FindOffsets()) {
        SetConsoleCursorPosition(g_hConsole, { 0, 0 });
        WriteConsoleA(g_hConsole, "Failed to find offsets!\n", 23, NULL, NULL);
    } else if (!ExtractViewMatrix()) {
        SetConsoleCursorPosition(g_hConsole, { 0, 0 });
        WriteConsoleA(g_hConsole, "Failed to extract view matrix!\n", 31, NULL, NULL);
    }

    while (true) {
        if (GetAsyncKeyState(VK_INSERT) & 1) {
            g_showMenu = !g_showMenu;
            SetConsoleCursorPosition(g_hConsole, { 0, 0 });
            WriteConsoleA(g_hConsole, "Menu toggled\n", 12, NULL, NULL);
        }
        if (GetAsyncKeyState(VK_F1) & 1) {
            g_enableAimbot = !g_enableAimbot;
            SetConsoleCursorPosition(g_hConsole, { 0, 0 });
            WriteConsoleA(g_hConsole, g_enableAimbot ? "Aimbot enabled\n" : "Aimbot disabled\n", g_enableAimbot ? 14 : 15, NULL, NULL);
        }
        if (GetAsyncKeyState(VK_F2) & 1) {
            g_enableESP = !g_enableESP;
            SetConsoleCursorPosition(g_hConsole, { 0, 0 });
            WriteConsoleA(g_hConsole, g_enableESP ? "ESP enabled\n" : "ESP disabled\n", g_enableESP ? 12 : 13, NULL, NULL);
        }
        if (GetAsyncKeyState(VK_NUMPAD0) & 1) {
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
                config.close();
                SetConsoleCursorPosition(g_hConsole, { 0, 0 });
                WriteConsoleA(g_hConsole, "Settings saved!\n", 16, NULL, NULL);
            }
        }
        if (GetAsyncKeyState(VK_NUMPAD1) & 1) { g_espColorR = std::min(255, g_espColorR + 10); SetConsoleCursorPosition(g_hConsole, { 0, 0 }); WriteConsoleA(g_hConsole, "ESP R set to ", 13, NULL, NULL); }
        if (GetAsyncKeyState(VK_NUMPAD2) & 1) { g_espColorG = std::min(255, g_espColorG + 10); SetConsoleCursorPosition(g_hConsole, { 0, 0 }); WriteConsoleA(g_hConsole, "ESP G set to ", 13, NULL, NULL); }
        if (GetAsyncKeyState(VK_NUMPAD3) & 1) { g_espColorB = std::min(255, g_espColorB + 10); SetConsoleCursorPosition(g_hConsole, { 0, 0 }); WriteConsoleA(g_hConsole, "ESP B set to ", 13, NULL, NULL); }
        if (GetAsyncKeyState(VK_NUMPAD4) & 1) { g_aimbotFOV = std::min(90.0f, g_aimbotFOV + 5.0f); SetConsoleCursorPosition(g_hConsole, { 0, 0 }); WriteConsoleA(g_hConsole, "Aimbot FOV set to ", 18, NULL, NULL); }
        if (GetAsyncKeyState(VK_NUMPAD5) & 1) { g_aimbotSmooth = std::max(1.0f, g_aimbotSmooth - 1.0f); SetConsoleCursorPosition(g_hConsole, { 0, 0 }); WriteConsoleA(g_hConsole, "Aimbot Smooth set to ", 21, NULL, NULL); }
        if (GetAsyncKeyState(VK_NUMPAD6) & 1) { g_espDistanceMax += 50.0f; SetConsoleCursorPosition(g_hConsole, { 0, 0 }); WriteConsoleA(g_hConsole, "ESP Distance set to ", 20, NULL, NULL); }
        if (GetAsyncKeyState(VK_NUMPAD7) & 1) { g_aimbotPriority = (g_aimbotPriority + 1) % 2; SetConsoleCursorPosition(g_hConsole, { 0, 0 }); WriteConsoleA(g_hConsole, "Aimbot Priority set to ", 23, NULL, NULL); }

        if (GetAsyncKeyState(VK_NUMPAD1) & 1 || GetAsyncKeyState(VK_NUMPAD2) & 1 || GetAsyncKeyState(VK_NUMPAD3) & 1) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d\n", g_espColorR);
            WriteConsoleA(g_hConsole, buffer, strlen(buffer), NULL, NULL);
        }
        if (GetAsyncKeyState(VK_NUMPAD4) & 1) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%.1f\n", g_aimbotFOV);
            WriteConsoleA(g_hConsole, buffer, strlen(buffer), NULL, NULL);
        }
        if (GetAsyncKeyState(VK_NUMPAD5) & 1) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%.1f\n", g_aimbotSmooth);
            WriteConsoleA(g_hConsole, buffer, strlen(buffer), NULL, NULL);
        }
        if (GetAsyncKeyState(VK_NUMPAD6) & 1) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%.1f\n", g_espDistanceMax);
            WriteConsoleA(g_hConsole, buffer, strlen(buffer), NULL, NULL);
        }
        if (GetAsyncKeyState(VK_NUMPAD7) & 1) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d\n", g_aimbotPriority);
            WriteConsoleA(g_hConsole, buffer, strlen(buffer), NULL, NULL);
        }

        Aimbot();
        if (g_enableESP) DrawESP();
        Sleep(1);
    }
    return 0;
}

// Точка входа
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadConfig();
        CreateThread(NULL, 0, InputThread, NULL, 0, NULL);
    }
    return TRUE;
}
