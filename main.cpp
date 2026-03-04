#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <d3d9.h>
#include <tchar.h>
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <shlobj.h>
#include <shellapi.h>
#include <fstream>
#include "json.hpp"
#include "offsets.h"

using json = nlohmann::json;

// ────────────────────────────────────────────────
// Constants & Globals
// ────────────────────────────────────────────────

static const uint32_t PSZ_DEFAULT = 1065353216u; // 1.0f
static const uint32_t PSZ_TINY = 985353216u; // ~0.001f
static const uint32_t PSZ_NOCOLL = 0u;
static const uint32_t JUMP_INF = 6400u;

static LPDIRECT3D9        g_d3d = nullptr;
static LPDIRECT3DDEVICE9  g_device = nullptr;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
static bool g_deviceLost = false;

static HANDLE     hProc = nullptr;
static DWORD      pid = 0;
static uintptr_t  modBase = 0;
static uintptr_t  playerPtr = 0;

static bool noclip = false;
static bool noCollision = false;
static bool infJump = false;
static bool showMenu = true;

struct KeyBind {
    int  key = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

static KeyBind bindNoclip, bindNoColl, bindInfJump, bindToggleUI{ VK_INSERT };

static int listeningFor = 0; // 0=none, 1=noclip, 2=nocoll, 3=infjump, 4=toggleUI

struct Account {
    std::string user, pass;
};
static std::vector<Account> accounts;
static int selectedAcc = -1;
static std::string statusMsg = "Ready";

// ────────────────────────────────────────────────
// Forward declarations
// ────────────────────────────────────────────────

bool  CreateD3D(HWND hwnd);
void  CleanupD3D();
void  ResetD3D();
LRESULT WINAPI WndProc(HWND, UINT, WPARAM, LPARAM);

DWORD GetProcId(const std::wstring& name);
uintptr_t GetModuleBase(DWORD pid, const std::wstring& modName);
bool Attach();
void Detach();
void TryAttachOrRefresh();

bool ReadU32(uintptr_t base, uintptr_t off, uint32_t& v);
bool WriteU32(uintptr_t base, uintptr_t off, uint32_t v);

std::string KeyToString(const KeyBind& b);

void HandleHotkeys();
std::string AppDataPath(const std::string& fn);
void LoadAccounts();
void SaveAccounts();
bool AddAccount(const std::string& u, const std::string& p);
void WriteConnectionCfg(const Account& acc);
void LaunchGame();
void LoginSelected();

void LoadSettings();
void SaveSettings();

void RenderInterface();
void ApplyCheats();

// ────────────────────────────────────────────────
// Entry point
// ────────────────────────────────────────────────

int main(int, char**) {
    ImGui_ImplWin32_EnableDpiAwareness();
    float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint({ 0,0 }, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CastleWare";
    ::RegisterClassExW(&wc);

    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST, wc.lpszClassName, L"CastleWare",
        WS_POPUP | WS_VISIBLE, 0, 0, sx, sy, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateD3D(hwnd)) {
        CleanupD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 6.f;
    style.FrameRounding = 4.f;
    style.Colors[ImGuiCol_WindowBg] = { 0.0f, 0.03f, 0.03f, 0.90f };
    style.Colors[ImGuiCol_ModalWindowDimBg] = { 0,0,0,0 };

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_device);

    LoadAccounts();
    LoadSettings();

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_deviceLost) {
            if (g_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) ResetD3D();
            g_deviceLost = false;
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        TryAttachOrRefresh();
        HandleHotkeys();

        if (showMenu) RenderInterface();

        ApplyCheats();

        ImGui::EndFrame();

        g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        g_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_RGBA(0, 0, 0, 0), 1.f, 0);

        if (g_device->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_device->EndScene();
        }

        if (g_device->Present(nullptr, nullptr, nullptr, nullptr) == D3DERR_DEVICELOST)
            g_deviceLost = true;

        ::Sleep(6); // ~144–165 fps cap light
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (hProc) CloseHandle(hProc);
    return 0;
}

