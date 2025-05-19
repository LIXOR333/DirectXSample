#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <ctime>
#include "../json/single_include/nlohmann/json.hpp"

using json = nlohmann::json;

// Оффсеты (по умолчанию 0, обновляются автоматически)
namespace Offsets {
    DWORD64 GWorld = 0x0;
    DWORD64 GNames = 0x0;
    DWORD64 LocalPlayer = 0x0;
    DWORD64 EntityList = 0x0;
    DWORD64 Health = 0x320;
    DWORD64 Position = 0x1A0;
    DWORD64 Name = 0x4B0;
    bool needsUpdate = true;
}

// Глобальные переменные
HMODULE hModule;
bool bShowESP = false;
bool bShowMenu = false;
HWND hwndOverlay = NULL;
HWND hwndMenu = NULL;
HDC hdcOverlay = NULL;
HDC hdcMem = NULL; // Контекст памяти для двойной буферизации
HBITMAP hbmMem = NULL;
HFONT hFont = NULL;
std::vector<Player> playerList;
int screenWidth = GetSystemMetrics(SM_CXSCREEN);
int screenHeight = GetSystemMetrics(SM_CYSCREEN);

// Структура игрока
struct Player {
    DWORD64 entity;
    float x, y, z;
    int health;
    std::string name;
    bool isVisible;
};

// Чтение памяти
template<typename T>
T ReadMemory(DWORD64 address) {
    if (!address) return T();
    T value;
    ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, &value, sizeof(T), NULL);
    return value;
}

// Расшифровщик строк
std::string DecryptString(DWORD64 address) {
    if (!address) return "";
    char encrypted[256];
    SIZE_T bytesRead;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, encrypted, sizeof(encrypted) - 1, &bytesRead)) return "";
    encrypted[bytesRead] = '\0';

    std::string decrypted;
    srand(static_cast<unsigned>(time(0)));
    int key = rand() % 255 + 1;
    for (size_t i = 0; i < strlen(encrypted); i++) {
        char c = encrypted[i] ^ key;
        if (c >= 32 && c <= 126) decrypted += c;
    }
    return decrypted.empty() ? "Unknown" : decrypted;
}

// Поиск сигнатур
DWORD64 FindPattern(const char* pattern, const char* mask, DWORD64 start, DWORD64 end) {
    size_t len = strlen(mask);
    for (DWORD64 i = start; i < end - len; i++) {
        bool found = true;
        for (size_t j = 0; j < len; j++) {
            if (mask[j] != '?' && pattern[j] != *(char*)(i + j)) {
                found = false;
                break;
            }
        }
        if (found) return i;
    }
    return 0;
}

// Автодампер оффсетов
void AutoDumpOffsets() {
    static DWORD64 lastProcessId = 0;
    DWORD64 currentProcessId = GetCurrentProcessId();
    if (lastProcessId != currentProcessId || Offsets::needsUpdate) {
        DWORD64 baseAddress = (DWORD64)GetModuleHandle(NULL);
        DWORD64 endAddress = baseAddress + 0x10000000;

        Offsets::GWorld = FindPattern("\x48\x8B\x05\x00\x00\x00\x00\x48\x85\xC0\x0F\x84", "xxx????xxxxx", baseAddress, endAddress);
        if (!Offsets::GWorld) Offsets::GWorld = FindPattern("\x48\x8B\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00", "xxx????x????", baseAddress, endAddress);

        Offsets::GNames = FindPattern("\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x8B\xC8", "xxx????x????xxx", baseAddress, endAddress);
        if (!Offsets::GNames) Offsets::GNames = FindPattern("\x48\x89\x1D\x00\x00\x00\x00\x48\x85\xDB", "xxx????xxx", baseAddress, endAddress);

        Offsets::LocalPlayer = FindPattern("\x48\x8B\x05\x00\x00\x00\x00\x48\x85\xC0\x74\x0E", "xxx????xxxxxx", baseAddress, endAddress);
        if (!Offsets::LocalPlayer) Offsets::LocalPlayer = FindPattern("\x48\x8B\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00", "xxx????x????", baseAddress, endAddress);

        Offsets::EntityList = FindPattern("\x4C\x8B\x0D\x00\x00\x00\x00\x4C\x8B\xC0", "xxx????xxx", baseAddress, endAddress);
        if (!Offsets::EntityList) Offsets::EntityList = FindPattern("\x48\x8B\x05\x00\x00\x00\x00\x48\x89\x04\xC8", "xxx????xxxxx", baseAddress, endAddress);

        json offsets;
        offsets["GWorld"] = Offsets::GWorld;
        offsets["GNames"] = Offsets::GNames;
        offsets["LocalPlayer"] = Offsets::LocalPlayer;
        offsets["EntityList"] = Offsets::EntityList;
        offsets["Health"] = Offsets::Health;
        offsets["Position"] = Offsets::Position;
        offsets["Name"] = Offsets::Name;

        std::ofstream file("offsets_dump.json");
        file << offsets.dump(4);
        file.close();

        Offsets::needsUpdate = false;
        lastProcessId = currentProcessId;
    }
}

