#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <cwchar>
#include <string>
#include <vector>

namespace {

constexpr wchar_t window_class[] = L"RecallMemoryDesktopWindow";
constexpr UINT_PTR process_timer = 1;
constexpr UINT autostart_message = WM_APP + 1;

enum ControlId {
    database_edit = 1001,
    browse_button,
    host_edit,
    port_edit,
    service_button,
    health_button,
    log_edit,
};

struct AppState {
    HWND window{};
    HWND status{};
    HWND database{};
    HWND browse{};
    HWND host{};
    HWND port{};
    HWND service{};
    HWND health{};
    HWND log{};
    HFONT font{};
    HANDLE process{};
    HANDLE job{};
    DWORD process_id{};
    bool autostart{};
};

AppState app;

std::wstring control_text(HWND control) {
    const auto length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);
    GetWindowTextW(control, buffer.data(), static_cast<int>(buffer.size()));
    return buffer.data();
}

std::filesystem::path executable_directory() {
    std::vector<wchar_t> buffer(32768);
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

std::filesystem::path default_database() {
    PWSTR local_data = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local_data))) {
        const auto path = std::filesystem::path(local_data) / L"Recall Memory" / L"recall-memory.db";
        CoTaskMemFree(local_data);
        return path;
    }
    return executable_directory() / L"recall-memory.db";
}

