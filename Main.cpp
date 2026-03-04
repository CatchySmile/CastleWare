#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <d3d9.h>
#include <tchar.h>
#include <windows.h>
#include <winuser.h>
#include <tlhelp32.h>
#include <string>
#include <cstdint>
#include <wingdi.h>
#include "offsets.h"
#include <fstream>
#include <vector>
#include <shlobj.h>
#include <shellapi.h>
#include "json.hpp"
#include <chrono>
using json = nlohmann::json;
using namespace std::chrono;

// Constants for hack values
static const uint32_t VALUE_DEFAULT_PLAYERSIZE = 1065353216u;  // 1.0f
static const uint32_t VALUE_TINY_PLAYERSIZE = 985353216u;   // ~0.001f for noclip
static const uint32_t VALUE_NOCOLLISION_PLAYERSIZE = 0u;
static const uint32_t VALUE_INFINITE_JUMP = 6400u;

// Globals
static LPDIRECT3D9 g_pD3D = nullptr;
static LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
static bool g_DeviceLost = false;
static UINT g_ResizeWidth = 0;
static UINT g_ResizeHeight = 0;
static D3DPRESENT_PARAMETERS g_d3dpp = {};

// Process
static HANDLE hProcess = nullptr;
static DWORD targetPid = 0;
static uintptr_t moduleBase = 0;
static uintptr_t pointerP = 0;

// Hacks
static bool noclip = false;
static bool nocollision = false;
static bool infiniteJump = false;
static bool show_ui = true;

// Key bindings
static int noclipKey = 0;
static bool noclipCtrl = false, noclipShift = false, noclipAlt = false;

static int nocollisionKey = 0;
static bool nocollisionCtrl = false, nocollisionShift = false, nocollisionAlt = false;

static int infiniteJumpKey = 0;
static bool infiniteJumpCtrl = false, infiniteJumpShift = false, infiniteJumpAlt = false;

static int toggleUIKey = VK_INSERT;
static bool toggleUICtrl = false, toggleUIShift = false, toggleUIAlt = false;

static int listeningBind = 0; // 0:none, 1:noclip, 2:nocollision, 3:infiniteJump, 4:toggleUI
static bool prev_noclipKeyPressed = false;
static bool prev_nocollisionKeyPressed = false;
static bool prev_infiniteJumpKeyPressed = false;
static bool prev_toggleUIKeyPressed = false;

// Account manager
struct User { std::string username, password; };
static std::vector<User> g_users;
static int g_selectedUserIndex = -1;
static std::string g_status = "Ready";

// Binding popup state
static bool show_bind_popup = false;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

DWORD GetProcessIdByName(const std::wstring& name);
uintptr_t GetModuleBaseAddress(DWORD pid, const std::wstring& moduleName);
bool AttachToTarget();
void DetachTarget();
void EnsureAttached();
bool ReadUint32FromOffset(uintptr_t base, uintptr_t off, uint32_t& out);
bool WriteUint32ToOffset(uintptr_t base, uintptr_t off, uint32_t value);

std::string GetKeyNameWithMods(int key, bool ctrl, bool shift, bool alt);
void HandleKeyBindings();

std::string GetAppDataPath(const std::string& filename);
std::string GetUserDataFile();
std::string GetConnectionCfgFile();
User ReadUserFromConnectionCfg();
std::vector<User> LoadUsers();
void SaveUsers(const std::vector<User>& users);
bool CreateUser(const User& user);
void WriteConnectionCfg(const User& user);
bool IsProcessRunning(const std::wstring& name);
void CloseProcessByName(const std::wstring& name);
void LaunchCubic();
void SignInUser(const User& user);
void ReloadUsers();
void EnsureAccountsJsonInitialized();

std::string GetConfigFile();
void LoadConfig();
void SaveConfig();

void RenderUI();
void ApplyHacks();