// ────────────────────────────────────────────────
// DirectX / Window
// ────────────────────────────────────────────────

bool CreateD3D(HWND hwnd) {
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) return false;

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    return SUCCEEDED(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_device));
}

void CleanupD3D() {
    if (g_device) { g_device->Release(); g_device = nullptr; }
    if (g_d3d) { g_d3d->Release();    g_d3d = nullptr; }
}

void ResetD3D() {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    g_device->Reset(&g_d3dpp);
    ImGui_ImplDX9_CreateDeviceObjects();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            g_d3dpp.BackBufferWidth = LOWORD(lp);
            g_d3dpp.BackBufferHeight = HIWORD(lp);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// ────────────────────────────────────────────────
// Process / Memory
// ────────────────────────────────────────────────

DWORD GetProcId(const std::wstring& name) {
    DWORD pid = 0;
    PROCESSENTRY32W pe{ sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (name == pe.szExeFile) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

uintptr_t GetModuleBase(DWORD pid, const std::wstring& mod) {
    MODULEENTRY32W me{ sizeof(me) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            if (mod == me.szModule) { base = (uintptr_t)me.modBaseAddr; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return base;
}

bool Attach() {
    if (hProc) return true;
    pid = GetProcId(targetProcessName);
    if (!pid) return false;
    hProc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc) return false;
    modBase = GetModuleBase(pid, targetProcessName);
    if (!modBase) { CloseHandle(hProc); hProc = nullptr; return false; }

    uint32_t ptr = 0;
    if (!ReadProcessMemory(hProc, (LPCVOID)(modBase + offsetGameToBaseAddress), &ptr, 4, nullptr)) {
        CloseHandle(hProc); hProc = nullptr; return false;
    }
    playerPtr = ptr;
    return true;
}

void Detach() {
    if (hProc) CloseHandle(hProc);
    hProc = nullptr; pid = 0; modBase = 0; playerPtr = 0;
}

void TryAttachOrRefresh() {
    if (hProc) {
        DWORD ec; if (!GetExitCodeProcess(hProc, &ec) || ec != STILL_ACTIVE) { Detach(); return; }
        uintptr_t mb = GetModuleBase(pid, targetProcessName);
        if (!mb) { Detach(); return; }
        if (mb != modBase) modBase = mb;

        uint32_t p = 0;
        if (ReadProcessMemory(hProc, (LPCVOID)(modBase + offsetGameToBaseAddress), &p, 4, nullptr))
            playerPtr = p;
        else
            Detach();
    }
    else {
        Attach();
    }
}

bool ReadU32(uintptr_t base, uintptr_t off, uint32_t& v) {
    if (!hProc || !base) return false;
    return ReadProcessMemory(hProc, (LPCVOID)(base + off), &v, 4, nullptr);
}

bool WriteU32(uintptr_t base, uintptr_t off, uint32_t v) {
    if (!hProc || !base) return false;
    return WriteProcessMemory(hProc, (LPVOID)(base + off), &v, 4, nullptr);
}

// ────────────────────────────────────────────────
// Key binding
// ────────────────────────────────────────────────

std::string KeyToString(const KeyBind& b) {
    if (!b.key) return "None";

    std::string s;
    if (b.ctrl)  s += "Ctrl+";
    if (b.shift) s += "Shift+";
    if (b.alt)   s += "Alt+";

    if (b.key >= 'A' && b.key <= 'Z') return s + char(b.key);
    if (b.key >= '0' && b.key <= '9') return s + char(b.key);

    if (b.key >= VK_F1 && b.key <= VK_F12) {
        s += "F" + std::to_string(b.key - VK_F1 + 1);
    }
    else if (b.key == VK_SPACE) {
        s += "Space";
    }
    else if (b.key == VK_INSERT) {
        s += "Insert";
    }
    else if (b.key == VK_DELETE) {
        s += "Delete";
    }
    else {
        s += "Key";
    }

    return s;
}

bool IsPressed(const KeyBind& b) {
    if (!b.key) return false;
    if (!(GetAsyncKeyState(b.key) & 0x8000)) return false;
    bool c = GetAsyncKeyState(VK_CONTROL) & 0x8000;
    bool s = GetAsyncKeyState(VK_SHIFT) & 0x8000;
    bool a = GetAsyncKeyState(VK_MENU) & 0x8000;
    return (b.ctrl == !!c) && (b.shift == !!s) && (b.alt == !!a);
}

bool IsModifier(int vk) {
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:
    case VK_MENU:    case VK_LMENU:    case VK_RMENU:
        return true;
    default:
        return false;
    }
}

void HandleHotkeys() {
    static bool wasListening = false;

    // ESC cancels binding at any time
    if (listeningFor != 0) {
        if (GetAsyncKeyState(VK_ESCAPE) & 1) {  // pressed this frame
            listeningFor = 0;
            wasListening = false;
            return;
        }
    }

    // ── Binding mode ───────────────────────────────────────
    if (listeningFor != 0) {
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        // Look for any non-modifier key that JUST went down
        for (int k = 8; k <= 255; ++k) {
            if (IsModifier(k)) continue;           // skip modifiers
            if (k == VK_ESCAPE) continue;          // already handled

            short state = GetAsyncKeyState(k);
            bool isDownNow = (state & 0x8000) != 0;
            bool wasDown = (state & 0x0001) != 0; // transition bit: went down this frame

            if (isDownNow && wasDown) {  // key pressed this exact frame
                // Found valid main key + current modifiers
                KeyBind newBind = { k, ctrl, shift, alt };

                if (listeningFor == 1) bindNoclip = newBind;
                if (listeningFor == 2) bindNoColl = newBind;
                if (listeningFor == 3) bindInfJump = newBind;
                if (listeningFor == 4) bindToggleUI = newBind;

                listeningFor = 0;
                wasListening = false;
                return;  // only bind one key per frame
            }
        }

        wasListening = true;
        return;
    }

    // ── Normal hotkey checking (when NOT binding) ─────────
    wasListening = false;

    static bool prevNoclip = false;
    static bool prevNoColl = false;
    static bool prevInfJump = false;
    static bool prevToggle = false;

    bool nowNoclip = IsPressed(bindNoclip);
    bool nowNoColl = IsPressed(bindNoColl);
    bool nowInfJump = IsPressed(bindInfJump);
    bool nowToggle = IsPressed(bindToggleUI);

    if (nowNoclip && !prevNoclip)   noclip = !noclip;
    if (nowNoColl && !prevNoColl)   noCollision = !noCollision;
    if (nowInfJump && !prevInfJump)  infJump = !infJump;
    if (nowToggle && !prevToggle)   showMenu = !showMenu;

    prevNoclip = nowNoclip;
    prevNoColl = nowNoColl;
    prevInfJump = nowInfJump;
    prevToggle = nowToggle;
}

// ────────────────────────────────────────────────
// Accounts & Config
// ────────────────────────────────────────────────

std::string AppDataPath(const std::string& fn) {
    char p[MAX_PATH]{};
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, p))) return "";
    std::string dir = std::string(p) + "\\Cubic";
    CreateDirectoryA(dir.c_str(), NULL);
    return dir + "\\" + fn;
}

