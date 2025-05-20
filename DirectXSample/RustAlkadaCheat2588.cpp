#include <windows.h>
#include <d3d11.h>
#include "nuklear.h"
#include "nuklear_d3d11.h"
#include <curl/curl.h>
#include <zlib.h>
#include <vector>
#include <cmath>
#include <string>
#include <fstream>

// Глобальные переменные
static IDXGISwapChain* swap_chain = nullptr;
static ID3D11Device* device = nullptr;
static ID3D11DeviceContext* context = nullptr;
static nk_context* nk_ctx = nullptr;

// Смещения для Rust 2588 (Jungle Update), примерные, проверь через Cheat Engine
static uintptr_t g_baseModule = 0x0; // База RustClient.exe
static uintptr_t g_weaponRecoilOffset = 0x1B0; // +0x10 от 2585 (0x1A0)
static uintptr_t g_weaponSpreadOffset = 0x1B4; // +0x10 от 2585 (0x1A4)
static uintptr_t g_weaponFireRateOffset = 0x1B8; // +0x10 от 2585 (0x1A8)
static uintptr_t g_weaponSwapSpeedOffset = 0x1BC; // +0x10 от 2585 (0x1AC)
static uintptr_t g_playerListOffset = 0x5C0; // +0x10 от 2585 (0x5B0)
static uintptr_t g_localPlayerOffset = 0x5D0; // +0x10 от 2585 (0x5C0)
static uintptr_t g_headOffset = 0x130; // Без изменений, проверь
static uintptr_t g_positionOffset = 0x140; // +0x10 от 2585 (0x130)
static uintptr_t g_viewAnglesOffset = 0x150; // +0x10 от 2585 (0x140)

// Структура для игрока
struct Player {
    uintptr_t base;
    float position[3];
    float headPosition[3];
};

// Функции чита
void NoRecoil(uintptr_t weaponBase) {
    if (weaponBase && g_weaponRecoilOffset) {
        float* recoil = (float*)(weaponBase + g_weaponRecoilOffset);
        *recoil = 0.0f; // Отключаем отдачу
    }
}

void NoSpread(uintptr_t weaponBase) {
    if (weaponBase && g_weaponSpreadOffset) {
        float* spread = (float*)(weaponBase + g_weaponSpreadOffset);
        *spread = 0.0f; // Отключаем разброс
    }
}

void RapidFire(uintptr_t weaponBase) {
    if (weaponBase && g_weaponFireRateOffset) {
        float* fireRate = (float*)(weaponBase + g_weaponFireRateOffset);
        *fireRate = 0.01f; // Увеличиваем скорострельность
    }
}

void FastSwap(uintptr_t weaponBase) {
    if (weaponBase && g_weaponSwapSpeedOffset) {
        float* swapSpeed = (float*)(weaponBase + g_weaponSwapSpeedOffset);
        *swapSpeed = 0.0f; // Ускоряем смену оружия
    }
}

// Получение списка игроков
std::vector<Player> GetPlayers() {
    std::vector<Player> players;
    if (!g_baseModule) {
        g_baseModule = (uintptr_t)GetModuleHandle(L"RustClient.exe");
        if (!g_baseModule) return players;
    }

    uintptr_t playerList = *(uintptr_t*)(g_baseModule + g_playerListOffset);
    if (!playerList) return players;

    int playerCount = *(int*)(playerList + 0x8); // Предполагаемое смещение для количества игроков
    uintptr_t playerArray = *(uintptr_t*)(playerList + 0x10); // Предполагаемое смещение для массива игроков

    for (int i = 0; i < playerCount; i++) {
        uintptr_t playerBase = *(uintptr_t*)(playerArray + i * 0x8);
        if (!playerBase) continue;

        float* position = (float*)(playerBase + g_positionOffset);
        float* head = (float*)(playerBase + g_headOffset);

        Player p;
        p.base = playerBase;
        p.position[0] = position[0];
        p.position[1] = position[1];
        p.position[2] = position[2];
        p.headPosition[0] = head[0];
        p.headPosition[1] = head[1];
        p.headPosition[2] = head[2];
        players.push_back(p);
    }
    return players;
}