int main(int, char**) {
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        ::MonitorFromPoint(POINT{ 0, 0 }, 0x00000001 /* MONITOR_DEFAULTTOPRIMARY */)
    );
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"CastleWare", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST, wc.lpszClassName, L"CastleWare",
        WS_POPUP | WS_VISIBLE, 0, 0, screen_width, screen_height,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui::SetNextWindowSize(ImVec2(284, 349), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(320 * main_scale, 330 * main_scale), ImGuiCond_FirstUseEver);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.03f, 0.03f, 0.9f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    EnsureAccountsJsonInitialized();
    ReloadUsers();
    LoadConfig();

    bool done = false;
    while (!done) {
        auto frame_start = steady_clock::now();

        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_DeviceLost) {
            HRESULT hr = g_pd3dDevice->TestCooperativeLevel();
            if (hr == D3DERR_DEVICELOST) { ::Sleep(10); continue; }
            if (hr == D3DERR_DEVICENOTRESET) ResetDevice();
            g_DeviceLost = false;
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        EnsureAttached();
        HandleKeyBindings();

        if (show_ui) {
            RenderUI();
        }

        ApplyHacks();

        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_RGBA(0, 0, 0, 0), 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST) g_DeviceLost = true;

        auto frame_end = steady_clock::now();
        auto frame_duration = duration_cast<milliseconds>(frame_end - frame_start);
        int sleep_ms = (1000 / 60) - static_cast<int>(frame_duration.count());
        if (sleep_ms > 0) ::Sleep(sleep_ms);
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (hProcess) CloseHandle(hProcess);
    return 0;
}

// ────────────────────────────────────────────────
// DirectX / Window
// ────────────────────────────────────────────────

bool CreateDeviceD3D(HWND hWnd) {
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr) return false;
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;
    return true;
}

void CleanupDeviceD3D() {
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}

void ResetDevice() {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL) IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

extern IMGUI_IMPL_API LRESULT WINAPI ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ────────────────────────────────────────────────
// Process / Memory
// ────────────────────────────────────────────────

