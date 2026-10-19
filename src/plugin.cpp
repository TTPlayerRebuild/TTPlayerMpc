#include "common.h"
namespace ttp::mpc {
HMODULE module{};
StandardContent standard_content() {
    for (auto name : {L"ttpctrl.dll", L"soundcore.dll", static_cast<const wchar_t *>(nullptr)})
        if (auto h = GetModuleHandleW(name))
            if (auto p = GetProcAddress(h, "CreateStdContent"))
                return reinterpret_cast<StandardContent>(p);
    return nullptr;
}
class Factory final : public ReaderCreator {
    LONG refs_{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **p) override {
        if (!p) return E_POINTER; *p=nullptr;
        if (!same(iid,IID_IUnknown) && !same(iid,cat_reader)) return E_NOINTERFACE;
        *p=static_cast<ReaderCreator*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release() override {auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Create(void **p) override {return make_reader(p);}
    HRESULT STDMETHODCALLTYPE Name(wchar_t **p) override {return text(L"MPC Reader",p);}
    HRESULT STDMETHODCALLTYPE Extensions(wchar_t **p) override {return text(L"Musepack Audio(*.mpc;*.mp+)",p);}
};
class Plugin final : public AddIn {
    LONG refs_{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **p) override {
        if(!p)return E_POINTER;*p=nullptr;
        if(!same(iid,IID_IUnknown)&&!same(iid,iid_addin))return E_NOINTERFACE;
        *p=static_cast<AddIn*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release() override {auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Enum(DWORD i,GUID *cat,void **p) override {
        if(!p)return E_POINTER;*p=nullptr;if(!cat)return E_POINTER;if(i)return E_INVALIDARG;
        return protect([&]{*cat=cat_reader;*p=static_cast<ReaderCreator*>(new Factory);return S_OK;});
    }
};
}
extern "C" HRESULT WINAPI ttpGetSoundAddIn(void **p) {
    if(!p)return E_POINTER;*p=nullptr;
    return ttp::mpc::protect([&]{*p=static_cast<ttp::mpc::AddIn*>(new ttp::mpc::Plugin);return S_OK;});
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){ttp::mpc::module=h;DisableThreadLibraryCalls(h);}return TRUE;
}