// WH (ESP)
void DrawESP(nk_context* ctx, const std::vector<Player>& players) {
    for (const auto& player : players) {
        // Упрощённая проекция (нужна реальная WorldToScreen)
        float screenX = player.position[0] * 10.0f + 960.0f;
        float screenY = player.position[1] * 10.0f + 540.0f;

        nk_layout_row_dynamic(ctx, 0, 1);
        nk_label(ctx, "Player", NK_TEXT_CENTERED);
        struct nk_rect box = nk_rect(screenX - 25, screenY - 50, 50, 100);
        nk_fill_rect(nk_window_get_canvas(ctx), box, 0, nk_rgb(255, 0, 0));
    }
}

// Aimbot
void Aimbot(uintptr_t localPlayer, const std::vector<Player>& players) {
    if (!localPlayer || players.empty()) return;

    float* localPos = (float*)(localPlayer + g_positionOffset);
    float* viewAngles = (float*)(localPlayer + g_viewAnglesOffset);

    float closestDist = FLT_MAX;
    float targetAngles[2] = { 0.0f, 0.0f };

    for (const auto& player : players) {
        if (player.base == localPlayer) continue;

        float dx = player.headPosition[0] - localPos[0];
        float dy = player.headPosition[1] - localPos[1];
        float dz = player.headPosition[2] - localPos[2];
        float dist = sqrt(dx * dx + dy * dy + dz * dz);

        if (dist < closestDist) {
            closestDist = dist;
            float yaw = atan2(dy, dx) * 180.0f / 3.14159f;
            float pitch = -atan2(dz, sqrt(dx * dx + dy * dy)) * 180.0f / 3.14159f;
            targetAngles[0] = pitch;
            targetAngles[1] = yaw;
        }
    }

    if (closestDist != FLT_MAX) {
        viewAngles[0] = targetAngles[0]; // Pitch
        viewAngles[1] = targetAngles[1]; // Yaw
    }
}

// Функция для скачивания и обновления чита
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::ofstream* out = (std::ofstream*)userp;
    out->write((char*)contents, totalSize);
    return totalSize;
}

bool DownloadAndUpdate(const char* url, const char* outputPath) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile.is_open()) {
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outFile);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    outFile.close();
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

// Инициализация Nuklear GUI
bool InitGUI() {
    nk_ctx = nk_d3d11_init(&device, &context, swap_chain, 1920, 1080);
    return nk_ctx != nullptr;
}

// Отрисовка GUI
void RenderGUI() {
    nk_d3d11_begin(nk_ctx);

    if (!g_baseModule) {
        g_baseModule = (uintptr_t)GetModuleHandle(L"RustClient.exe");
    }
    uintptr_t localPlayer = *(uintptr_t*)(g_baseModule + g_localPlayerOffset);
    std::vector<Player> players = GetPlayers();

    DrawESP(nk_ctx, players);

    if (nk_begin(nk_ctx, "Rust Cheat", nk_rect(50, 50, 300, 500), NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(nk_ctx, 30, 1);
        if (nk_button_label(nk_ctx, "No Recoil")) {
            NoRecoil(0xDEADBEEF); // Замени на адрес оружия
        }
        if (nk_button_label(nk_ctx, "No Spread")) {
            NoSpread(0xDEADBEEF);
        }
        if (nk_button_label(nk_ctx, "Rapid Fire")) {
            RapidFire(0xDEADBEEF);
        }
        if (nk_button_label(nk_ctx, "Fast Swap")) {
            FastSwap(0xDEADBEEF);
        }
        if (nk_button_label(nk_ctx, "Aimbot")) {
            Aimbot(localPlayer, players);
        }
        if (nk_button_label(nk_ctx, "Update Cheat")) {
            DownloadAndUpdate("http://your-server/update.zip", "update.zip");
        }
    }
    nk_end(nk_ctx);

    nk_d3d11_end();
}

// Перехват Present
typedef HRESULT(WINAPI* Present_t)(IDXGISwapChain*, UINT, UINT);
Present_t oPresent = nullptr;

HRESULT WINAPI hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (!device) {
        pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&device);
        device->GetImmediateContext(&context);
        swap_chain = pSwapChain;
        InitGUI();
    }

    RenderGUI();
    return oPresent(pSwapChain, SyncInterval, Flags);
}

// Инициализация чита
DWORD WINAPI MainThread(LPVOID lpParam) {
    curl_global_init(CURL_GLOBAL_ALL);

    HMODULE dxgi = GetModuleHandle(L"dxgi.dll");
    oPresent = (Present_t)GetProcAddress(dxgi, "Present");

    *(uintptr_t*)&oPresent = (uintptr_t)hkPresent;

    return 0;
}

// Точка входа
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}
