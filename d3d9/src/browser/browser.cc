#include "../internal.h"
#include <psapi.h>

#include "include/capi/cef_app_capi.h"
#include "include/capi/cef_client_capi.h"
#include "include/capi/cef_browser_capi.h"

// BROWSER PROCESS ONLY.

UINT REMOTE_DEBUGGING_PORT = 0;

cef_browser_t *browser_ = nullptr;
static int browser_id_ = -1;

extern LPCWSTR DEVTOOLS_WINDOW_NAME;

void PrepareDevTools();
void OpenDevTools_Internal(bool remote);
void SetUpBrowserWindow(cef_browser_t *browser, cef_frame_t *frame);

cef_resource_handler_t *CreateAssetsResourceHandler(const wstring &path, bool isPlugins);
cef_resource_handler_t *CreateRiotClientResourceHandler(cef_frame_t *frame, wstring path);
void SetRiotClientCredentials(const wstring &appPort, const wstring &authToken);

void OpenInternalServer();
void CloseInternalServer();

static decltype(cef_life_span_handler_t::on_after_created) Old_OnAfterCreated;
static void CEF_CALLBACK Hooked_OnAfterCreated(struct _cef_life_span_handler_t* self,
    struct _cef_browser_t* browser)
{
    if (browser_ == nullptr)
    {
        // Add ref.
        browser->base.add_ref(&browser_->base);
        // Save client browser.
        browser_ = browser;
        browser_id_ = browser->get_identifier(browser);

        // Initialize DevTools opener.
        PrepareDevTools();
    }

    Old_OnAfterCreated(self, browser);
}

static decltype(cef_life_span_handler_t::on_before_close) Old_OnBeforeClose;
static void CEF_CALLBACK Hooked_OnBeforeClose(cef_life_span_handler_t* self,
    struct _cef_browser_t* browser)
{
    // Check main browser.
    if (browser->get_identifier(browser) == browser_id_)
    {
        browser_ = nullptr;
        CloseInternalServer();
    }

    Old_OnBeforeClose(self, browser);
}

static decltype(cef_load_handler_t::on_load_start) Old_OnLoadStart;
static void CALLBACK Hooked_OnLoadStart(struct _cef_load_handler_t* self,
    struct _cef_browser_t* browser,
    struct _cef_frame_t* frame,
    cef_transition_type_t transition_type)
{
    Old_OnLoadStart(self, browser, frame, transition_type);
    if (!frame->is_main(frame)) return;

    // Patch once.
    static bool patched = false;
    if (patched || (patched = true, false)) return;

    SetUpBrowserWindow(browser, frame);
    OpenInternalServer();
};

static void HookClient(cef_client_t *client)
{
    // Hook LifeSpanHandler.
    static auto Old_GetLifeSpanHandler = client->get_life_span_handler;
    // Don't worry about calling convention here (stdcall).
    client->get_life_span_handler =  [](struct _cef_client_t* self) -> cef_life_span_handler_t*
    {
        auto handler = Old_GetLifeSpanHandler(self);

        // Hook OnAfterCreated().
        Old_OnAfterCreated = handler->on_after_created;
        handler->on_after_created = Hooked_OnAfterCreated;

        // Hook OnBeforeClose().
        Old_OnBeforeClose = handler->on_before_close;
        handler->on_before_close = Hooked_OnBeforeClose;

        return handler;
    };

    // Hook LoadHandler;
    static auto Old_GetLoadHandler = client->get_load_handler;
    client->get_load_handler = [](struct _cef_client_t* self)
    {
        auto handler = Old_GetLoadHandler(self);

        // Hook OnLoadStart().
        Old_OnLoadStart = handler->on_load_start;
        handler->on_load_start = Hooked_OnLoadStart;

        return handler;
    };

    // Hook RequestHandler.
    static auto Old_GetRequestHandler = client->get_request_handler;
    client->get_request_handler = [](struct _cef_client_t* self) -> cef_request_handler_t*
    {
        auto handler = Old_GetRequestHandler(self);

        static auto Old_GetResourceRequestHandler = handler->get_resource_request_handler;
        handler->get_resource_request_handler = [](
            struct _cef_request_handler_t* self,
            struct _cef_browser_t* browser,
            struct _cef_frame_t* frame,
            struct _cef_request_t* request,
            int is_navigation,
            int is_download,
            const cef_string_t* request_initiator,
            int* disable_default_handling) -> cef_resource_request_handler_t*
        {
            auto handler = Old_GetResourceRequestHandler(self, browser, frame, request,
                is_navigation, is_download, request_initiator, disable_default_handling);

            static auto Old_GetResourceHandler = handler->get_resource_handler;
            handler->get_resource_handler = [](
                struct _cef_resource_request_handler_t* self,
                struct _cef_browser_t* browser,
                struct _cef_frame_t* frame,
                struct _cef_request_t* request) -> cef_resource_handler_t*
            {
                CefScopedStr url{ request->get_url(request) };
                cef_resource_handler_t *handler = nullptr;

                if (wcsncmp(url.str, L"https://assets/", 15) == 0)
                    return CreateAssetsResourceHandler(url.str + 14, false);
                if (wcsncmp(url.str, L"https://plugins/", 16) == 0)
                    return CreateAssetsResourceHandler(url.str + 15, true);
                if (wcsncmp(url.str, L"https://riotclient/", 19) == 0)
                    return CreateRiotClientResourceHandler(frame, url.str + 18);

                return Old_GetResourceHandler(self, browser, frame, request);
            };

            return handler;
        };

        return handler;
    };

    static auto OnProcessMessageReceived = client->on_process_message_received;
    client->on_process_message_received = [](struct _cef_client_t* self,
        struct _cef_browser_t* browser,
        struct _cef_frame_t* frame,
        cef_process_id_t source_process,
        struct _cef_process_message_t* message)
    {
        if (source_process == PID_RENDERER)
        {
            CefScopedStr name{ message->get_name(message) };
            if (name == L"__OPEN_DEVTOOLS")
                OpenDevTools_Internal(false);
        }

        return OnProcessMessageReceived(self, browser, frame, source_process, message);
    };
}

