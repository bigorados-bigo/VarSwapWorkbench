#include "context_gl.h"
#include "varswap/varswap_pane.h"
#include "framedata.h"
#include "filedialog.h"
#include "ui/font_loader.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_win32.h>
#include <glad/glad.h>

#include <windows.h>
#include <shellapi.h>

#ifndef IMGUI_DISABLE
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

HWND mainWindowHandle = nullptr;
ImVec2 clientRect = ImVec2(1280.f, 720.f);

namespace {
ContextGl* gContext = nullptr;
char gIniPath[MAX_PATH] = {};

std::string WideToUtf8(const std::wstring& input) {
    if (input.empty()) {
        return {};
    }
    int required = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string output(static_cast<size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, output.data(), required, nullptr, nullptr);
    return output;
}

std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) {
        return {};
    }
    int required = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring output(static_cast<size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, output.data(), required);
    return output;
}

std::string FormatNiceName(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::filesystem::path p(path);
    return p.filename().string();
}

bool EndsWithInsensitive(const std::string& value, std::string_view ending) {
    if (value.size() < ending.size()) {
        return false;
    }
    size_t offset = value.size() - ending.size();
    for (size_t i = 0; i < ending.size(); ++i) {
        unsigned char lhs = static_cast<unsigned char>(value[offset + i]);
        unsigned char rhs = static_cast<unsigned char>(ending[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
            return false;
        }
    }
    return true;
}

std::string NormalizePath(const std::string& input) {
    std::filesystem::path p(input);
    try {
        auto absolute = std::filesystem::absolute(p);
        return absolute.lexically_normal().string();
    } catch (...) {
        return p.lexically_normal().string();
    }
}
}

class VarSwapWorkbenchFrame {
public:
    explicit VarSwapWorkbenchFrame(ContextGl* ctx)
        : context(ctx) {
        pane = std::make_unique<VarSwapPane>(&frameData);
        pane->onModified = [this]() {
            MarkDirty(true);
            SetStatus("Pending changes");
        };
        defaultDockLayoutPending = (gIniPath[0] == '\0') ? true : !std::filesystem::exists(gIniPath);
    }

    void RenderFrame() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawDockspace();
        DrawMenuBar();
        HandleShortcuts();
        pane->Draw();
        DrawWelcomeOverlay();
        DrawStatusBar();

        ImGui::Render();
        glViewport(0, 0, static_cast<int>(clientRect.x), static_cast<int>(clientRect.y));
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(context->dc);
    }

    bool LoadFile(const std::string& path) {
        if (path.empty()) {
            return false;
        }
        std::string normalized = NormalizePath(path);
        if (EndsWithInsensitive(normalized, ".txt")) {
            return LoadTxtBundle(normalized);
        }
        return LoadHa6File(normalized);
    }

    bool LoadFromDrop(const std::string& path) {
        if (path.empty()) {
            return false;
        }
        if (!EnsureChangesCommitted()) {
            return false;
        }
        return LoadFile(path);
    }

    bool SaveBeforeClose() {
        if (!EnsurePendingEditsResolved(L"closing")) {
            return false;
        }
        if (!dirty) {
            return true;
        }
        return Save();
    }

    bool HasUnsavedChanges() const {
        return dirty;
    }

