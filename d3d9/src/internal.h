#ifndef _LEAGUE_LOADER_H
#define _LEAGUE_LOADER_H

#ifdef _WIN64
#error "Build 32-bit only."
#endif

#ifdef _MSC_VER
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifndef NOINLINE
#define NOINLINE __declspec(noinline)
#endif

#ifndef COUNT_OF
#define COUNT_OF(arr) (sizeof(arr) / sizeof(*arr))
#endif

#include <stdint.h>
#include <stdio.h>
#include <atomic>
#include <string>
#include <vector>
#include <functional>
#include <windows.h>

#include "include/internal/cef_string.h"
#include "include/capi/cef_parser_capi.h"
#include "include/capi/cef_base_capi.h"
#include "include/capi/cef_app_capi.h"
#include "include/capi/cef_browser_capi.h"
#include "include/capi/cef_v8_capi.h"
#include "include/capi/cef_request_capi.h"

using std::string;
using std::wstring;
using std::vector;

template <typename T>
class CefRefCount : public T
{
public:
    template <typename U>
    CefRefCount(const U *) : ref_(1) {
        base.size = sizeof(U);
        base.add_ref = _Base_AddRef;
        base.release = _Base_Release;
        base.has_one_ref = _Base_HasOneRef;
        base.has_at_least_one_ref = _Base_HasAtLeastOneRef;
        self_delete_ = [](void *self) { delete static_cast<U *>(self); };
    }

private:
    void(*self_delete_)(void *);
    std::atomic<size_t> ref_;

    static void CALLBACK _Base_AddRef(cef_base_ref_counted_t *_)
    { ++reinterpret_cast<CefRefCount *>(_)->ref_; }

    static int CALLBACK _Base_Release(cef_base_ref_counted_t *_)
    { return !--reinterpret_cast<CefRefCount *>(_)->ref_ ?
        (reinterpret_cast<CefRefCount *>(_)->self_delete_(_), 1) : 0; }

    static int CALLBACK _Base_HasOneRef(cef_base_ref_counted_t *_)
    { return reinterpret_cast<CefRefCount *>(_)->ref_ == 1; }

    static int CALLBACK _Base_HasAtLeastOneRef(cef_base_ref_counted_t *_)
    { return reinterpret_cast<CefRefCount *>(_)->ref_ != 0; }
};

struct CefStrBase : cef_string_t
{
    CefStrBase() { dtor = nullptr; }

    bool empty() const;
    bool equal(const wchar_t *s) const;
    bool equal(const wstring &s) const;
    bool equali(const wchar_t *s) const;
    bool equali(const wstring &s) const;
    bool contain(const wchar_t *s) const;
    bool contain(const wstring &s) const;
    bool operator ==(const wchar_t *s) const;

    wstring cstr() const {
        return wstring{ str, length };
    }
};

// cef_string_t wrapper.
struct CefStr : CefStrBase
{
    CefStr(const char *s, size_t l);
    CefStr(const wchar_t *s, size_t l);
    CefStr(const string &s);
    CefStr(const wstring &s);
    ~CefStr();

    CefStr &forawrd();

private:
    bool owner_;
};

struct CefScopedStr : CefStrBase
{
    explicit CefScopedStr(cef_string_userfree_t uf);
    ~CefScopedStr();

    const cef_string_t *get() const {
        return str_;
    }

private:
    cef_string_userfree_t str_;
};

// CEF functions.
extern decltype(&cef_get_mime_type) CefGetMimeType;
extern decltype(&cef_request_create) CefRequest_Create;
extern decltype(&cef_string_multimap_alloc) CefStringMultimap_Alloc;
extern decltype(&cef_string_multimap_free) CefStringMultimap_Free;
extern decltype(&cef_register_extension) CefRegisterExtension;
extern decltype(&cef_dictionary_value_create) CefDictionaryValue_Create;
extern decltype(&cef_stream_reader_create_for_file) CefStreamReader_CreateForFile;
extern decltype(&cef_stream_reader_create_for_data) CefStreamReader_CreateForData;
extern decltype(&cef_process_message_create) CefProcessMessage_Create;
extern decltype(&cef_v8context_get_current_context) CefV8Context_GetCurrentContext;

// Strings helpers.
extern decltype(&cef_string_set) CefString_Set;
extern decltype(&cef_string_clear) CefString_Clear;
extern decltype(&cef_string_from_utf8) CefString_FromUtf8;
extern decltype(&cef_string_from_wide) CefString_FromWide;
extern decltype(&cef_string_userfree_free) CefString_UserFree_Free;
extern decltype(&cef_string_to_utf8) CefString_ToUtf8;
extern decltype(&cef_string_utf8_clear) CefString_ClearUtf8;

// V8 values.
extern decltype(&cef_v8value_create_null) CefV8Value_CreateNull;
extern decltype(&cef_v8value_create_int) CefV8Value_CreateInt;
extern decltype(&cef_v8value_create_string) CefV8Value_CreateString;
extern decltype(&cef_v8value_create_function) CefV8Value_CreateFunction;
extern decltype(&cef_v8value_create_array) CefV8Value_CreateArray;
extern decltype(&cef_v8value_create_bool) CefV8Value_CreateBool;

// Hooking entries.
extern decltype(&cef_initialize) CefInitialize;
extern decltype(&cef_execute_process) CefExecuteProcess;
extern decltype(&cef_browser_host_create_browser) CefBrowserHost_CreateBrowser;

static CefStr operator""_s(const char *s, size_t l)
{
    return CefStr(s, l);
}

namespace config
{
    wstring getLoaderDir();
    wstring getAssetsDir();
    wstring getPluginsDir();
    wstring getConfigValue(const wstring &key);
}

namespace utils
{
    wstring toWide(const string &str);
    string toNarrow(const wstring &wstr);
    wstring encodeBase64(const wstring &str);

    bool strEqual(const wstring &a, const wstring &b, bool sensitive = true);
    bool strContain(const wstring &str, const wstring &sub, bool sensitive = true);
    bool strStartWith(const wstring &str, const wstring &sub);
    bool strEndWith(const wstring &str, const wstring &sub);

    bool dirExist(const wstring &path);
    bool fileExist(const wstring &path);
    void readDir(const std::wstring &dir, const std::function<void(const wstring &, bool)> &callback);
    bool readFile(const wstring &path, string &out);

    void hookFunc(void **orig, void *hooked);
    void *scanInternal(void *image, size_t length, const string &pattern);

    void openFilesExplorer(const wstring &path);
}

#endif