void append_log(const std::wstring& message) {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t prefix[32]{};
    swprintf_s(prefix, L"[%02u:%02u:%02u] ", time.wHour, time.wMinute, time.wSecond);
    const std::wstring line = std::wstring(prefix) + message + L"\r\n";
    SendMessageW(app.log, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(app.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

void set_running(bool running) {
    SetWindowTextW(app.status, running ? L"● 服务运行中" : L"○ 服务未运行");
    SetWindowTextW(app.service, running ? L"停止服务" : L"启动服务");
    EnableWindow(app.database, !running);
    EnableWindow(app.browse, !running);
    EnableWindow(app.host, !running);
    EnableWindow(app.port, !running);
    EnableWindow(app.health, running);
}

void release_process_handles() {
    if (app.process != nullptr) CloseHandle(app.process);
    if (app.job != nullptr) CloseHandle(app.job);
    app.process = nullptr;
    app.job = nullptr;
    app.process_id = 0;
    set_running(false);
}

void stop_service(bool write_log = true) {
    if (app.process == nullptr) return;
    if (app.job != nullptr) TerminateJobObject(app.job, 0);
    else TerminateProcess(app.process, 0);
    WaitForSingleObject(app.process, 3000);
    release_process_handles();
    if (write_log) append_log(L"服务已停止");
}

bool start_service() {
    const auto daemon = executable_directory() / L"recall-memoryd.exe";
    if (!std::filesystem::exists(daemon)) {
        MessageBoxW(app.window, L"未找到 recall-memoryd.exe，请确保桌面端和服务程序位于同一目录。",
                    L"无法启动服务", MB_OK | MB_ICONERROR);
        return false;
    }

    const auto database = std::filesystem::path(control_text(app.database));
    const auto host = control_text(app.host);
    const auto port_text = control_text(app.port);
    wchar_t* port_end = nullptr;
    const auto port = wcstol(port_text.c_str(), &port_end, 10);
    if (database.empty() || host.empty() || port_end == port_text.c_str() || *port_end != L'\0' ||
        port < 1 || port > 65535) {
        MessageBoxW(app.window, L"请填写有效的数据库路径、监听地址和端口（1-65535）。",
                    L"配置无效", MB_OK | MB_ICONWARNING);
        return false;
    }

    std::error_code error;
    if (!database.parent_path().empty()) std::filesystem::create_directories(database.parent_path(), error);
    if (error) {
        MessageBoxW(app.window, L"无法创建数据库目录。", L"配置无效", MB_OK | MB_ICONERROR);
        return false;
    }

    std::wstring command = L"\"" + daemon.wstring() + L"\" --db \"" + database.wstring() +
                           L"\" --host \"" + host + L"\" --port " + port_text;
    std::vector<wchar_t> command_buffer(command.begin(), command.end());
    command_buffer.push_back(L'\0');

    app.job = CreateJobObjectW(nullptr, nullptr);
    if (app.job == nullptr) {
        MessageBoxW(app.window, L"无法创建服务进程托管对象。", L"启动失败", MB_OK | MB_ICONERROR);
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(app.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        release_process_handles();
        MessageBoxW(app.window, L"无法配置服务进程托管对象。", L"启动失败", MB_OK | MB_ICONERROR);
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto working_directory = executable_directory().wstring();
    if (!CreateProcessW(daemon.c_str(), command_buffer.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                        working_directory.c_str(), &startup, &process)) {
        release_process_handles();
        MessageBoxW(app.window, L"服务进程启动失败。", L"启动失败", MB_OK | MB_ICONERROR);
        return false;
    }
    CloseHandle(process.hThread);
    app.process = process.hProcess;
    app.process_id = process.dwProcessId;
    if (!AssignProcessToJobObject(app.job, app.process)) {
        stop_service(false);
        MessageBoxW(app.window, L"服务进程无法加入托管对象。", L"启动失败", MB_OK | MB_ICONERROR);
        return false;
    }

    set_running(true);
    append_log(L"服务已启动，进程 ID：" + std::to_wstring(app.process_id));
    append_log(L"状态地址：http://" + host + L":" + port_text + L"/health");
    return true;
}

void browse_database() {
    wchar_t file[MAX_PATH]{};
    const auto current = control_text(app.database);
    wcsncpy_s(file, current.c_str(), _TRUNCATE);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.window;
    dialog.lpstrFilter = L"SQLite 数据库 (*.db)\0*.db\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"db";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog)) SetWindowTextW(app.database, file);
}

void open_health() {
    const auto url = L"http://" + control_text(app.host) + L":" + control_text(app.port) + L"/health";
    ShellExecuteW(app.window, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

HWND make_control(const wchar_t* class_name, const wchar_t* text, DWORD style, int id) {
    const auto control = CreateWindowExW(
        wcscmp(class_name, WC_EDITW) == 0 ? WS_EX_CLIENTEDGE : 0, class_name, text,
        WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    return control;
}

void layout_controls(int width, int height) {
    constexpr int margin = 24;
    constexpr int label_width = 88;
    constexpr int row_height = 30;
    constexpr int gap = 12;
    const int field_x = margin + label_width;
    const int content_width = width - margin * 2;
    const int browse_width = 88;

    MoveWindow(GetDlgItem(app.window, 2001), margin, 22, content_width, 28, TRUE);
    MoveWindow(GetDlgItem(app.window, 2002), margin, 66, label_width, row_height, TRUE);
    MoveWindow(app.database, field_x, 64, content_width - label_width - browse_width - gap, row_height, TRUE);
    MoveWindow(app.browse, width - margin - browse_width, 64, browse_width, row_height, TRUE);
    MoveWindow(GetDlgItem(app.window, 2003), margin, 108, label_width, row_height, TRUE);
    MoveWindow(app.host, field_x, 106, 220, row_height, TRUE);
    MoveWindow(GetDlgItem(app.window, 2004), field_x + 244, 108, 52, row_height, TRUE);
    MoveWindow(app.port, field_x + 300, 106, 90, row_height, TRUE);
    MoveWindow(app.service, margin, 152, 112, 34, TRUE);
    MoveWindow(app.health, margin + 124, 152, 112, 34, TRUE);
    MoveWindow(GetDlgItem(app.window, 2005), margin, 204, content_width, 24, TRUE);
    MoveWindow(app.log, margin, 232, content_width, (height > 280 ? height - 256 : 40), TRUE);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            app.window = window;
            app.font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            make_control(WC_STATICW, L"○ 服务未运行", SS_LEFT, 2001);
            make_control(WC_STATICW, L"数据库", SS_LEFT, 2002);
            app.database = make_control(WC_EDITW, default_database().c_str(), ES_AUTOHSCROLL, database_edit);
            app.browse = make_control(WC_BUTTONW, L"浏览...", BS_PUSHBUTTON, browse_button);
            make_control(WC_STATICW, L"监听地址", SS_LEFT, 2003);
            app.host = make_control(WC_EDITW, L"127.0.0.1", ES_AUTOHSCROLL, host_edit);
            make_control(WC_STATICW, L"端口", SS_LEFT, 2004);
            app.port = make_control(WC_EDITW, L"47831", ES_NUMBER | ES_AUTOHSCROLL, port_edit);
            app.service = make_control(WC_BUTTONW, L"启动服务", BS_DEFPUSHBUTTON, service_button);
            app.health = make_control(WC_BUTTONW, L"打开状态页", BS_PUSHBUTTON, health_button);
            make_control(WC_STATICW, L"运行日志", SS_LEFT, 2005);
            app.log = make_control(WC_EDITW, L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                                   log_edit);
            app.status = GetDlgItem(window, 2001);
            set_running(false);
            append_log(L"Recall Memory 服务控制台已就绪");
            SetTimer(window, process_timer, 500, nullptr);
            return 0;
        }
        case WM_SIZE:
            layout_controls(LOWORD(lparam), HIWORD(lparam));
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize = {640, 430};
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case service_button:
                    if (app.process != nullptr) stop_service();
                    else start_service();
                    return 0;
                case browse_button: browse_database(); return 0;
                case health_button: open_health(); return 0;
            }
            break;
        case WM_TIMER:
            if (wparam == process_timer && app.process != nullptr &&
                WaitForSingleObject(app.process, 0) == WAIT_OBJECT_0) {
                DWORD exit_code = 0;
                GetExitCodeProcess(app.process, &exit_code);
                release_process_handles();
                append_log(L"服务已退出，退出码：" + std::to_wstring(exit_code));
            }
            return 0;
        case autostart_message:
            start_service();
            return 0;
        case WM_DESTROY:
            KillTimer(window, process_timer);
            stop_service(false);
            if (app.font != nullptr) DeleteObject(app.font);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int show_command) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSEXW window_class_info{sizeof(window_class_info)};
    window_class_info.lpfnWndProc = window_proc;
    window_class_info.hInstance = instance;
    window_class_info.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class_info.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class_info.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class_info.lpszClassName = window_class;
    if (!RegisterClassExW(&window_class_info)) return 1;

    app.autostart = command_line != nullptr && std::wstring(command_line).find(L"--autostart") != std::wstring::npos;
    const auto window = CreateWindowExW(
        0, window_class, L"Recall Memory 服务控制台", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 520, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) return 1;
    ShowWindow(window, show_command);
    UpdateWindow(window);
    if (app.autostart) PostMessageW(window, autostart_message, 0, 0);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