void LoadAccounts() {
    accounts.clear();
    std::ifstream f(AppDataPath("accounts.json"));
    if (!f) return;
    try {
        json j; f >> j;
        if (!j.is_array()) return;
        for (auto& e : j) {
            std::string u = e.value("username", "");
            std::string p = e.value("password", "");
            u.erase(0, u.find_first_not_of(" \t\r\n"));
            u.erase(u.find_last_not_of(" \t\r\n") + 1);
            if (!u.empty()) accounts.push_back({ u,p });
        }
    }
    catch (...) {}
}

void SaveAccounts() {
    json arr = json::array();
    for (auto& a : accounts) {
        std::string u = a.user;
        u.erase(0, u.find_first_not_of(" \t\r\n"));
        u.erase(u.find_last_not_of(" \t\r\n") + 1);
        if (!u.empty()) arr.push_back({ {"username",a.user},{"password",a.pass} });
    }
    std::ofstream f(AppDataPath("accounts.json"), std::ios::trunc);
    if (f) f << arr.dump(2);
}

bool AddAccount(const std::string& u, const std::string& p) {
    std::string tu = u, tp = p;
    tu.erase(0, tu.find_first_not_of(" \t\r\n")); tu.erase(tu.find_last_not_of(" \t\r\n") + 1);
    tp.erase(0, tp.find_first_not_of(" \t\r\n")); tp.erase(tp.find_last_not_of(" \t\r\n") + 1);
    if (tu.empty() || tp.empty()) return false;
    for (auto& a : accounts) if (a.user == tu) return false;
    accounts.push_back({ tu, tp });
    SaveAccounts();
    return true;
}

