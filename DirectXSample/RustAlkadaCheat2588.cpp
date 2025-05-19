#include <windows.h>
#include <wininet.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath> // Добавлено для sqrt
#pragma comment(lib, "wininet.lib")

// Глобальные переменные
bool g_showMenu = false;
bool g_enableESP = false;
bool g_enableAimbot = false;
int g_espColorR = 255, g_espColorG = 0, g_espColorB = 0;
float g_aimbotFOV = 30.0f;
float g_aimbotSmooth = 5.0f;

// Оффсеты для версии 2588 (начальные значения)
DWORD g_entityListOffset = 0x4D19345;
DWORD g_localPlayerOffset = 0x4D5C321;
DWORD g_viewMatrixOffset = 0x4D6F890;

// Консольная настройка
HANDLE g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

// Функция поиска сигнатуры в памяти
DWORD FindPattern(const char* moduleName, const char* pattern, const char* mask) {
    HMODULE hModule = GetModuleHandleA(moduleName);
    if (!hModule) return 0;

    MODULEINFO modInfo;
    GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(MODULEINFO));

    DWORD base = (DWORD)hModule;
    DWORD size = modInfo.SizeOfImage;

    DWORD patternLength = (DWORD)strlen(mask);

    for (DWORD i = 0; i < size - patternLength; i++) {
        bool found = true;
        for (DWORD j = 0; j < patternLength; j++) {
            found &= mask[j] == '?' || pattern[j] == *(char*)(base + i + j);
        }
        if (found) {
            return base + i;
        }
    }
    return 0;
}

// Поиск оффсетов
bool FindOffsets() {
    const char* pattern = "\x48\x8B\x00\x00\x00\x00\x00\xF6\x80\x2F\x01\x00\x00";
    const char* mask = "xx?????xxxxxx";

    DWORD baseAddr = FindPattern("GameAssembly.dll", pattern, mask);
    if (!baseAddr) {
        return false;
    }

    baseAddr += 0x10; // Примерное смещение после сигнатуры

    g_entityListOffset = *(DWORD*)(baseAddr + 0x100); // Примерное смещение
    g_localPlayerOffset = *(DWORD*)(baseAddr + 0x200);
    g_viewMatrixOffset = *(DWORD*)(baseAddr + 0x300);

    if (g_entityListOffset < 0x400000 || g_localPlayerOffset < 0x400000 || g_viewMatrixOffset < 0x400000) {
        return false;
    }

    return true;
}

// Чтение настроек из config.txt
void LoadConfig() {
    std::ifstream file("config.txt");
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
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
        }
    }
    file.close();
}

