#include <windows.h>
#include <d3d9.h>
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
LPDIRECT3D9 pD3D = NULL;
LPDIRECT3DDEVICE9 pDevice = NULL;
HFONT hFont = NULL;
std::vector<Player> playerList;

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

// Расшифровщик строк (улучшенный)
std::string DecryptString(DWORD64 address) {
    if (!address) return "";
    char encrypted[256];
    SIZE_T bytesRead;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, encrypted, sizeof(encrypted) - 1, &bytesRead)) return "";
    encrypted[bytesRead] = '\0';

    std::string decrypted;
    srand(static_cast<unsigned>(time(0)));
    int key = rand() % 255 + 1; // Динамический ключ
    for (size_t i = 0; i < strlen(encrypted); i++) {
        char c = encrypted[i] ^ key;
        if (c >= 32 && c <= 126) decrypted += c; // Проверка валидности символа
    }
    if (decrypted.empty()) return "Unknown";
    return decrypted;
}

// Поиск сигнатур (AOB-сканирование с несколькими вариантами)
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

        // Несколько сигнатур для разных режимов (оффлайн/онлайн)
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

// Проекция 3D->2D (упрощённая, нуждается в ViewMatrix)
bool WorldToScreen(float x, float y, float z, float& screenX, float& screenY) {
    screenX = x;
    screenY = y;
    return true;
}

// Рендеринг текста через DirectX
void DrawText(const char* text, float x, float y, DWORD color) {
    if (!pDevice) return;
    LPD3DXFONT pFont;
    D3DXCreateFont(pDevice, 16, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial", &pFont);
    RECT rect = { (LONG)x, (LONG)y, (LONG)(x + 200), (LONG)(y + 20) };
    pFont->DrawTextA(NULL, text, -1, &rect, DT_LEFT | DT_NOCLIP, color);
    pFont->Release();
}

// Рендеринг бокса через DirectX
void DrawBox(float x, float y, float w, float h, DWORD color) {
    struct Vertex { float x, y, z, rhw; DWORD color; };
    Vertex vertices[] = {
        { x, y, 0, 1, color },
        { x + w, y, 0, 1, color },
        { x, y + h, 0, 1, color },
        { x + w, y + h, 0, 1, color }
    };
    pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    pDevice->DrawPrimitiveUP(D3DPT_LINESTRIP, 4, vertices, sizeof(Vertex));
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
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Инициализация DirectX
bool InitD3D(HWND hWnd) {
    pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return false;

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hWnd;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice);
    if (FAILED(hr)) return false;

    hFont = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Arial");
    return true;
}

// Основной поток
DWORD WINAPI MainThread(LPVOID lpParam) {
    // Создаём оверлей
    WNDCLASSEX wcOverlay = { sizeof(WNDCLASSEX), CS_CLASSDC, OverlayWndProc, 0, 0, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"Overlay", NULL };
    RegisterClassEx(&wcOverlay);
    hwndOverlay = CreateWindow(wcOverlay.lpszClassName, L"Rust Alkad Overlay", WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, wcOverlay.hInstance, NULL);
    SetWindowLong(hwndOverlay, GWL_EXSTYLE, GetWindowLong(hwndOverlay, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    SetLayeredWindowAttributes(hwndOverlay, 0, 255, LWA_ALPHA);
    ShowWindow(hwndOverlay, SW_SHOW);
    UpdateWindow(hwndOverlay);

    // Инициализация DirectX
    if (!InitD3D(hwndOverlay)) return 0;

    // Создаём меню
    WNDCLASSEX wcMenu = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, MenuWndProc, 0, 0, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"Menu", NULL };
    RegisterClassEx(&wcMenu);
    hwndMenu = CreateWindow(wcMenu.lpszClassName, L"Rust Alkad Menu", WS_OVERLAPPEDWINDOW, 100, 100, 300, 200, NULL, NULL, wcMenu.hInstance, NULL);
    CreateWindow(L"BUTTON", L"ESP", WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 10, 10, 100, 30, hwndMenu, (HMENU)1000, wcMenu.hInstance, NULL);
    CreateWindow(L"BUTTON", L"Dump Offsets", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 10, 50, 100, 30, hwndMenu, (HMENU)1001, wcMenu.hInstance, NULL);
    CreateWindow(L"BUTTON", L"Reload Offsets", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 10, 90, 100, 30, hwndMenu, (HMENU)1002, wcMenu.hInstance, NULL);

    MSG msg;
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

        // Рендеринг
        if (pDevice) {
            pDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
            pDevice->BeginScene();

            if (bShowESP) {
                for (const auto& player : playerList) {
                    float screenX, screenY;
                    if (WorldToScreen(player.x, player.y, player.z, screenX, screenY)) {
                        char label[64];
                        sprintf(label, "%s [HP: %d]", player.name.c_str(), player.health);
                        DrawText(label, screenX, screenY - 20, player.isVisible ? D3DCOLOR_ARGB(255, 0, 255, 0) : D3DCOLOR_ARGB(255, 255, 0, 0));
                        DrawBox(screenX - 25, screenY - 50, 50, 100, player.isVisible ? D3DCOLOR_ARGB(255, 0, 255, 0) : D3DCOLOR_ARGB(255, 255, 0, 0));
                    }
                }
            }

            pDevice->EndScene();
            pDevice->Present(NULL, NULL, NULL, NULL);
        }

        Sleep(10);
    }

    if (pDevice) pDevice->Release();
    if (pD3D) pD3D->Release();
    if (hFont) DeleteObject(hFont);
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