private:
    ContextGl* context;
    FrameData frameData;
    std::unique_ptr<VarSwapPane> pane;
    std::string currentFilePath;
    std::string currentTxtPath;
    std::vector<std::string> layeredHa6Paths;
    bool dirty = false;
    std::string statusMessage;
    bool defaultDockLayoutPending = false;

    void DrawDockspace() {
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoBackground;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("VarSwapDockHost", nullptr, windowFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspaceId = ImGui::GetID("VarSwapWorkbenchDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        if (defaultDockLayoutPending) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);
            ImGuiID leftId = 0;
            ImGuiID rightId = 0;
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.35f, &leftId, &rightId);
            ImGui::DockBuilderDockWindow("Var Summary", leftId);
            ImGui::DockBuilderDockWindow("VarSwap Workbench", rightId);
            ImGui::DockBuilderFinish(dockspaceId);
            defaultDockLayoutPending = false;
        }
        ImGui::End();
    }

    void DrawMenuBar() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                    OpenFileDialog();
                }
                bool canSave = frameData.m_loaded != 0;
                if (ImGui::MenuItem("Save", "Ctrl+S", false, canSave)) {
                    Save();
                }
                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, canSave)) {
                    SaveAs();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) {
                    PostMessage(mainWindowHandle, WM_CLOSE, 0, 0);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void DrawStatusBar() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - 22));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 22));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 2.f));
        if (ImGui::Begin("VarSwapStatusBar", nullptr, flags)) {
            std::string fileLabel = BuildFileLabel();
            if (dirty) {
                fileLabel = "* " + fileLabel;
            }
            ImGui::TextUnformatted(fileLabel.c_str());
            bool pendingEdits = pane && pane->hasPendingEdits();
            if (pendingEdits) {
                ImGui::SameLine(0.f, 12.f);
                ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.2f, 1.f), "Pending edits");
            }
            if (!statusMessage.empty()) {
                ImGui::SameLine();
                ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 220.f);
                ImGui::TextDisabled("%s", statusMessage.c_str());
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void DrawWelcomeOverlay() {
        if (frameData.m_loaded) {
            return;
        }
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::SetNextWindowSize(ImVec2(420.f, 220.f), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("VarSwapWelcome", nullptr, flags)) {
            ImGui::Text("Welcome to VarSwap Workbench");
            ImGui::Separator();
            ImGui::TextWrapped("Open a .ha6 binary or a .txt bundle to inspect, filter, edit, and apply variable IDs without launching Hantei.");
            ImGui::Spacing();
            if (ImGui::Button("Open File")) {
                OpenFileDialog();
            }
        }
        ImGui::End();
    }

    void HandleShortcuts() {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
            if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                OpenFileDialog();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
                    SaveAs();
                } else {
                    Save();
                }
            }
        }
    }

    bool OpenFileDialog() {
        if (!EnsureChangesCommitted()) {
            return false;
        }
        std::string selected = FileDialog(fileType::HA6, false);
        if (selected.empty()) {
            return false;
        }
        return LoadFile(selected);
    }

    bool Save() {
        if (!frameData.m_loaded) {
            return false;
        }
        if (!EnsurePendingEditsResolved(L"saving")) {
            return false;
        }
        if (currentFilePath.empty()) {
            return SaveAs();
        }
        return SaveToPath(currentFilePath);
    }

    bool SaveAs() {
        if (!frameData.m_loaded) {
            return false;
        }
        if (!EnsurePendingEditsResolved(L"saving")) {
            return false;
        }
        std::string suggestion = SuggestDestinationPath();
        std::vector<char> buffer(suggestion.begin(), suggestion.end());
        buffer.push_back('\0');
        std::string target = FileDialog(fileType::HA6, true, buffer.data());
        if (target.empty()) {
            return false;
        }
        return SaveToPath(target);
    }

    bool SaveToPath(const std::string& path) {
        std::string normalized = NormalizePath(path);
        frameData.save(normalized.c_str());
        std::error_code ec;
        if (!std::filesystem::exists(normalized, ec)) {
            auto title = Utf8ToWide("Failed to save " + normalized);
            MessageBoxW(mainWindowHandle, title.c_str(), L"VarSwap Workbench", MB_ICONERROR | MB_OK);
            SetStatus("Save failed");
            return false;
        }
        bool replacedPath = (currentFilePath != normalized);
        currentFilePath = normalized;
        if (replacedPath) {
            currentTxtPath.clear();
            layeredHa6Paths.clear();
            layeredHa6Paths.push_back(normalized);
        }
        MarkDirty(false);
        SetStatus("Saved " + FormatNiceName(normalized));
        return true;
    }

    std::string SuggestDestinationPath() const {
        if (currentFilePath.empty()) {
            return "varswap_workbench.ha6";
        }
        std::filesystem::path p(currentFilePath);
        std::string stem = p.stem().string();
        std::string ext = p.extension().string();
        if (ext.empty()) {
            ext = ".ha6";
        }
        std::filesystem::path suggestion = p.parent_path() / (stem + "_workbench" + ext);
        return suggestion.string();
    }

    bool EnsureChangesCommitted() {
        if (!EnsurePendingEditsResolved(L"continuing")) {
            return false;
        }
        if (!dirty) {
            return true;
        }
        int response = MessageBoxW(
            mainWindowHandle,
            L"Save pending changes before continuing?",
            L"VarSwap Workbench",
            MB_ICONQUESTION | MB_YESNOCANCEL);
        if (response == IDYES) {
            return Save();
        }
        if (response == IDNO) {
            return true;
        }
        return false;
    }

    bool EnsurePendingEditsResolved(const wchar_t* actionLabel) {
        if (!pane || !pane->hasPendingEdits()) {
            return true;
        }
        std::wstring message = L"Apply or clear pending edits before ";
        if (actionLabel && *actionLabel) {
            message += actionLabel;
        } else {
            message += L"continuing";
        }
        message += L".";
        MessageBoxW(mainWindowHandle, message.c_str(), L"VarSwap Workbench", MB_ICONWARNING | MB_OK);
        SetStatus("Pending edits require attention");
        return false;
    }

    void SetStatus(const std::string& text) {
        statusMessage = text;
    }

    void MarkDirty(bool value) {
        dirty = value;
        UpdateWindowTitle();
    }

    void UpdateWindowTitle() {
        std::wstring title = L"VarSwap Workbench";
        if (!currentFilePath.empty()) {
            title += L" - ";
            if (!currentTxtPath.empty()) {
                title += Utf8ToWide(FormatNiceName(currentTxtPath));
                title += L" -> ";
            }
            title += Utf8ToWide(FormatNiceName(currentFilePath));
        }
        if (dirty) {
            title += L" *";
        }
        SetWindowTextW(mainWindowHandle, title.c_str());
    }

    bool LoadHa6File(const std::string& path) {
        FrameData newData;
        if (!newData.load(path.c_str())) {
            auto title = Utf8ToWide("Failed to load " + path);
            MessageBoxW(mainWindowHandle, title.c_str(), L"VarSwap Workbench", MB_ICONERROR | MB_OK);
            SetStatus("Load failed");
            return false;
        }

        frameData = std::move(newData);
        currentFilePath = path;
        currentTxtPath.clear();
        layeredHa6Paths.clear();
        layeredHa6Paths.push_back(path);
        pane->ForceRescan();
        MarkDirty(false);
        SetStatus("Loaded " + FormatNiceName(path));
        return true;
    }

    bool LoadTxtBundle(const std::string& path) {
        int fileNum = GetPrivateProfileIntA("DataFile", "FileNum", 0, path.c_str());
        if (fileNum <= 0) {
            auto title = Utf8ToWide("No HA6 files declared in " + path);
            MessageBoxW(mainWindowHandle, title.c_str(), L"VarSwap Workbench", MB_ICONERROR | MB_OK);
            SetStatus("Load failed");
            return false;
        }

        std::filesystem::path txtPath(path);
        std::filesystem::path baseFolder = txtPath.parent_path();
        if (baseFolder.empty()) {
            baseFolder = std::filesystem::current_path();
        }

        FrameData newData;
        std::vector<std::string> loadedPaths;
        loadedPaths.reserve(static_cast<size_t>(fileNum));
        for (int i = 0; i < fileNum; ++i) {
            char key[16];
            std::snprintf(key, sizeof(key), "File%02d", i);
            char ha6file[MAX_PATH] = {};
            GetPrivateProfileStringA("DataFile", key, "", ha6file, sizeof(ha6file), path.c_str());
            if (ha6file[0] == '\0') {
                continue;
            }

            std::filesystem::path candidate = baseFolder / ha6file;
            std::string normalized = NormalizePath(candidate.string());
            bool patch = !loadedPaths.empty();
            if (!newData.load(normalized.c_str(), patch)) {
                auto title = Utf8ToWide("Failed to load " + normalized + " from " + path);
                MessageBoxW(mainWindowHandle, title.c_str(), L"VarSwap Workbench", MB_ICONERROR | MB_OK);
                SetStatus("Load failed");
                return false;
            }
            loadedPaths.push_back(normalized);
        }

        if (loadedPaths.empty()) {
            auto title = Utf8ToWide("No HA6 files were loaded from " + path);
            MessageBoxW(mainWindowHandle, title.c_str(), L"VarSwap Workbench", MB_ICONERROR | MB_OK);
            SetStatus("Load failed");
            return false;
        }

        frameData = std::move(newData);
        currentTxtPath = path;
        layeredHa6Paths = loadedPaths;
        currentFilePath = layeredHa6Paths.back();
        pane->ForceRescan();
        MarkDirty(false);
        std::string label = "Loaded " + FormatNiceName(path) + " (" + FormatNiceName(currentFilePath) + ")";
        SetStatus(label);
        return true;
    }

    std::string BuildFileLabel() const {
        if (currentFilePath.empty()) {
            return "No file loaded";
        }
        if (currentTxtPath.empty()) {
            return currentFilePath;
        }
        return currentTxtPath + " -> " + currentFilePath;
    }
};