DWORD GetProcessIdByName(const std::wstring& name) {
    DWORD pid = 0;
    PROCESSENTRY32W entry = { sizeof(entry) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (name == entry.szExeFile) { pid = entry.th32ProcessID; break; }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return pid;
}

uintptr_t GetModuleBaseAddress(DWORD pid, const std::wstring& moduleName) {
    uintptr_t base = 0;
    MODULEENTRY32W me = { sizeof(me) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    if (Module32FirstW(snap, &me)) {
        do {
            if (moduleName == me.szModule) { base = (uintptr_t)me.modBaseAddr; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return base;
}

bool AttachToTarget() {
    if (hProcess) return true;
    targetPid = GetProcessIdByName(targetProcessName);
    if (targetPid == 0) return false;
    hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, targetPid);
    if (!hProcess) { targetPid = 0; return false; }
    moduleBase = GetModuleBaseAddress(targetPid, targetProcessName);
    if (!moduleBase) {
        CloseHandle(hProcess); hProcess = nullptr; targetPid = 0; return false;
    }
    uint32_t p32 = 0;
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(hProcess, (LPCVOID)(moduleBase + offsetGameToBaseAddress), &p32, sizeof(p32), &bytesRead) || bytesRead != sizeof(p32)) {
        CloseHandle(hProcess); hProcess = nullptr; targetPid = moduleBase = 0; return false;
    }
    pointerP = p32;
    return true;
}

void DetachTarget() {
    if (hProcess) { CloseHandle(hProcess); hProcess = nullptr; }
    targetPid = moduleBase = pointerP = 0;
}

void EnsureAttached() {
    if (hProcess) {
        DWORD exitCode = 0;
        if (!GetExitCodeProcess(hProcess, &exitCode) || exitCode != STILL_ACTIVE) { DetachTarget(); return; }
        uintptr_t mb = GetModuleBaseAddress(targetPid, targetProcessName);
        if (mb == 0) { DetachTarget(); return; }
        if (mb != moduleBase) moduleBase = mb;
        uint32_t p32 = 0; SIZE_T br = 0;
        if (ReadProcessMemory(hProcess, (LPCVOID)(moduleBase + offsetGameToBaseAddress), &p32, sizeof(p32), &br) && br == sizeof(p32)) {
            pointerP = p32;
        }
        else {
            DetachTarget();
        }
    }
    else {
        AttachToTarget();
    }
}

bool ReadUint32FromOffset(uintptr_t base, uintptr_t off, uint32_t& out) {
    if (!hProcess || base == 0) return false;
    SIZE_T br = 0;
    return ReadProcessMemory(hProcess, (LPCVOID)(base + off), &out, sizeof(out), &br) && br == sizeof(out);
}

bool WriteUint32ToOffset(uintptr_t base, uintptr_t off, uint32_t value) {
    if (!hProcess || base == 0) return false;
    SIZE_T bw = 0;
    return WriteProcessMemory(hProcess, (LPVOID)(base + off), &value, sizeof(value), &bw) && bw == sizeof(value);
}

// ────────────────────────────────────────────────
// Key handling
// ────────────────────────────────────────────────

std::string GetKeyNameWithMods(int key, bool ctrl, bool shift, bool alt) {
    if (key == 0) return "None";
    std::string s;
    if (ctrl) s += "Ctrl+";
    if (shift) s += "Shift+";
    if (alt) s += "Alt+";
    if (key >= 'A' && key <= 'Z') { s += char(key); return s; }
    if (key >= '0' && key <= '9') { s += char(key); return s; }
    switch (key) {
    case VK_F1: s += "F1"; break;
    case VK_F2: s += "F2"; break;
    case VK_F3: s += "F3"; break;
    case VK_F4: s += "F4"; break;
    case VK_F5: s += "F5"; break;
    case VK_F6: s += "F6"; break;
    case VK_F7: s += "F7"; break;
    case VK_F8: s += "F8"; break;
    case VK_F9: s += "F9"; break;
    case VK_F10: s += "F10"; break;
    case VK_F11: s += "F11"; break;
    case VK_F12: s += "F12"; break;
    case VK_SPACE: s += "Space"; break;
    case VK_DELETE: s += "Delete"; break;
    case VK_INSERT: s += "Insert"; break;
    default: s += "Key"; break;
    }
    return s;
}

void HandleKeyBindings() {
    // Handle binding input
    if (listeningBind > 0) {
        for (int key = 0x08; key <= 0xFF; ++key) {
            if (GetAsyncKeyState(key) & 1) { // key pressed this frame
                if (key == VK_ESCAPE) {
                    listeningBind = 0;
                    return;
                }

                bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

                if (listeningBind == 1) {
                    noclipKey = key; noclipCtrl = ctrl; noclipShift = shift; noclipAlt = alt;
                }
                else if (listeningBind == 2) {
                    nocollisionKey = key; nocollisionCtrl = ctrl; nocollisionShift = shift; nocollisionAlt = alt;
                }
                else if (listeningBind == 3) {
                    infiniteJumpKey = key; infiniteJumpCtrl = ctrl; infiniteJumpShift = shift; infiniteJumpAlt = alt;
                }
                else if (listeningBind == 4) {
                    toggleUIKey = key; toggleUICtrl = ctrl; toggleUIShift = shift; toggleUIAlt = alt;
                }

                listeningBind = 0;
                ImGui::CloseCurrentPopup();
                break;
            }
        }
    }

    // Hotkey toggles
    bool noclipPressed = (noclipKey != 0) &&
        (GetAsyncKeyState(noclipKey) & 0x8000) &&
        (noclipCtrl == ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)) &&
        (noclipShift == ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)) &&
        (noclipAlt == ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0));
    if (noclipPressed && !prev_noclipKeyPressed) noclip = !noclip;
    prev_noclipKeyPressed = noclipPressed;

    bool nocollisionPressed = (nocollisionKey != 0) &&
        (GetAsyncKeyState(nocollisionKey) & 0x8000) &&
        (nocollisionCtrl == ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)) &&
        (nocollisionShift == ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)) &&
        (nocollisionAlt == ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0));
    if (nocollisionPressed && !prev_nocollisionKeyPressed) nocollision = !nocollision;
    prev_nocollisionKeyPressed = nocollisionPressed;

    bool infiniteJumpPressed = (infiniteJumpKey != 0) &&
        (GetAsyncKeyState(infiniteJumpKey) & 0x8000) &&
        (infiniteJumpCtrl == ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)) &&
        (infiniteJumpShift == ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)) &&
        (infiniteJumpAlt == ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0));
    if (infiniteJumpPressed && !prev_infiniteJumpKeyPressed) infiniteJump = !infiniteJump;
    prev_infiniteJumpKeyPressed = infiniteJumpPressed;

    bool toggleUIPressed = (toggleUIKey != 0) &&
        (GetAsyncKeyState(toggleUIKey) & 0x8000) &&
        (toggleUICtrl == ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)) &&
        (toggleUIShift == ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)) &&
        (toggleUIAlt == ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0));
    if (toggleUIPressed && !prev_toggleUIKeyPressed) show_ui = !show_ui;
    prev_toggleUIKeyPressed = toggleUIPressed;
}

