// Dear ImGui: standalone example application for Windows API + DirectX 9

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <d3d9.h>
#include <tchar.h>
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <cstdint>
#include "offsets.h"

// Data
static LPDIRECT3D9              g_pD3D = nullptr;
static LPDIRECT3DDEVICE9        g_pd3dDevice = nullptr;
static bool                     g_DeviceLost = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static D3DPRESENT_PARAMETERS    g_d3dpp = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// --- Process memory helper functions and state ---
static HANDLE hProcess = nullptr;
static DWORD targetPid = 0;
static uintptr_t moduleBase = 0;
static uintptr_t pointerP = 0;

static bool noclip = false;
static bool nocollision = false;
static bool freezeJump = false;

// Key bindings
static int noclipKey = 0;
static int nocollisionKey = 0;
static int freezeJumpKey = 0;
static bool noclipCtrl = false, noclipShift = false, noclipAlt = false;
static bool nocollisionCtrl = false, nocollisionShift = false, nocollisionAlt = false;
static bool freezeJumpCtrl = false, freezeJumpShift = false, freezeJumpAlt = false;
static int listeningBind = 0; // 0 = none, 1 = noclip, 2 = nocollision, 3 = freezejump

// Track previous key pressed states for edge detection
static bool prev_noclipKeyPressed = false;
static bool prev_nocollisionKeyPressed = false;
static bool prev_freezeJumpKeyPressed = false;

// Helper: convert key + modifiers to display string
static std::string GetKeyNameWithMods(int key, bool ctrl, bool shift, bool alt)
{
    if (key == 0)
        return std::string("None");
    std::string s;
    if (ctrl) s += "Ctrl+";
    if (shift) s += "Shift+";
    if (alt) s += "Alt+";
    if (key >= 'A' && key <= 'Z') { s += char(key); return s; }
    if (key >= '0' && key <= '9') { s += char(key); return s; }
    switch (key)
    {
    case VK_F1: s += "F1"; break; case VK_F2: s += "F2"; break; case VK_F3: s += "F3"; break;
    case VK_F4: s += "F4"; break; case VK_F5: s += "F5"; break; case VK_F6: s += "F6"; break;
    case VK_F7: s += "F7"; break; case VK_F8: s += "F8"; break; case VK_F9: s += "F9"; break;
    case VK_F10: s += "F10"; break; case VK_F11: s += "F11"; break; case VK_F12: s += "F12"; break;
    case VK_SPACE: s += "Space"; break;
    case VK_DELETE: s += "Delete"; break;
    case VK_INSERT: s += "Insert"; break;
    default: s += "Key"; break;
    }
    return s;
}

static const uint32_t VALUE_DEFAULT_PLAYERSIZE = 1065353216u; // normal
static const uint32_t VALUE_NOCLIP_PLAYERSIZE  = 985353216u;  // noclip
static const uint32_t VALUE_NOCOLLISION        = 0u;
static const uint32_t VALUE_JUMP_FROZEN        = 0u;
static const uint32_t VALUE_JUMP_NORMAL        = 6400u;