// Автообновление оффсетов (через HTTP, F5)
bool UpdateOffsets() {
    HINTERNET hInternet = InternetOpenA("CheatUpdater", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hConnect = InternetOpenUrlA(hInternet, "http://example.com/offsets.txt", NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return false;
    }

    char buffer[1024];
    DWORD bytesRead;
    std::string data;
    while (InternetReadFile(hConnect, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        data += buffer;
    }

    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

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
    return true;
}

// Текстовый ESP (упрощённый)
void DrawESP() {
    if (!g_enableESP) return;

    DWORD baseAddr = 0x400000; // Базовый адрес игры (нужно найти)
    DWORD entityList = *(DWORD*)(baseAddr + g_entityListOffset);
    if (!entityList) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hConsole, &csbi);
    COORD pos = { 0, (SHORT)(csbi.dwCursorPosition.Y + 1) };

    for (int i = 0; i < 32; i++) {
        DWORD entityAddr = baseAddr + g_entityListOffset + (i * 0x4); // Корректное вычисление адреса
        DWORD entity = *(DWORD*)((uintptr_t)entityAddr); // Приведение с использованием uintptr_t
        if (!entity) continue;

        float x = *(float*)((uintptr_t)(entity + 0x4)); // Корректное приведение
        float y = *(float*)((uintptr_t)(entity + 0x8)); // Корректное приведение

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Player %d: X=%.1f Y=%.1f", i, x, y);
        SetConsoleCursorPosition(g_hConsole, pos);
        SetConsoleTextAttribute(g_hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        WriteConsoleA(g_hConsole, buffer, strlen(buffer), NULL, NULL);
        pos.Y++;
    }
}

// Аимбот
void Aimbot() {
    if (!g_enableAimbot) return;

    DWORD baseAddr = 0x400000;
    DWORD localPlayerAddr = baseAddr + g_localPlayerOffset;
    DWORD localPlayer = *(DWORD*)((uintptr_t)localPlayerAddr);
    if (!localPlayer) return;

    DWORD entityList = *(DWORD*)((uintptr_t)(baseAddr + g_entityListOffset));
    if (!entityList) return;

    float closestDist = FLT_MAX;
    float targetX = 0, targetY = 0;

    for (int i = 0; i < 32; i++) {
        DWORD entityAddr = baseAddr + g_entityListOffset + (i * 0x4);
        DWORD entity = *(DWORD*)((uintptr_t)entityAddr);
        if (!entity) continue;

        float x = *(float*)((uintptr_t)(entity + 0x4));
        float y = *(float*)((uintptr_t)(entity + 0x8));

        float dist = std::sqrt(x * x + y * y); // Используем std::sqrt
        if (dist < closestDist && dist < g_aimbotFOV) {
            closestDist = dist;
            targetX = x;
            targetY = y;
        }
    }

    if (closestDist != FLT_MAX) {
        mouse_event(MOUSEEVENTF_MOVE, (DWORD)(targetX / g_aimbotSmooth), (DWORD)(targetY / g_aimbotSmooth), 0, 0);
    }
}

// Поток для обработки клавиш
DWORD WINAPI InputThread(LPVOID lpParam) {
    AllocConsole();
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTitleA("RustAlkadCheat2588 Console");

    while (true) {
        if (GetAsyncKeyState(VK_INSERT) & 1) {
            g_showMenu = !g_showMenu;
            SetConsoleCursorPosition(g_hConsole, { 0, 0 });
            WriteConsoleA(g_hConsole, "Menu toggled\n", 12, NULL, NULL);
        }
        if (GetAsyncKeyState(VK_F1) & 1) {
            g_enableESP = !g_enableESP;
            SetConsoleCursorPosition(g_hConsole, { 0, 0 });
            WriteConsoleA(g_hConsole, g_enableESP ? "ESP enabled\n" : "ESP disabled\n", g_enableESP ? 12 : 13, NULL, NULL);
        }
        if (GetAsyncKeyState(VK_F2) & 1) {
            g_enableAimbot = !g_enableAimbot;
            SetConsoleCursorPosition(g_hConsole, { 0, 0 });
            WriteConsoleA(g_hConsole, g_enableAimbot ? "Aimbot enabled\n" : "Aimbot disabled\n", g_enableAimbot ? 14 : 15, NULL, NULL);
        }
        if (GetAsyncKeyState(VK_F5) & 1) {
            if (UpdateOffsets()) {
                SetConsoleCursorPosition(g_hConsole, { 0, 0 });
                WriteConsoleA(g_hConsole, "Offsets updated successfully via HTTP!\n", 37, NULL, NULL);
            }
            else {
                SetConsoleCursorPosition(g_hConsole, { 0, 0 });
                WriteConsoleA(g_hConsole, "Failed to update offsets via HTTP!\n", 34, NULL, NULL);
            }
        }
        if (GetAsyncKeyState(VK_F6) & 1) {
            if (FindOffsets()) {
                SetConsoleCursorPosition(g_hConsole, { 0, 0 });
                char buffer[128];
                snprintf(buffer, sizeof(buffer), "Offsets found: entityList=0x%lX, localPlayer=0x%lX, viewMatrix=0x%lX\n",
                         g_entityListOffset, g_localPlayerOffset, g_viewMatrixOffset);
                WriteConsoleA(g_hConsole, buffer, strlen(buffer), NULL, NULL);
            }
            else {
                SetConsoleCursorPosition(g_hConsole, { 0, 0 });
                WriteConsoleA(g_hConsole, "Failed to find offsets!\n", 23, NULL, NULL);
            }
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