// ────────────────────────────────────────────────
// Account / Config functions 
// ────────────────────────────────────────────────

std::string GetAppDataPath(const std::string& filename) {
    char path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        std::string dir = std::string(path) + "\\Cubic";
        CreateDirectoryA(dir.c_str(), NULL);
        return dir + "\\" + filename;
    }
    return "";
}

std::string GetUserDataFile() { return GetAppDataPath("accounts.json"); }
std::string GetConnectionCfgFile() { return GetAppDataPath("Connection.cfg"); }
std::string GetConfigFile() { return GetAppDataPath("config.json"); }

User ReadUserFromConnectionCfg() {
    User user;
    std::ifstream file(GetConnectionCfgFile());
    if (!file) return user;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("Game.Username=") == 0) user.username = line.substr(14);
        else if (line.find("Game.Password=") == 0) user.password = line.substr(14);
    }
    return user;
}

std::vector<User> LoadUsers() {
    std::vector<User> users;
    std::ifstream file(GetUserDataFile());
    if (!file) return users;
    try {
        json j; file >> j;
        if (j.is_array()) {
            for (const auto& item : j) {
                if (item.is_object()) {
                    std::string u = item.value("username", "");
                    std::string p = item.value("password", "");
                    // Skip invalid entries during load too
                    u.erase(0, u.find_first_not_of(" \t\r\n"));
                    u.erase(u.find_last_not_of(" \t\r\n") + 1);
                    if (!u.empty()) {
                        users.push_back({ u, p });
                    }
                }
            }
        }
    }
    catch (...) {}
    return users;
}

void SaveUsers(const std::vector<User>& users) {
    json j = json::array();
    for (const auto& u : users) {
        // Only save if username is still valid
        std::string clean = u.username;
        clean.erase(0, clean.find_first_not_of(" \t\r\n"));
        clean.erase(clean.find_last_not_of(" \t\r\n") + 1);
        if (!clean.empty()) {
            j.push_back({ {"username", u.username}, {"password", u.password} });
        }
    }
    std::ofstream file(GetUserDataFile(), std::ios::trunc);
    if (file) file << j.dump(4);
}

bool CreateUser(const User& user) {
    std::string u = user.username;
    std::string p = user.password;

    // Trim whitespace
    u.erase(0, u.find_first_not_of(" \t\r\n"));
    u.erase(u.find_last_not_of(" \t\r\n") + 1);
    p.erase(0, p.find_first_not_of(" \t\r\n"));
    p.erase(p.find_last_not_of(" \t\r\n") + 1);

    // Reject if username is empty after trimming
    if (u.empty() || p.empty()) return false;

    auto users = LoadUsers();
    for (const auto& existing : users) {
        if (existing.username == u) return false; // duplicate
    }

    users.push_back({ u, p });
    SaveUsers(users);
    return true;
}