static DWORD GetProcessIdByName(const std::wstring& name)
{
    DWORD pid = 0;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (name == entry.szExeFile)
            {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

static uintptr_t GetModuleBaseAddress(DWORD pid, const std::wstring& moduleName)
{
    uintptr_t baseAddress = 0;
    MODULEENTRY32W me32;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return 0;
    me32.dwSize = sizeof(MODULEENTRY32W);
    if (Module32FirstW(hSnapshot, &me32))
    {
        do
        {
            if (moduleName == me32.szModule)
            {
                baseAddress = (uintptr_t)me32.modBaseAddr;
                break;
            }
        } while (Module32NextW(hSnapshot, &me32));
    }
    CloseHandle(hSnapshot);
    return baseAddress;
}

static bool AttachToTarget()
{
    if (hProcess)
        return true;

    targetPid = GetProcessIdByName(targetProcessName);
    if (targetPid == 0)
        return false;

    hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, targetPid);
    if (!hProcess)
    {
        targetPid = 0;
        return false;
    }

    moduleBase = GetModuleBaseAddress(targetPid, targetProcessName);
    if (moduleBase == 0)
    {
        CloseHandle(hProcess);
        hProcess = nullptr;
        targetPid = 0;
        return false;
    }

    // Read pointer P from module base + offsetGameToBaseAddress
    // The game's pointer is stored as a 32-bit value (4 bytes). Read as uint32_t to be compatible with 32-bit game.
    uint32_t p32 = 0;
    SIZE_T bytesRead = 0;
    uintptr_t readAddress = moduleBase + offsetGameToBaseAddress;
    if (!ReadProcessMemory(hProcess, (LPCVOID)readAddress, &p32, sizeof(p32), &bytesRead) || bytesRead != sizeof(p32))
    {
        // Could not read pointer
        CloseHandle(hProcess);
        hProcess = nullptr;
        targetPid = 0;
        moduleBase = 0;
        return false;
    }
    // Zero-extend 32-bit pointer into uintptr_t (works when host is x64 and target is x86)
    pointerP = (uintptr_t)p32;
    return true;
}

static void DetachTarget()
{
    if (hProcess)
    {
        CloseHandle(hProcess);
        hProcess = nullptr;
    }
    targetPid = 0;
    moduleBase = 0;
    pointerP = 0;
}

// Ensure we are attached to the target process. Try to auto-attach if not attached,
// and validate existing handle/module each frame. If the target exits or module disappears,
// detach and attempt to reattach.
static void EnsureAttached()
{
    if (hProcess)
    {
        // Verify process still running
        DWORD exitCode = 0;
        if (!GetExitCodeProcess(hProcess, &exitCode) || exitCode != STILL_ACTIVE)
        {
            // Process ended
            DetachTarget();
            return;
        }

        // Verify module base still valid
        uintptr_t mb = GetModuleBaseAddress(targetPid, targetProcessName);
        if (mb == 0)
        {
            // Module not found anymore (map/server change?), detach and attempt reattach next frame
            DetachTarget();
            return;
        }
        if (mb != moduleBase)
        {
            moduleBase = mb;
        }

        // Re-read pointerP in case it changed (e.g. due to server switch)
        uint32_t p32 = 0;
        SIZE_T br = 0;
        uintptr_t readAddress = moduleBase + offsetGameToBaseAddress;
        if (ReadProcessMemory(hProcess, (LPCVOID)readAddress, &p32, sizeof(p32), &br) && br == sizeof(p32))
        {
            pointerP = (uintptr_t)p32;
        }
        else
        {
            // Failed to read pointer; detach and try to recover next frame
            DetachTarget();
        }
    }
    else
    {
        // Try to attach automatically (silent)
        AttachToTarget();
    }
}

static bool ReadUint32FromOffset(uintptr_t base, uintptr_t off, uint32_t &out)
{
    if (!hProcess || base == 0)
        return false;
    SIZE_T br = 0;
    return ReadProcessMemory(hProcess, (LPCVOID)(base + off), &out, sizeof(out), &br) && br == sizeof(out);
}

static bool WriteUint32ToOffset(uintptr_t base, uintptr_t off, uint32_t value)
{
    if (!hProcess || base == 0)
        return false;
    SIZE_T bw = 0;
    return WriteProcessMemory(hProcess, (LPVOID)(base + off), &value, sizeof(value), &bw) && bw == sizeof(value);
}

// Main code
int main(int, char**)
{
    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create application window: small borderless layered topmost window (overlay)
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    // Use WS_EX_TOPMOST so this acts as a tiny overlay window. Size is in logical pixels and multiplied by main_scale for HiDPI.
    HWND hwnd = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST, wc.lpszClassName, L"Cubic External", WS_POPUP | WS_VISIBLE, 120, 120, (int)(1080 * main_scale), (int)(720 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Make magenta color-key transparent so only ImGui-drawn pixels are visible.
    // Using magenta (255,0,255) avoids accidental removal of dark UI pixels.
    SetLayeredWindowAttributes(hwnd, RGB(255, 0, 255), 0, LWA_COLORKEY);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // Minimal style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    // Set a semi-opaque window background so the panel is visible (not 100% transparent)
    ImVec4& wb = style.Colors[ImGuiCol_WindowBg];
    wb = ImVec4(0.0f, 0.03f, 0.03f, 0.9f);
    // Disable the modal dimming background so BeginPopupModal doesn't draw a fullscreen dim (was showing as magenta due to color-key)
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    // Our state
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // black -> transparent via color key

    // Track previous states to perform restore writes on toggle off
    bool prev_noclip = false;
    bool prev_nocollision = false;
    bool prev_freezeJump = false;

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle lost D3D9 device
        if (g_DeviceLost)
        {
            HRESULT hr = g_pd3dDevice->TestCooperativeLevel();
            if (hr == D3DERR_DEVICELOST)
            {
                ::Sleep(10);
                continue;
            }
            if (hr == D3DERR_DEVICENOTRESET)
                ResetDevice();
            g_DeviceLost = false;
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Minimal window with three toggles
        ImGui::SetNextWindowSize(ImVec2(200.0f * main_scale, 250.0f * main_scale), ImGuiCond_FirstUseEver);
        ImGui::Begin("Cubic Mini", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
        // Make the title draggable: invisible full-width drag area, center the title text, and move the host window when dragging.
        {
            const char* title = "Cubic External";
            ImVec2 txt_sz = ImGui::CalcTextSize(title);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            // Create an invisible button that spans the top bar so it receives active/drag events
            ImGui::InvisibleButton("title_drag", ImVec2(ImGui::GetWindowWidth() - style.WindowPadding.x * 2.0f, txt_sz.y));
            bool title_active = ImGui::IsItemActive();
            // Center the title text on the top bar
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - txt_sz.x) * 0.5f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - txt_sz.y);
            ImGui::TextColored(ImVec4(0.60f, 0.80f, 1.0f, 1.0f), "%s", title);
            ImGui::PopStyleVar(2);
            if (title_active)
            {
                ImVec2 md = ImGui::GetIO().MouseDelta;
                ImVec2 wp = ImGui::GetWindowPos();
                wp.x += md.x;
                wp.y += md.y;
                ImGui::SetWindowPos(wp);
            }
        }

        if (!hProcess)
        {
            if (ImGui::Button("Attach"))
            {
                AttachToTarget();
            }
            ImGui::SameLine();
            if (ImGui::Button("Detach"))
            {
                DetachTarget();
            }
        }
        ImGui::Separator();

        ImGui::Checkbox("No Clip", &noclip);
        ImGui::SameLine();
        if (ImGui::SmallButton("Bind##noclip")) { listeningBind = 1; ImGui::OpenPopup("BindPopup"); }
        ImGui::SameLine(); ImGui::Text("[%s]", GetKeyNameWithMods(noclipKey, noclipCtrl, noclipShift, noclipAlt).c_str());
        ImGui::NewLine();
        ImGui::Checkbox("No Collision", &nocollision);
        ImGui::SameLine();
        if (ImGui::SmallButton("Bind##nocollision")) { listeningBind = 2; ImGui::OpenPopup("BindPopup"); }
        ImGui::SameLine(); ImGui::Text("[%s]", GetKeyNameWithMods(nocollisionKey, nocollisionCtrl, nocollisionShift, nocollisionAlt).c_str());
        ImGui::NewLine();
        ImGui::Checkbox("Infinite Jump", &freezeJump);
        ImGui::SameLine();
        if (ImGui::SmallButton("Bind##freezejump")) { listeningBind = 3; ImGui::OpenPopup("BindPopup"); }
        ImGui::SameLine(); ImGui::Text("[%s]", GetKeyNameWithMods(freezeJumpKey, freezeJumpCtrl, freezeJumpShift, freezeJumpAlt).c_str());

        // Bind popup: capture key or unbind
        if (ImGui::BeginPopupModal("BindPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Press a key or key combo to bind. Press Esc to cancel.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Show which binding we are editing
            const char* bindName = (listeningBind == 1) ? "No Clip" : (listeningBind == 2) ? "No Collision" : (listeningBind == 3) ? "Infinite Jump" : "Unknown";
            ImGui::Text("Binding: %s", bindName);

            // Unbind button
            if (ImGui::Button("Unbind"))
            {
                if (listeningBind == 1) { noclipKey = 0; noclipCtrl = noclipShift = noclipAlt = false; }
                else if (listeningBind == 2) { nocollisionKey = 0; nocollisionCtrl = nocollisionShift = nocollisionAlt = false; }
                else if (listeningBind == 3) { freezeJumpKey = 0; freezeJumpCtrl = freezeJumpShift = freezeJumpAlt = false; }
                listeningBind = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { listeningBind = 0; ImGui::CloseCurrentPopup(); }

            // Capture keys
            // Common keys to capture
            static const int keys[] = {
                'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
                '0','1','2','3','4','5','6','7','8','9',
                VK_F1,VK_F2,VK_F3,VK_F4,VK_F5,VK_F6,VK_F7,VK_F8,VK_F9,VK_F10,VK_F11,VK_F12,
                VK_SPACE, VK_DELETE, VK_INSERT
            };

            // Read modifier states
            bool curCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool curShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            bool curAlt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

            // If user presses Escape -> cancel
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            {
                listeningBind = 0;
                ImGui::CloseCurrentPopup();
            }

            for (int k : keys)
            {
                if (GetAsyncKeyState(k) & 0x8000)
                {
                    if (listeningBind == 1)
                    {
                        noclipKey = k; noclipCtrl = curCtrl; noclipShift = curShift; noclipAlt = curAlt;
                    }
                    else if (listeningBind == 2)
                    {
                        nocollisionKey = k; nocollisionCtrl = curCtrl; nocollisionShift = curShift; nocollisionAlt = curAlt;
                    }
                    else if (listeningBind == 3)
                    {
                        freezeJumpKey = k; freezeJumpCtrl = curCtrl; freezeJumpShift = curShift; freezeJumpAlt = curAlt;
                    }
                    listeningBind = 0;
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }

            ImGui::EndPopup();
        }

        // Handle key press toggling for bound keys (with modifiers and edge detection)
        auto IsBindPressed = [&](int key, bool needCtrl, bool needShift, bool needAlt)->bool {
            if (key == 0) return false;
            bool pressed = (GetAsyncKeyState(key) & 0x8000) != 0;
            if (!pressed) return false;
            if (needCtrl && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) return false;
            if (needShift && !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) return false;
            if (needAlt && !(GetAsyncKeyState(VK_MENU) & 0x8000)) return false;
            return true;
        };

        bool cur_noclipPressed = IsBindPressed(noclipKey, noclipCtrl, noclipShift, noclipAlt);
        if (cur_noclipPressed && !prev_noclipKeyPressed)
            noclip = !noclip;
        prev_noclipKeyPressed = cur_noclipPressed;

        bool cur_nocollisionPressed = IsBindPressed(nocollisionKey, nocollisionCtrl, nocollisionShift, nocollisionAlt);
        if (cur_nocollisionPressed && !prev_nocollisionKeyPressed)
            nocollision = !nocollision;
        prev_nocollisionKeyPressed = cur_nocollisionPressed;

        bool cur_freezeJumpPressed = IsBindPressed(freezeJumpKey, freezeJumpCtrl, freezeJumpShift, freezeJumpAlt);
        if (cur_freezeJumpPressed && !prev_freezeJumpKeyPressed)
            freezeJump = !freezeJump;
        prev_freezeJumpKeyPressed = cur_freezeJumpPressed;

        ImGui::Text("Status: %s", hProcess ? "Attached" : "Not attached");
        ImGui::End();

        // Ensure attachment to the target process
        EnsureAttached();

        // If attached, apply settings per frame
        if (hProcess && pointerP != 0)
        {
            // Player size precedence: NoCollision > Noclip > Default
            if (nocollision)
            {
                WriteUint32ToOffset(pointerP, playerSizeOffset, VALUE_NOCOLLISION);
            }
            else if (noclip)
            {
                WriteUint32ToOffset(pointerP, playerSizeOffset, VALUE_NOCLIP_PLAYERSIZE);
            }
            else
            {
                WriteUint32ToOffset(pointerP, playerSizeOffset, VALUE_DEFAULT_PLAYERSIZE);
            }

            // Jump freeze handling: when enabled, repeatedly write the normal jump potential (6400)
            // to prevent the game from modifying it. When disabled, allow the game to update the value.
            if (freezeJump)
            {
                WriteUint32ToOffset(pointerP, jumpPotentialOffset, VALUE_JUMP_NORMAL);
            }
        }

        // Update previous states
        prev_noclip = noclip;
        prev_nocollision = nocollision;
        prev_freezeJump = freezeJump;

        // Rendering
        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        // Clear to magenta color-key so the layered window makes that color transparent.
        // Use an explicit magenta color to avoid shadowing the clear_color variable declared earlier.
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA(255, 0, 255, 255);
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST)
            g_DeviceLost = true;
    }

    // Cleanup
    DetachTarget();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr)
        return false;

    // Create the D3DDevice
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN; // Need to use an explicit format with alpha if needing per-pixel alpha composition.
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;           // Present with vsync
    //g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;   // Present without vsync, maximum unthrottled framerate
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;

    return true;
}

void CleanupDeviceD3D()
{
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}

void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