// Обновление списка игроков
void UpdatePlayers() {
    playerList.clear();
    if (!Offsets::EntityList) return;

    DWORD64 entityList = ReadMemory<DWORD64>(Offsets::EntityList);
    if (!entityList) return;

    for (int i = 0; i < 100; i++) {
        DWORD64 entity = ReadMemory<DWORD64>(entityList + i * sizeof(DWORD64));
        if (!entity) continue;

        Player player;
        player.entity = entity;
        player.health = ReadMemory<int>(entity + Offsets::Health);
        player.x = ReadMemory<float>(entity + Offsets::Position);
        player.y = ReadMemory<float>(entity + Offsets::Position + 4);
        player.z = ReadMemory<float>(entity + Offsets::Position + 8);
        player.name = DecryptString(entity + Offsets::Name);
        player.isVisible = true; // Замени на реальную проверку
        playerList.push_back(player);
    }
}

// Простая матрица проекции для WorldToScreen
bool WorldToScreen(float x, float y, float z, float& screenX, float& screenY) {
    // Примерная матрица проекции (нужна реальная матрица вида из игры)
    float fov = 90.0f; // Условный угол обзора
    float aspectRatio = (float)screenWidth / screenHeight;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    float tanHalfFOV = tanf(fov * 0.5f * 3.14159f / 180.0f);
    float height = nearPlane * tanHalfFOV;
    float width = height * aspectRatio;

    float range = farPlane - nearPlane;
    float projX = (2.0f * nearPlane) / (width + width);
    float projY = (2.0f * nearPlane) / (height + height);
    float projW = -(farPlane + nearPlane) / range;
    float projZ = -(2.0f * farPlane * nearPlane) / range;

    // Проекция (упрощённая, без ViewMatrix)
    float worldX = x;
    float worldY = y;
    float worldZ = z;

    screenX = (worldX * projX * screenWidth / 2) + (screenWidth / 2);
    screenY = (worldY * projY * screenHeight / 2) + (screenHeight / 2);

    return (worldZ > 0); // Простая проверка видимости
}

// Рендеринг текста через GDI
void DrawText(HDC hdc, const char* text, float x, float y, COLORREF color) {
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, hFont);
    TextOutA(hdc, (int)x, (int)y, text, strlen(text));
}

// Рендеринг бокса через GDI
void DrawBox(HDC hdc, float x, float y, float w, float h, COLORREF color) {
    HPEN hPen = CreatePen(PS_SOLID, 1, color);
    HBRUSH hBrush = (HBRUSH)GetStockObject(NULL_BRUSH); // Прозрачный фон
    SelectObject(hdc, hPen);
    SelectObject(hdc, hBrush);
    Rectangle(hdc, (int)x, (int)y, (int)(x + w), (int)(y + h));
    DeleteObject(hPen);
}

// Window procedure для меню
LRESULT CALLBACK MenuWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1000: // Чекбокс ESP
            bShowESP = !bShowESP;
            CheckDlgButton(hWnd, 1000, bShowESP ? BST_CHECKED : BST_UNCHECKED);
            break;
        case 1001: // Кнопка дампа оффсетов
            Offsets::needsUpdate = true;
            AutoDumpOffsets();
            break;
        case 1002: // Кнопка перезагрузки оффсетов
            Offsets::needsUpdate = true;
            break;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Window procedure для оверлея
LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        screenWidth = LOWORD(lParam);
        screenHeight = HIWORD(lParam);
        if (hbmMem) DeleteObject(hbmMem);
        hbmMem = CreateCompatibleBitmap(hdcOverlay, screenWidth, screenHeight);
        SelectObject(hdcMem, hbmMem);
        return 0;
    case WM_PAINT:
        if (hdcMem && hdcOverlay) {
            BitBlt(hdcOverlay, 0, 0, screenWidth, screenHeight, hdcMem, 0, 0, SRCCOPY);
        }
        ValidateRect(hWnd, NULL);
        return 0;
    case WM_DESTROY:
        if (hbmMem) DeleteObject(hbmMem);
        if (hdcMem) DeleteDC(hdcMem);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Инициализация GDI
bool InitGDI(HWND hWnd) {
    hdcOverlay = GetDC(hWnd);
    if (!hdcOverlay) return false;

    hdcMem = CreateCompatibleDC(hdcOverlay);
    if (!hdcMem) {
        ReleaseDC(hWnd, hdcOverlay);
        return false;
    }

    hbmMem = CreateCompatibleBitmap(hdcOverlay, screenWidth, screenHeight);
    if (!hbmMem) {
        DeleteDC(hdcMem);
        ReleaseDC(hWnd, hdcOverlay);
        return false;
    }
    SelectObject(hdcMem, hbmMem);

    hFont = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Arial");
    if (!hFont) {
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);
        ReleaseDC(hWnd, hdcOverlay);
        return false;
    }

    SetBkMode(hdcMem, TRANSPARENT);
    return true;
}