LRESULT CALLBACK WorkbenchWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX),
        CS_CLASSDC,
        WorkbenchWndProc,
        0L,
        0L,
        hInstance,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        L"VarSwapWorkbenchWindow",
        nullptr
    };

    RegisterClassEx(&wc);

    std::wstring windowTitle = L"VarSwap Workbench";
    HWND hwnd = CreateWindow(
        wc.lpszClassName,
        windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        800,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr);

    mainWindowHandle = hwnd;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    DragAcceptFiles(hwnd, TRUE);

    std::wstring startupPath;
    int argc = 0;
    LPWSTR fullCmd = GetCommandLineW();
    LPWSTR* argv = CommandLineToArgvW(fullCmd, &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (!argv[i] || argv[i][0] == L'-' || argv[i][0] == L'/') {
                continue;
            }
            startupPath = argv[i];
            break;
        }
        LocalFree(argv);
    }

    if (!startupPath.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(startupPath, ec)) {
            startupPath.clear();
        }
    }

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        auto* frame = reinterpret_cast<VarSwapWorkbenchFrame*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!frame) {
            continue;
        }

        if (!startupPath.empty()) {
            frame->LoadFile(WideToUtf8(startupPath));
            startupPath.clear();
        }

        frame->RenderFrame();
    }

    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}