static int Hooked_CefBrowserHost_CreateBrowser(
    const cef_window_info_t* windowInfo,
    struct _cef_client_t* client,
    const cef_string_t* url,
    const struct _cef_browser_settings_t* settings,
    struct _cef_dictionary_value_t* extra_info,
    struct _cef_request_context_t* request_context)
{
    // Hook main window only.
    if (utils::strContain(url->str, L"riot:") && utils::strContain(url->str, L"/bootstrap.html"))
    {
        // Create extra info if null.
        if (extra_info == NULL)
            extra_info = CefDictionaryValue_Create();

        // Add current process ID (browser process).
        extra_info->set_null(extra_info, &"is_main"_s);

        // Hook client.
        HookClient(client);
    }

    return CefBrowserHost_CreateBrowser(windowInfo, client, url, settings, extra_info, request_context);
}

static decltype(cef_app_t::on_before_command_line_processing) Old_OnBeforeCommandLineProcessing;
static void CEF_CALLBACK Hooked_OnBeforeCommandLineProcessing(
    struct _cef_app_t* self,
    const cef_string_t* process_type,
    struct _cef_command_line_t* command_line)
{
    CefScopedStr rc_port{ command_line->get_switch_value(command_line, &"riotclient-app-port"_s) };
    CefScopedStr rc_token{ command_line->get_switch_value(command_line, &"riotclient-auth-token"_s) };
    SetRiotClientCredentials(rc_port.cstr(), rc_token.cstr());

    // Keep Riot's command lines.
    Old_OnBeforeCommandLineProcessing(self, process_type, command_line);

    // Extract args string.
    auto args = CefScopedStr{ command_line->get_command_line_string(command_line) }.cstr();

    auto chromiumArgs = config::getConfigValue(L"ChromiumArgs");
    if (!chromiumArgs.empty())
        args += L" " + chromiumArgs;

    if (config::getConfigValue(L"NoProxyServer") == L"0")
    {
        size_t pos = args.find(L"--no-proxy-server");
        if (pos != std::wstring::npos)
            args.replace(pos, 17, L"");
    }

    // Rebuild it.
    command_line->reset(command_line);
    command_line->init_from_string(command_line, &CefStr(args));

    auto sPort = config::getConfigValue(L"RemoteDebuggingPort");
    REMOTE_DEBUGGING_PORT = wcstol(sPort.c_str(), NULL, 10);
    if (REMOTE_DEBUGGING_PORT != 0) {
        // Set remote debugging port.
        command_line->append_switch_with_value(command_line,
            &"remote-debugging-port"_s, &CefStr(std::to_string(REMOTE_DEBUGGING_PORT)));
    }

    if (config::getConfigValue(L"DisableWebSecurity") == L"1")
    {
        // Disable web security.
        command_line->append_switch(command_line, &"disable-web-security"_s);
    }

    if (config::getConfigValue(L"IgnoreCertificateErrors") == L"1")
    {
        // Ignore invalid certs.
        command_line->append_switch(command_line, &"ignore-certificate-errors"_s);
    }
}

static int Hooked_CefInitialize(const struct _cef_main_args_t* args,
    const struct _cef_settings_t* settings, cef_app_t* app, void* windows_sandbox_info)
{
    // Hook command line.
    Old_OnBeforeCommandLineProcessing = app->on_before_command_line_processing;
    app->on_before_command_line_processing = Hooked_OnBeforeCommandLineProcessing;

    return CefInitialize(args, settings, app, windows_sandbox_info);
}

static decltype(&CreateProcessW) Old_CreateProcessW = &CreateProcessW;
static BOOL WINAPI Hooked_CreateProcessW(
    _In_opt_ LPCWSTR lpApplicationName,
    _Inout_opt_ LPWSTR lpCommandLine,
    _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
    _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
    _In_ BOOL bInheritHandles,
    _In_ DWORD dwCreationFlags,
    _In_opt_ LPVOID lpEnvironment,
    _In_opt_ LPCWSTR lpCurrentDirectory,
    _In_ LPSTARTUPINFOW lpStartupInfo,
    _Out_ LPPROCESS_INFORMATION lpProcessInformation)
{
    bool shouldHook = utils::strContain(lpCommandLine, L"LeagueClientUxRender.exe", false)
        && utils::strContain(lpCommandLine, L"--type=renderer", false);

    if (shouldHook)
        dwCreationFlags |= CREATE_SUSPENDED;

    BOOL ret = Old_CreateProcessW(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
        bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);

    if (ret && shouldHook)
    {
        void InjectThisDll(HANDLE hProcess);
        InjectThisDll(lpProcessInformation->hProcess);
        ResumeThread(lpProcessInformation->hThread);
    }

    return ret;
}

void HookBrowserProcess()
{
    // Open console window.
#if _DEBUG
    AllocConsole();
    SetConsoleTitleA("League Client (browser process)");
    freopen("CONOUT$", "w", stdout);
#endif

    // Hook CefInitialize().
    utils::hookFunc(&CefInitialize, Hooked_CefInitialize);
    // Hook CefBrowserHost::CreateBrowser().
    utils::hookFunc(&CefBrowserHost_CreateBrowser, Hooked_CefBrowserHost_CreateBrowser);

    // Hook CreateProcessW().
    utils::hookFunc(&Old_CreateProcessW, Hooked_CreateProcessW);
}