void WriteConnectionCfg(const Account& a) {
    std::ofstream f(AppDataPath("Connection.cfg"), std::ios::trunc);
    if (!f) return;
    f << "Game.RememberMe=true\nGame.Username=" << a.user << "\nGame.Password=" << a.pass << "\n";
}

void LaunchGame() {
    ShellExecuteW(NULL, L"open", L"steam://rungameid/317470", NULL, NULL, SW_SHOWNORMAL);
}

void LoginSelected() {
    if (selectedAcc < 0 || selectedAcc >= (int)accounts.size()) return;
    if (GetProcId(L"Cubic.exe")) {
        // very rough – in real code consider more graceful shutdown
        Sleep(800);
    }
    WriteConnectionCfg(accounts[selectedAcc]);
    LaunchGame();
}

void LoadSettings() {
    std::ifstream f(AppDataPath("config.json"));
    if (!f) return;
    try {
        json j; f >> j;
        noclip = j.value("noclip", false);
        noCollision = j.value("noCollision", false);
        infJump = j.value("infJump", false);

        bindNoclip = { j.value("noclipKey",0),   j.value("noclipCtrl",false),   j.value("noclipShift",false),   j.value("noclipAlt",false) };
        bindNoColl = { j.value("noCollKey",0),   j.value("noCollCtrl",false),   j.value("noCollShift",false),   j.value("noCollAlt",false) };
        bindInfJump = { j.value("infJumpKey",0),  j.value("infJumpCtrl",false),  j.value("infJumpShift",false),  j.value("infJumpAlt",false) };
        bindToggleUI = { j.value("toggleKey",VK_INSERT), j.value("toggleCtrl",false), j.value("toggleShift",false), j.value("toggleAlt",false) };
    }
    catch (...) {}
}

void SaveSettings() {
    json j;
    j["noclip"] = noclip;
    j["noCollision"] = noCollision;
    j["infJump"] = infJump;

    j["noclipKey"] = bindNoclip.key;   j["noclipCtrl"] = bindNoclip.ctrl;   j["noclipShift"] = bindNoclip.shift;   j["noclipAlt"] = bindNoclip.alt;
    j["noCollKey"] = bindNoColl.key;   j["noCollCtrl"] = bindNoColl.ctrl;   j["noCollShift"] = bindNoColl.shift;   j["noCollAlt"] = bindNoColl.alt;
    j["infJumpKey"] = bindInfJump.key;  j["infJumpCtrl"] = bindInfJump.ctrl;  j["infJumpShift"] = bindInfJump.shift;  j["infJumpAlt"] = bindInfJump.alt;
    j["toggleKey"] = bindToggleUI.key; j["toggleCtrl"] = bindToggleUI.ctrl; j["toggleShift"] = bindToggleUI.shift; j["toggleAlt"] = bindToggleUI.alt;

    std::ofstream f(AppDataPath("config.json"), std::ios::trunc);
    if (f) f << j.dump(2);
}

// ────────────────────────────────────────────────
// UI
// ────────────────────────────────────────────────