void WriteConnectionCfg(const User& user) {
    std::ofstream file(GetConnectionCfgFile(), std::ios::trunc);
    if (!file) return;
    file << "Game.RememberMe=true\n";
    file << "Game.Username=" << user.username << "\n";
    file << "Game.Password=" << user.password << "\n";
}

bool IsProcessRunning(const std::wstring& name) {
    PROCESSENTRY32W pe32{ sizeof(pe32) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    if (Process32FirstW(snap, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, name.c_str()) == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe32));
    }
    CloseHandle(snap);
    return found;
}

void CloseProcessByName(const std::wstring& name) {
    PROCESSENTRY32W pe32{ sizeof(pe32) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    if (Process32FirstW(snap, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, name.c_str()) == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); }
            }
        } while (Process32NextW(snap, &pe32));
    }
    CloseHandle(snap);
}

void LaunchCubic() {
    ShellExecuteW(NULL, L"open", L"steam://rungameid/317470", NULL, NULL, SW_SHOWNORMAL);
}

void SignInUser(const User& user) {
    if (IsProcessRunning(L"Cubic.exe")) {
        CloseProcessByName(L"Cubic.exe");
        Sleep(2800);
    }
    WriteConnectionCfg(user);
    LaunchCubic();
}

void ReloadUsers() {
    g_users = LoadUsers();
    if (g_selectedUserIndex >= (int)g_users.size()) g_selectedUserIndex = -1;
}

void EnsureAccountsJsonInitialized() {
    std::string jsonPath = GetUserDataFile();
    std::ifstream jsonFile(jsonPath);
    if (jsonFile.good()) {
        // File exists → just make sure loaded users are clean
        ReloadUsers();
        return;
    }

    // No json yet → migrate from old Connection.cfg if possible
    User old = ReadUserFromConnectionCfg();
    if (!old.username.empty() && !old.password.empty()) {
        std::string u = old.username;
        u.erase(0, u.find_first_not_of(" \t\r\n"));
        u.erase(u.find_last_not_of(" \t\r\n") + 1);

        if (!u.empty()) {
            std::vector<User> users = { {u, old.password} };
            SaveUsers(users);
            ReloadUsers();
        }
    }
}

void LoadConfig() {
    std::ifstream file(GetConfigFile());
    if (!file) return;
    try {
        json j; file >> j;
        noclip = j.value("noclip", false);
        nocollision = j.value("nocollision", false);
        infiniteJump = j.value("infiniteJump", false);

        noclipKey = j.value("noclipKey", 0);
        noclipCtrl = j.value("noclipCtrl", false);
        noclipShift = j.value("noclipShift", false);
        noclipAlt = j.value("noclipAlt", false);

        nocollisionKey = j.value("nocollisionKey", 0);
        nocollisionCtrl = j.value("nocollisionCtrl", false);
        nocollisionShift = j.value("nocollisionShift", false);
        nocollisionAlt = j.value("nocollisionAlt", false);

        infiniteJumpKey = j.value("infiniteJumpKey", 0);
        infiniteJumpCtrl = j.value("infiniteJumpCtrl", false);
        infiniteJumpShift = j.value("infiniteJumpShift", false);
        infiniteJumpAlt = j.value("infiniteJumpAlt", false);

        toggleUIKey = j.value("toggleUIKey", (int)VK_INSERT);
        toggleUICtrl = j.value("toggleUICtrl", false);
        toggleUIShift = j.value("toggleUIShift", false);
        toggleUIAlt = j.value("toggleUIAlt", false);
    }
    catch (...) {}
}