// Рендеринг сцены
void RenderScene() {
    if (!hdcMem || !hdcOverlay) return;

    // Очистка буфера
    RECT rect = { 0, 0, screenWidth, screenHeight };
    FillRect(hdcMem, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));

    if (bShowESP) {
        for (const auto& player : playerList) {
            float screenX, screenY;
            if (WorldToScreen(player.x, player.y, player.z, screenX, screenY)) {
                char label[64];
                sprintf(label, "%s [HP: %d]", player.name.c_str(), player.health);
                DrawText(hdcMem, label, screenX, screenY - 20, player.isVisible ? RGB(0, 255, 0) : RGB(255, 0, 0));
                DrawBox(hdcMem, screenX - 25, screenY - 50, 50, 100, player.isVisible ? RGB(0, 255, 0) : RGB(255, 0, 0));
            }
        }
    }

    // Обновление оверлея с прозрачностью
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    SIZE size = { screenWidth, screenHeight };
    POINT ptZero = { 0, 0 };
    POINT ptPos = { 0, 0 };
    UpdateLayeredWindow(hwndOverlay, NULL, &ptPos, &size, hdcMem, &ptZero, 0, &blend, ULW_ALPHA);
}

// Основной поток
DWORD WINAPI MainThread(LPVOID lpParam) {
    // Создаём оверлей
    WNDCLASSEX wcOverlay = { sizeof(WNDCLASSEX), CS_OWNDC, OverlayWndProc, 0, 0, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"Overlay", NULL };
    RegisterClassEx(&wcOverlay);
    hwndOverlay = CreateWindow(wcOverlay.lpszClassName, L"Rust Alkad Overlay", WS_POPUP | WS_VISIBLE, 0, 0, screenWidth, screenHeight, NULL, NULL, wcOverlay.hInstance, NULL);
    SetWindowLong(hwndOverlay, GWL_EXSTYLE, GetWindowLong(hwndOverlay, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT);
    SetLayeredWindowAttributes(hwndOverlay, 0, 255, LWA_ALPHA);

    // Инициализация GDI
    if (!InitGDI(hwndOverlay)) {
        DestroyWindow(hwndOverlay);
        return 0;
    }

    // Создаём меню
    WNDCLASSEX wcMenu = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, MenuWndProc, 0, 0, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"Menu", NULL };
    RegisterClassEx(&wcMenu);
    hwndMenu = CreateWindow(wcMenu.lpszClassName, L"Rust Alkad Menu", WS_OVERLAPPEDWINDOW, 100, 100, 300, 200, NULL, NULL, wcMenu.hInstance, NULL);
    CreateWindow(L"BUTTON", L"ESP", WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 10, 10, 100, 30, hwndMenu, (HMENU)1000, wcMenu.hInstance, NULL);
    CreateWindow(L"BUTTON", L"Dump Offsets", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 10, 50, 100, 30, hwndMenu, (HMENU)1001, wcMenu.hInstance, NULL);
    CreateWindow(L"BUTTON", L"Reload Offsets", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 10, 90, 100, 30, hwndMenu, (HMENU)1002, wcMenu.hInstance, NULL);
    ShowWindow(hwndMenu, SW_HIDE);

    MSG msg;
    clock_t lastRender = 0;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        // Показ/скрытие меню по клавише Insert
        if (GetAsyncKeyState(VK_INSERT) & 1) {
            bShowMenu = !bShowMenu;
            ShowWindow(hwndMenu, bShowMenu ? SW_SHOW : SW_HIDE);
        }

        // Автообновление оффсетов каждые 5 секунд
        static clock_t lastUpdate = 0;
        if (clock() - lastUpdate > 5000) {
            AutoDumpOffsets();
            lastUpdate = clock();
        }

        // Обновление игроков
        UpdatePlayers();

        // Рендеринг с частотой ~60 FPS
        if (clock() - lastRender > 16) { // ~60 FPS
            RenderScene();
            lastRender = clock();
        }

        Sleep(1);
    }

    if (hbmMem) DeleteObject(hbmMem);
    if (hdcMem) DeleteDC(hdcMem);
    if (hdcOverlay) ReleaseDC(hwndOverlay, hdcOverlay);
    if (hFont) DeleteObject(hFont);
    DestroyWindow(hwndOverlay);
    DestroyWindow(hwndMenu);
    return 0;
}

// Entry point
BOOL APIENTRY DllMain(HMODULE hMod, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        hModule = hMod;
        CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