void RenderInterface() {
    ImGui::SetNextWindowSize({ 284, 0 }, ImGuiCond_Always);
    if (!ImGui::Begin("CastleWare", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::End(); return;
    }

    if (ImGui::BeginTabBar("Tab")) {
        if (ImGui::BeginTabItem("Cheats")) {
            ImGui::Checkbox("Noclip", &noclip);
            ImGui::SameLine(); if (ImGui::SmallButton("Bind##1")) listeningFor = 1;
            ImGui::Text(" Bound: %s", KeyToString(bindNoclip).c_str());

            ImGui::Checkbox("No Collision", &noCollision);
            ImGui::SameLine(); if (ImGui::SmallButton("Bind##2")) listeningFor = 2;
            ImGui::Text(" Bound: %s", KeyToString(bindNoColl).c_str());

            ImGui::Checkbox("Infinite Jump", &infJump);
            ImGui::SameLine(); if (ImGui::SmallButton("Bind##3")) listeningFor = 3;
            ImGui::Text(" Bound: %s", KeyToString(bindInfJump).c_str());

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Accounts")) {
            static char userBuf[128]{}, passBuf[128]{};

            if (ImGui::BeginListBox("##accs", { 0, 140 })) {
                for (size_t i = 0; i < accounts.size(); ++i) {
                    bool sel = (selectedAcc == (int)i);
                    std::string disp = accounts[i].user.empty() ? "<empty>" : accounts[i].user;
                    if (ImGui::Selectable(disp.c_str(), sel)) {
                        selectedAcc = (int)i;
                        strncpy_s(userBuf, accounts[i].user.c_str(), _TRUNCATE);
                        strncpy_s(passBuf, accounts[i].pass.c_str(), _TRUNCATE);
                    }
                }
                ImGui::EndListBox();
            }

            ImGui::InputText("Username", userBuf, sizeof(userBuf));
            ImGui::InputText("Password", passBuf, sizeof(passBuf), ImGuiInputTextFlags_Password);

            if (ImGui::Button("Add")) {
                if (AddAccount(userBuf, passBuf)) {
                    LoadAccounts();
                    statusMsg = "Added.";
                    userBuf[0] = passBuf[0] = 0;
                }
                else statusMsg = "Failed (dup/empty)";
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete") && selectedAcc >= 0) {
                accounts.erase(accounts.begin() + selectedAcc);
                SaveAccounts();
                LoadAccounts();
                selectedAcc = -1;
                userBuf[0] = passBuf[0] = 0;
                statusMsg = "Deleted.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Login")) {
                LoginSelected();
                statusMsg = "Login triggered.";
            }

            ImGui::TextColored({ 0.6f,1.0f,0.6f,1 }, "%s", statusMsg.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Settings")) {
            ImGui::Text("Menu toggle:");
            ImGui::SameLine(); if (ImGui::SmallButton("Bind##4")) listeningFor = 4;
            ImGui::Text(" Bound: %s", KeyToString(bindToggleUI).c_str());

            if (ImGui::Button("Save settings")) SaveSettings();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Small bind overlay when listening
    if (listeningFor) {
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(
            ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f),
            ImGuiCond_Always,
            ImVec2(0.5f, 0.5f)
        );
        ImGui::SetNextWindowSize({ 320,0 });
        if (ImGui::Begin("Bind key", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Press key for %s  (ESC = cancel)",
                listeningFor == 1 ? "Noclip" : listeningFor == 2 ? "No Collision" : listeningFor == 3 ? "Inf Jump" : "UI Toggle");
            ImGui::End();
        }
    }

    ImGui::End();
}

// ────────────────────────────────────────────────
// Cheats
// ────────────────────────────────────────────────

void ApplyCheats() {
    if (!hProc || !playerPtr) return;

    uint32_t targetSize = PSZ_DEFAULT;
    if (noCollision) targetSize = PSZ_NOCOLL;
    else if (noclip) targetSize = PSZ_TINY;

    WriteU32(playerPtr, playerSizeOffset, targetSize);

    if (infJump)
        WriteU32(playerPtr, jumpPotentialOffset, JUMP_INF);
}