void SaveConfig() {
    json j;
    j["noclip"] = noclip;
    j["nocollision"] = nocollision;
    j["infiniteJump"] = infiniteJump;

    j["noclipKey"] = noclipKey;
    j["noclipCtrl"] = noclipCtrl;
    j["noclipShift"] = noclipShift;
    j["noclipAlt"] = noclipAlt;

    j["nocollisionKey"] = nocollisionKey;
    j["nocollisionCtrl"] = nocollisionCtrl;
    j["nocollisionShift"] = nocollisionShift;
    j["nocollisionAlt"] = nocollisionAlt;

    j["infiniteJumpKey"] = infiniteJumpKey;
    j["infiniteJumpCtrl"] = infiniteJumpCtrl;
    j["infiniteJumpShift"] = infiniteJumpShift;
    j["infiniteJumpAlt"] = infiniteJumpAlt;

    j["toggleUIKey"] = toggleUIKey;
    j["toggleUICtrl"] = toggleUICtrl;
    j["toggleUIShift"] = toggleUIShift;
    j["toggleUIAlt"] = toggleUIAlt;

    std::ofstream file(GetConfigFile(), std::ios::trunc);
    if (file) file << j.dump(4);
}

// ────────────────────────────────────────────────
// UI & Hacks
// ────────────────────────────────────────────────