LRESULT CALLBACK WorkbenchWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }

    auto* frame = reinterpret_cast<VarSwapWorkbenchFrame*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        gContext = new ContextGl(hWnd);
        if (!gladLoadGL()) {
            MessageBox(hWnd, L"Failed to load OpenGL", L"VarSwap Workbench", MB_ICONERROR | MB_OK);
            PostQuitMessage(1);
            return 0;
        }
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        int appendAt = GetCurrentDirectoryA(MAX_PATH, gIniPath);
        strcpy_s(gIniPath + appendAt, MAX_PATH - appendAt, "\\varswap_workbench.ini");
        io.IniFilename = gIniPath;

        LoadJapaneseFonts(io);

        ImGui_ImplWin32_Init(hWnd);
        ImGui_ImplOpenGL3_Init("#version 330 core");

        auto* newFrame = new VarSwapWorkbenchFrame(gContext);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(newFrame));

        return 0;
    }
    case WM_SIZE: {
        if (wParam != SIZE_MINIMIZED) {
            RECT rect;
            GetClientRect(hWnd, &rect);
            clientRect.x = static_cast<float>(rect.right - rect.left);
            clientRect.y = static_cast<float>(rect.bottom - rect.top);
        }
        break;
    }
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t filePath[MAX_PATH];
        if (DragQueryFile(drop, 0, filePath, MAX_PATH)) {
            std::string utf8 = WideToUtf8(filePath);
            if (frame) {
                frame->LoadFromDrop(utf8);
            }
        }
        DragFinish(drop);
        return 0;
    }
    case WM_CLOSE: {
        if (frame && !frame->SaveBeforeClose()) {
            return 0;
        }
        DestroyWindow(hWnd);
        return 0;
    }
    case WM_DESTROY: {
        delete frame;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        delete gContext;
        gContext = nullptr;

        PostQuitMessage(0);
        return 0;
    }
    default:
        break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}