void RenderUI() {
    ImGui::Begin("CastleWare");
    if (ImGui::BeginTabBar("Tabs")) {
        if (ImGui::BeginTabItem("Main")) {
            ImGui::Checkbox("Noclip", &noclip);
            ImGui::SameLine();
            if (ImGui::Button("Bind##noclip")) {
                listeningBind = 1;
                ImGui::OpenPopup("Bind Key");
            }
            ImGui::Text("Bind: %s", GetKeyNameWithMods(noclipKey, noclipCtrl, noclipShift, noclipAlt).c_str());

            ImGui::Checkbox("No Collision", &nocollision);
            ImGui::SameLine();
            if (ImGui::Button("Bind##nocollision")) {
                listeningBind = 2;
                ImGui::OpenPopup("Bind Key");
            }
            ImGui::Text("Bind: %s", GetKeyNameWithMods(nocollisionKey, nocollisionCtrl, nocollisionShift, nocollisionAlt).c_str());

            ImGui::Checkbox("Infinite Jump", &infiniteJump);
            ImGui::SameLine();
            if (ImGui::Button("Bind##infinitejump")) {
                listeningBind = 3;
                ImGui::OpenPopup("Bind Key");
            }
            ImGui::Text("Bind: %s", GetKeyNameWithMods(infiniteJumpKey, infiniteJumpCtrl, infiniteJumpShift, infiniteJumpAlt).c_str());

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Account Manager")) {
            static char bufU[128] = {};
            static char bufP[128] = {};

            if (ImGui::BeginListBox("Accounts", ImVec2(170, 170))) {
                for (size_t i = 0; i < g_users.size(); ++i) {
                    bool selected = (g_selectedUserIndex == (int)i);

                    // Defensive: never pass empty string to Selectable
                    std::string display = g_users[i].username;
                    if (display.empty()) display = "<no name>";
                    const char* label = display.c_str();

                    if (ImGui::Selectable(label, selected)) {
                        g_selectedUserIndex = (int)i;
                        strcpy_s(bufU, g_users[i].username.c_str());
                        strcpy_s(bufP, g_users[i].password.c_str());
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }

            ImGui::InputText("Username", bufU, sizeof(bufU));
            ImGui::InputText("Password", bufP, sizeof(bufP), ImGuiInputTextFlags_Password);

            if (ImGui::Button("Add Account")) {
                std::string u = bufU, p = bufP;
                u.erase(0, u.find_first_not_of(" \t\r\n"));
                u.erase(u.find_last_not_of(" \t\r\n") + 1);
                p.erase(0, p.find_first_not_of(" \t\r\n"));
                p.erase(p.find_last_not_of(" \t\r\n") + 1);
                if (!u.empty() && !p.empty()) {
                    User nu{ u, p };
                    if (CreateUser(nu)) {
                        ReloadUsers();
                        g_status = "Account added.";
                        bufU[0] = bufP[0] = '\0';
                    }
                    else {
                        g_status = "Failed – duplicate or invalid.";
                    }
                }
                else {
                    g_status = "Fields cannot be empty.";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                if (g_selectedUserIndex >= 0 && g_selectedUserIndex < (int)g_users.size()) {
                    g_users.erase(g_users.begin() + g_selectedUserIndex);
                    SaveUsers(g_users);
                    ReloadUsers();
                    bufU[0] = bufP[0] = '\0';
                    g_status = "Account deleted.";
                }
                else {
                    g_status = "No account selected.";
                }
            }

            if (ImGui::Button("Login with Selected")) {
                if (g_selectedUserIndex >= 0 && g_selectedUserIndex < (int)g_users.size()) {
                    SignInUser(g_users[g_selectedUserIndex]);
                    g_status = "Login sequence sent.";
                }
                else {
                    g_status = "Select an account first.";
                }
            }

            ImGui::Text("%s", g_status.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Config")) {
            ImGui::Text("UI Toggle Bind:");
            ImGui::SameLine();
            if (ImGui::Button("Bind##toggleui")) {
                listeningBind = 4;
                ImGui::OpenPopup("Bind Key");
            }
            ImGui::Text("Bind: %s", GetKeyNameWithMods(toggleUIKey, toggleUICtrl, toggleUIShift, toggleUIAlt).c_str());

            ImGui::Separator();

            if (ImGui::Button("Save Config")) {
                SaveConfig();
            }

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();

    // Binding popup
    if (ImGui::BeginPopupModal("Bind Key", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Waiting for key combo...");
        ImGui::Text("(Press ESC to cancel)");

        ImGui::Separator();

        const char* feature = "Unknown";
        int* targetKey = nullptr;
        bool* targetCtrl = nullptr, * targetShift = nullptr, * targetAlt = nullptr;

        if (listeningBind == 1) {
            feature = "Noclip";
            targetKey = &noclipKey;
            targetCtrl = &noclipCtrl; targetShift = &noclipShift; targetAlt = &noclipAlt;
        }
        else if (listeningBind == 2) {
            feature = "No Collision";
            targetKey = &nocollisionKey;
            targetCtrl = &nocollisionCtrl; targetShift = &nocollisionShift; targetAlt = &nocollisionAlt;
        }
        else if (listeningBind == 3) {
            feature = "Infinite Jump";
            targetKey = &infiniteJumpKey;
            targetCtrl = &infiniteJumpCtrl; targetShift = &infiniteJumpShift; targetAlt = &infiniteJumpAlt;
        }
        else if (listeningBind == 4) {
            feature = "Toggle UI";
            targetKey = &toggleUIKey;
            targetCtrl = &toggleUICtrl; targetShift = &toggleUIShift; targetAlt = &toggleUIAlt;
        }

        ImGui::Text("Binding for: %s", feature);

        if (ImGui::Button("Unbind")) {
            if (targetKey) *targetKey = 0;
            if (targetCtrl) *targetCtrl = false;
            if (targetShift) *targetShift = false;
            if (targetAlt) *targetAlt = false;
            listeningBind = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel")) {
            listeningBind = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ApplyHacks() {
    if (hProcess && pointerP != 0) {
        uint32_t targetSize = VALUE_DEFAULT_PLAYERSIZE;
        if (nocollision) {
            targetSize = VALUE_NOCOLLISION_PLAYERSIZE;
        }
        else if (noclip) {
            targetSize = VALUE_TINY_PLAYERSIZE;
        }
        WriteUint32ToOffset(pointerP, playerSizeOffset, targetSize);

        if (infiniteJump) {
            WriteUint32ToOffset(pointerP, jumpPotentialOffset, VALUE_INFINITE_JUMP);
        }
    }
}
