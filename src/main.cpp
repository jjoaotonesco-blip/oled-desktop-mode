#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <vector>
#include <memory>
#include <atomic>
#include <set>
#include "SDK/ADLXHelper/Windows/Cpp/ADLXHelper.h"
#include "SDK/Include/IDisplays.h"
#include "SDK/Include/IDisplaySettings.h"
#include "state.h"

using namespace adlx;
namespace fs = std::filesystem;
constexpr UINT Reevaluate=WM_APP+1, ColorChanged=WM_APP+2, TopologyChanged=WM_APP+3;
constexpr UINT TrayMessage=WM_APP+4, GuardianLost=WM_APP+5;
constexpr UINT Toggle=100, Exit=101, Settings=102;
constexpr ULONG_PTR UiProtocol=0x4f4c4544;
HWND messageWindow=nullptr;
fs::path dataDir;
void log(const std::string& message) {
    std::ofstream out(dataDir/L"events.log",std::ios::app);
    out << GetTickCount64() << " " << message << '\n';
}
void require(ADLX_RESULT result, const char* operation) {
    if (!ADLX_SUCCEEDED(result)) throw std::runtime_error(std::string(operation)+": ADLX "+std::to_string(result));
}
using Get = ADLX_RESULT (ADLX_STD_CALL IADLXDisplayCustomColor::*)(adlx_int*);
using Set = ADLX_RESULT (ADLX_STD_CALL IADLXDisplayCustomColor::*)(adlx_int);
using Support = ADLX_RESULT (ADLX_STD_CALL IADLXDisplayCustomColor::*)(adlx_bool*);
using Range = ADLX_RESULT (ADLX_STD_CALL IADLXDisplayCustomColor::*)(ADLX_IntRange*);
const std::array<Get,5> getters{&IADLXDisplayCustomColor::GetBrightness,&IADLXDisplayCustomColor::GetContrast,
    &IADLXDisplayCustomColor::GetSaturation,&IADLXDisplayCustomColor::GetHue,&IADLXDisplayCustomColor::GetTemperature};
const std::array<Set,5> setters{&IADLXDisplayCustomColor::SetBrightness,&IADLXDisplayCustomColor::SetContrast,
    &IADLXDisplayCustomColor::SetSaturation,&IADLXDisplayCustomColor::SetHue,&IADLXDisplayCustomColor::SetTemperature};
const std::array<Support,5> supports{&IADLXDisplayCustomColor::IsBrightnessSupported,&IADLXDisplayCustomColor::IsContrastSupported,
    &IADLXDisplayCustomColor::IsSaturationSupported,&IADLXDisplayCustomColor::IsHueSupported,&IADLXDisplayCustomColor::IsTemperatureSupported};
const std::array<Range,5> ranges{&IADLXDisplayCustomColor::GetBrightnessRange,&IADLXDisplayCustomColor::GetContrastRange,
    &IADLXDisplayCustomColor::GetSaturationRange,&IADLXDisplayCustomColor::GetHueRange,&IADLXDisplayCustomColor::GetTemperatureRange};
const char* labels[]{"Brightness","Contrast","Saturation","Hue","ColorTemperature"};
struct Monitor {
    IADLXDisplayPtr display;
    IADLXDisplayCustomColorPtr color;
    std::string name;
    unsigned mask=0;
};
class Amd : public oled::Backend {
    ADLXHelper helper; // Declared first: destroyed after every SDK interface.
public:
    IADLXDisplayServicesPtr services;
    std::map<std::string,Monitor> monitors;
    Amd() {
        require(helper.Initialize(),"Initialize AMD ADLX (compatible AMD driver required)");
        require(helper.GetSystemServices()->GetDisplaysServices(&services),"GetDisplaysServices");
        enumerate();
    }
    void enumerate() {
        std::map<std::string,Monitor> next;
        IADLXDisplayListPtr list;
        require(services->GetDisplays(&list),"GetDisplays");
        for(adlx_uint i=list->Begin();i<list->End();++i) {
            Monitor m;
            if(!ADLX_SUCCEEDED(list->At(i,&m.display))) continue;
            if(!ADLX_SUCCEEDED(services->GetCustomColor(m.display,&m.color))) continue;
            const char *name=nullptr,*edid=nullptr,*pnp=nullptr;
            adlx_size uid=0;
            IADLXGPUPtr gpu;
            if(!ADLX_SUCCEEDED(m.display->Name(&name)) || !name ||
               !ADLX_SUCCEEDED(m.display->EDID(&edid)) || !edid || !*edid ||
               !ADLX_SUCCEEDED(m.display->UniqueId(&uid)) ||
               !ADLX_SUCCEEDED(m.display->GetGPU(&gpu)) ||
               !ADLX_SUCCEEDED(gpu->PNPString(&pnp)) || !pnp) continue;
            // Exact identity only. Never migrate backups to another monitor by display order.
            std::string key=std::string(pnp)+"|"+edid+"|"+std::to_string(uid);
            m.name=name;
            for(unsigned j=0;j<5;++j) {
                adlx_bool supported=false; ADLX_IntRange range{}; int value=0;
                if(ADLX_SUCCEEDED((m.color.operator->()->*supports[j])(&supported)) && supported &&
                   ADLX_SUCCEEDED((m.color.operator->()->*ranges[j])(&range)) &&
                   ADLX_SUCCEEDED((m.color.operator->()->*getters[j])(&value)) &&
                   oled::preset[j]>=range.minValue && oled::preset[j]<=range.maxValue &&
                   (range.step<=0 || (oled::preset[j]-range.minValue)%range.step==0)) m.mask|=1u<<j;
            }
            if(m.mask && !next.emplace(key,std::move(m)).second)
                throw std::runtime_error("Ambiguous monitor identity; refusing color writes");
        }
        monitors=std::move(next);
    }
    oled::Color read(const std::string& id,unsigned mask) override {
        auto& m=monitors.at(id); oled::Color result{};
        for(unsigned j=0;j<5;++j) if(mask&(1u<<j))
            require((m.color.operator->()->*getters[j])(&result[j]),labels[j]);
        return result;
    }
    bool write(const std::string& id,const oled::Color& values,unsigned mask) override {
        try {
            auto& m=monitors.at(id);
            auto current=read(id,mask);
            bool ok=true;
            for(unsigned j=0;j<5;++j) if((mask&(1u<<j)) && current[j]!=values[j]) {
                auto result=(m.color.operator->()->*setters[j])(values[j]);
                if(!ADLX_SUCCEEDED(result)) { log(m.name+": setter failed "+labels[j]+" "+std::to_string(result)); ok=false; }
            }
            return oled::same(read(id,mask),values,mask) && ok;
        } catch(const std::exception& e) { log(e.what()); return false; }
    }
    void diagnose() {
        std::cout << "ADLX initialized; eligible AMD displays: "<<monitors.size()<<"\n";
        for(const auto& [id,m]:monitors) {
            std::cout<<m.name<<" (ADLX supported preset mask="<<m.mask<<")\n";
            auto color=read(id,m.mask);
            for(unsigned j=0;j<5;++j) std::cout<<"  "<<labels[j]<<": "
                <<((m.mask&(1u<<j))?std::to_string(color[j]):"unsupported / preset out of range")<<"\n";
        }
    }
};

fs::path journalPath() { return dataDir/L"recovery.txt"; }
oled::Records loadJournal() {
    oled::Records records;
    if(!fs::exists(journalPath())) return records;
    std::ifstream in(journalPath());
    std::string magic; unsigned version=0;
    if(!(in>>magic>>version) || magic!="OLED_DESKTOP_JOURNAL" || version!=1)
        throw std::runtime_error("Invalid recovery journal; refusing to capture/alter AMD colors");
    while(in>>std::ws && in.peek()!=EOF) {
        std::string id; oled::Record r; int pending=-1;
        if(!(in>>std::quoted(id)>>r.mask>>pending) || id.empty() || id.size()>16384 || !r.mask || r.mask>31 || pending<0 || pending>1)
            throw std::runtime_error("Corrupt recovery journal");
        for(auto& value:r.normal) if(!(in>>value)) throw std::runtime_error("Truncated recovery journal");
        r.pending=pending!=0;
        if(!records.emplace(id,r).second || records.size()>128) throw std::runtime_error("Duplicate/oversized recovery journal");
    }
    return records;
}
void writeDurable(const fs::path& path,const std::string& bytes) {
    auto temp=path; temp+=L".tmp";
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create configuration file");
    DWORD written=0;
    bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if(!ok || !MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot durably commit configuration");
}
void saveJournal(const oled::Records& records) {
    std::ostringstream out;
    out<<"OLED_DESKTOP_JOURNAL 1\n";
    for(const auto& [id,r]:records) {
        out<<std::quoted(id)<<' '<<r.mask<<' '<<r.pending;
        for(auto value:r.normal) out<<' '<<value;
        out<<'\n';
    }
    writeDurable(journalPath(),out.str());
}
std::map<std::string,oled::Color> loadProfiles() {
    std::map<std::string,oled::Color> result;
    auto path=dataDir/L"profiles.txt";
    if(!fs::exists(path)) return result;
    std::ifstream in(path); std::string magic; int version=0;
    if(!(in>>magic>>version) || magic!="OLED_PROFILES" || version!=1) throw std::runtime_error("Invalid OLED profiles");
    while(in>>std::ws && in.peek()!=EOF) {
        std::string id; oled::Color color{};
        if(!(in>>std::quoted(id)) || id.empty() || id.size()>16384) throw std::runtime_error("Invalid OLED profile identity");
        for(auto& value:color) if(!(in>>value)) throw std::runtime_error("Invalid OLED profile values");
        if(!result.emplace(id,color).second || result.size()>128) throw std::runtime_error("Duplicate OLED profile");
    }
    return result;
}
void saveProfiles(const std::map<std::string,oled::Color>& profiles) {
    std::ostringstream out; out<<"OLED_PROFILES 1\n";
    for(const auto& [id,color]:profiles) {out<<std::quoted(id); for(int v:color) out<<' '<<v; out<<'\n';}
    writeDurable(dataDir/L"profiles.txt",out.str());
}
std::string jsonString(const std::string& value) {
    std::ostringstream out; out<<'"';
    for(unsigned char c:value) {
        if(c=='"' || c=='\\') out<<'\\'<<c;
        else if(c<32) out<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c)<<std::dec;
        else out<<c;
    }
    out<<'"'; return out.str();
}
std::string profileToken(const std::string& id) {
    constexpr char hex[]="0123456789abcdef";
    std::string token; for(unsigned char c:id) {token+=hex[c>>4];token+=hex[c&15];} return token;
}
void showSettings() {
    HWND existing=FindWindowW(nullptr,L"OLED Desktop Mode — Settings");
    if(!existing) existing=FindWindowW(nullptr,L"OLED Desktop Mode — Definições");
    if(existing) {ShowWindow(existing,SW_RESTORE); SetForegroundWindow(existing); return;}
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
    auto ui=fs::path(executable).parent_path()/L"ui"/L"OledSettings.exe";
    if(!fs::exists(ui)) throw std::runtime_error("Settings UI not built; run build.ps1");
    std::wstring command=L"\""+ui.wstring()+L"\"";
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    if(!CreateProcessW(ui.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,ui.parent_path().c_str(),&si,&pi))
        throw std::runtime_error("Cannot open settings UI");
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}

bool shellSurface(HWND window) {
    if(!window) return false;
    DWORD shellPid=0,pid=0;
    GetWindowThreadProcessId(GetShellWindow(),&shellPid); GetWindowThreadProcessId(window,&pid);
    if(!shellPid || pid!=shellPid) return false;
    wchar_t cls[128]{}; GetClassNameW(window,cls,128);
    return wcscmp(cls,L"Progman")==0 || wcscmp(cls,L"WorkerW")==0 ||
        wcscmp(cls,L"Shell_TrayWnd")==0 || wcscmp(cls,L"Shell_SecondaryTrayWnd")==0;
}
BOOL CALLBACK visibleApplication(HWND window,LPARAM param) {
    if(!IsWindowVisible(window) || IsIconic(window) || shellSurface(window)) return TRUE;
    // Layered + transparent tool windows are click-through overlays, not app surfaces.
    // Do not exempt a foreground window or a normal application just by its name.
    auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
    constexpr LONG_PTR overlay=WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW;
    if((style&overlay)==overlay && window!=GetForegroundWindow()) return TRUE;
    if(style&WS_EX_LAYERED) {
        BYTE alpha=255; COLORREF key=0; DWORD flags=0;
        if(GetLayeredWindowAttributes(window,&key,&alpha,&flags) && (flags&LWA_ALPHA) && alpha==0 && window!=GetForegroundWindow()) return TRUE;
    }
    DWORD cloaked=0;
    if(FAILED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))) {
        *reinterpret_cast<bool*>(param)=true; return FALSE; // Unknown => normal.
    }
    if(cloaked) return TRUE;
    RECT r{};
    if(!GetWindowRect(window,&r)) { *reinterpret_cast<bool*>(param)=true; return FALSE; }
    RECT desktop{GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),0,0}, overlap{};
    desktop.right=desktop.left+GetSystemMetrics(SM_CXVIRTUALSCREEN);
    desktop.bottom=desktop.top+GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(r.right>r.left && r.bottom>r.top && IntersectRect(&overlap,&r,&desktop)) {
        *reinterpret_cast<bool*>(param)=true; return FALSE;
    }
    return TRUE;
}
bool desktopVisible() {
    HDESK input=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    if(!input) return false;
    wchar_t name[128]{}; DWORD required=0;
    bool defaultDesktop=GetUserObjectInformationW(input,UOI_NAME,name,sizeof(name),&required) && _wcsicmp(name,L"Default")==0;
    CloseDesktop(input);
    if(!defaultDesktop || !shellSurface(GetForegroundWindow())) return false;
    bool covered=false;
    // EnumWindows returns FALSE when our callback stops because it found a blocker.
    // Distinguish that from a genuine enumeration failure.
    SetLastError(ERROR_SUCCESS);
    if(!EnumWindows(visibleApplication,reinterpret_cast<LPARAM>(&covered)) && !covered) return false;
    return !covered;
}
BOOL CALLBACK describeWindows(HWND window,LPARAM) {
    if(!IsWindowVisible(window) || IsIconic(window) || shellSurface(window)) return TRUE;
    DWORD cloaked=0; DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
    RECT r{}; GetWindowRect(window,&r);
    if(cloaked || r.right<=r.left || r.bottom<=r.top) return TRUE;
    wchar_t cls[128]{},title[256]{}; GetClassNameW(window,cls,128); GetWindowTextW(window,title,256);
    BYTE alpha=255; COLORREF key=0; DWORD flags=0;
    GetLayeredWindowAttributes(window,&key,&alpha,&flags);
    std::wcout<<L"  Visible window: "<<cls<<L" | "<<title<<L" rect="<<r.left<<L","<<r.top<<L","<<r.right<<L","<<r.bottom
        <<L" exStyle="<<GetWindowLongPtrW(window,GWL_EXSTYLE)<<L" alpha="<<unsigned(alpha)<<L" flags="<<flags<<L"\n";
    return TRUE;
}

class SettingsListener : public IADLXDisplaySettingsChangedListener {
public:
    adlx_bool ADLX_STD_CALL OnDisplaySettingsChanged(IADLXDisplaySettingsChangedEvent* event) override {
        if(event->GetOrigin()==SYNC_ORIGIN_EXTERNAL && (event->IsCustomColorBrightnessChanged() ||
            event->IsCustomColorContrastChanged() || event->IsCustomColorSaturationChanged() ||
            event->IsCustomColorHueChanged() || event->IsCustomColorTemperatureChanged()))
            PostMessageW(messageWindow,ColorChanged,0,0);
        return true;
    }
};
class ListListener : public IADLXDisplayListChangedListener {
public:
    adlx_bool ADLX_STD_CALL OnDisplayListChanged(IADLXDisplayList*) override {
        PostMessageW(messageWindow,TopologyChanged,0,0); return true;
    }
};
struct App {
    Amd amd;
    oled::Controller controller{amd,saveJournal};
    IADLXDisplayChangedHandlingPtr handling;
    SettingsListener settings;
    ListListener list;
    bool enabled=true,locked=false,oledMode=false,fault=false;
    std::vector<HWINEVENTHOOK> hooks;
    HANDLE guardian=nullptr,guardianWait=nullptr;
    NOTIFYICONDATAW tray{};
    UINT taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    bool restoreAll() {
        bool ok=true;
        for(auto& [id,r]:controller.records) if(r.pending) {
            if(!amd.monitors.count(id)) { ok=false; continue; }
            try { if(!controller.restore(id)) ok=false; }
            catch(const std::exception& e) { log(e.what()); ok=false; }
        }
        oledMode=false;
        if(!ok) SetTimer(messageWindow,2,2000,nullptr); else KillTimer(messageWindow,2);
        return ok;
    }
    void updateTray() {
        std::wstring status=fault?L"OLED: erro — recuperação pendente":(!enabled?L"OLED Desktop Mode: desativado":
            (oledMode?L"OLED Desktop Mode: OLED_DESKTOP":L"OLED Desktop Mode: NORMAL"));
        wcsncpy_s(tray.szTip,status.c_str(),_TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY,&tray);
    }
    void recoverAndCapture() {
        if(!restoreAll()) throw std::runtime_error("Unresolved monitor recovery; reconnect original monitor before enabling");
        // Fresh startup captures actual user settings. Resolved historical backups aren't reused.
        controller.records.clear();
        saveJournal(controller.records);
        for(const auto& [id,m]:amd.monitors) controller.capture(id,m.mask);
    }
    void evaluate() {
        bool guardAlive=guardian && WaitForSingleObject(guardian,0)==WAIT_TIMEOUT;
        bool desired=enabled && guardAlive && !locked && !fault && desktopVisible();
        if(desired==oledMode) return;
        if(!desired) { if(!restoreAll()) fault=true; }
        else {
            // Every current monitor must be captured before any hardware write.
            bool ok=true;
            for(const auto& [id,m]:amd.monitors) if(!controller.records.count(id)) controller.capture(id,m.mask);
            for(const auto& [id,m]:amd.monitors) {
                validateProfile(id,controller.target(id));
                if(!controller.enter(id)) { ok=false; break; }
            }
            if(!ok) { fault=true; restoreAll(); } else oledMode=true;
        }
        log(oledMode?"OLED_DESKTOP":"NORMAL"); updateTray();
    }
    void manualChanges() {
        if(oledMode || fault) return; // Ignore own asynchronous echoes / edits during OLED.
        for(const auto& [id,m]:amd.monitors) if(controller.records.count(id)) controller.observeNormal(id);
    }
    void topology() {
        restoreAll();
        amd.enumerate();
        if(!restoreAll()) { fault=true; updateTray(); return; }
        for(auto it=controller.records.begin();it!=controller.records.end();)
            if(!amd.monitors.count(it->first) && !it->second.pending) it=controller.records.erase(it); else ++it;
        saveJournal(controller.records);
        for(const auto& [id,m]:amd.monitors) controller.capture(id,m.mask);
        fault=false; evaluate(); updateTray();
    }
    void validateProfile(const std::string& id,const oled::Color& color) {
        auto& m=amd.monitors.at(id);
        for(unsigned j=0;j<5;++j) if(m.mask&(1u<<j)) {
            ADLX_IntRange range{};
            require((m.color.operator->()->*ranges[j])(&range),"OLED profile range");
            if(color[j]<range.minValue || color[j]>range.maxValue ||
               (range.step>0 && (color[j]-range.minValue)%range.step!=0))
                throw std::runtime_error(std::string("Value outside supported range: ")+labels[j]);
        }
    }
    std::string uiCommand(const std::string& command) {
        std::istringstream in(command); std::string verb; in>>verb;
        if(verb=="LIST") {
            manualChanges();
            std::ostringstream out;
            out<<"{\"ok\":true,\"enabled\":"<<(enabled?"true":"false")<<",\"fault\":"<<(fault?"true":"false")<<",\"monitors\":[";
            bool first=true;
            for(const auto& [id,m]:amd.monitors) {
                if(!first) out<<','; first=false;
                out<<"{\"id\":"<<jsonString(profileToken(id))<<",\"name\":"<<jsonString(m.name)<<",\"mask\":"<<m.mask;
                auto values=controller.target(id); auto normal=controller.records.at(id).normal;
                out<<",\"values\":["; for(unsigned j=0;j<5;++j) out<<(j?",":"")<<values[j]; out<<']';
                out<<",\"normal\":["; for(unsigned j=0;j<5;++j) out<<(j?",":"")<<normal[j]; out<<']';
                out<<",\"ranges\":[";
                for(unsigned j=0;j<5;++j) {
                    ADLX_IntRange range{};
                    if(m.mask&(1u<<j)) require((m.color.operator->()->*ranges[j])(&range),"UI range");
                    out<<(j?",":"")<<"["<<range.minValue<<','<<range.maxValue<<','<<std::max(1,range.step)<<']';
                }
                out<<"]}";
            }
            out<<"]}"; return out.str();
        }
        if(verb=="SAVE") {
            std::string token; oled::Color values{};
            if(!(in>>token)) throw std::runtime_error("Missing monitor");
            for(auto& v:values) if(!(in>>v)) throw std::runtime_error("Expected five integers");
            in>>std::ws; if(!in.eof()) throw std::runtime_error("Unexpected profile data");
            for(const auto& [id,m]:amd.monitors) if(profileToken(id)==token) {
                validateProfile(id,values);
                if(!restoreAll()) throw std::runtime_error("Recovery pending; profile cannot be changed");
                auto next=controller.profiles; next[id]=values;
                saveProfiles(next); controller.profiles=std::move(next);
                updateTray(); return "{\"ok\":true}";
            }
            throw std::runtime_error("Monitor disconnected; refresh the list");
        }
        if(verb=="ENABLE") {
            int value=-1; if(!(in>>value) || (value!=0 && value!=1)) throw std::runtime_error("Invalid enable value");
            if(value && (fault || !guardian || WaitForSingleObject(guardian,0)!=WAIT_TIMEOUT))
                throw std::runtime_error("Recovery guardian unavailable; restart the application");
            enabled=value!=0; evaluate(); updateTray(); return "{\"ok\":true}";
        }
        if(verb=="EXIT") {PostMessageW(messageWindow,WM_CLOSE,0,0); return "{\"ok\":true}";}
        throw std::runtime_error("Unknown UI command");
    }
    ~App() {
        for(auto hook:hooks) UnhookWinEvent(hook);
        if(handling) { handling->RemoveDisplaySettingsEventListener(&settings); handling->RemoveDisplayListEventListener(&list); }
        restoreAll();
        if(guardianWait) UnregisterWaitEx(guardianWait,INVALID_HANDLE_VALUE);
        if(guardian) CloseHandle(guardian);
        Shell_NotifyIconW(NIM_DELETE,&tray);
        appCleanup();
    }
    void appCleanup();
};
App* app=nullptr;
void App::appCleanup() { if(app==this) app=nullptr; }
void CALLBACK winEvent(HWINEVENTHOOK,DWORD event,HWND,LONG object,LONG,DWORD,DWORD) {
    if(event>=EVENT_OBJECT_CREATE && object!=OBJID_WINDOW) return;
    PostMessageW(messageWindow,Reevaluate,0,0);
}
BOOL WINAPI consoleHandler(DWORD) { PostMessageW(messageWindow,WM_CLOSE,0,0); return TRUE; }
void CALLBACK guardianEnded(void*,BOOLEAN) { PostMessageW(messageWindow,GuardianLost,0,0); }
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(!app) return DefWindowProcW(window,message,w,l);
    try {
        if(message==app->taskbarCreated) { Shell_NotifyIconW(NIM_ADD,&app->tray); return 0; }
        switch(message) {
        case WM_COPYDATA: {
            auto* data=reinterpret_cast<COPYDATASTRUCT*>(l);
            if(!data || data->dwData!=UiProtocol || !data->lpData || data->cbData<1 || data->cbData>65536 || !IsWindow(reinterpret_cast<HWND>(w))) return FALSE;
            const char* bytes=static_cast<const char*>(data->lpData);
            if(bytes[data->cbData-1]!='\0') return FALSE;
            std::string response;
            try {response=app->uiCommand(std::string(bytes,data->cbData-1));}
            catch(const std::exception& e) {log(e.what()); response="{\"ok\":false,\"error\":"+jsonString(e.what())+"}";}
            COPYDATASTRUCT reply{UiProtocol,static_cast<DWORD>(response.size()+1),response.data()};
            DWORD_PTR result=0;
            return SendMessageTimeoutW(reinterpret_cast<HWND>(w),WM_COPYDATA,reinterpret_cast<WPARAM>(window),reinterpret_cast<LPARAM>(&reply),SMTO_ABORTIFHUNG,3000,&result)!=0;
        }
        case Reevaluate:
            // Restore immediately. Only entry is coalesced for Explorer's Win+D animation.
            if(app->oledMode && !desktopVisible()) app->evaluate();
            SetTimer(window,1,35,nullptr); return 0;
        case ColorChanged: app->manualChanges(); return 0;
        case TopologyChanged: SetTimer(window,3,150,nullptr); return 0;
        case WM_DISPLAYCHANGE: PostMessageW(window,TopologyChanged,0,0); return 0;
        case WM_TIMER:
            KillTimer(window,w);
            if(w==1) app->evaluate();
            if(w==2) { if(app->restoreAll()) {app->fault=false; app->updateTray();} }
            if(w==3) app->topology();
            return 0;
        case WM_WTSSESSION_CHANGE:
            if(w==WTS_SESSION_LOCK || w==WTS_CONSOLE_DISCONNECT || w==WTS_REMOTE_DISCONNECT) {
                app->locked=true; app->evaluate();
            } else if(w==WTS_SESSION_UNLOCK || w==WTS_CONSOLE_CONNECT || w==WTS_REMOTE_CONNECT) {
                app->locked=false; app->evaluate();
            } return 0;
        case WM_POWERBROADCAST:
            if(w==PBT_APMSUSPEND) { app->locked=true; app->evaluate(); }
            if(w==PBT_APMRESUMEAUTOMATIC) { app->locked=false; PostMessageW(window,TopologyChanged,0,0); }
            return TRUE;
        case WM_QUERYENDSESSION: app->enabled=false; app->restoreAll(); return TRUE;
        case WM_ENDSESSION: if(w) PostMessageW(window,WM_CLOSE,0,0); return 0;
        case GuardianLost:
            app->enabled=false; app->fault=true; app->restoreAll(); app->updateTray();
            log("Guardian exited unexpectedly; mode disabled"); return 0;
        case TrayMessage:
            if(l==WM_LBUTTONUP || l==WM_LBUTTONDBLCLK) {app->restoreAll(); showSettings(); return 0;}
            if(l==WM_RBUTTONUP || l==WM_CONTEXTMENU) {
                HMENU menu=CreatePopupMenu();
                std::ifstream languageFile(dataDir/L"language.txt");
                std::string language; languageFile>>language;
                const bool en=language=="en", es=language=="es";
                AppendMenuW(menu,MF_STRING,Settings,en?L"OLED settings":es?L"Ajustes del modo OLED":L"Definições do modo OLED");
                AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
                AppendMenuW(menu,MF_STRING|(app->enabled?MF_CHECKED:0),Toggle,en?L"Enable OLED Desktop Mode":es?L"Activar OLED Desktop Mode":L"Ativar OLED Desktop Mode");
                AppendMenuW(menu,MF_STRING,Exit,en?L"Exit and restore colors":es?L"Salir y restaurar colores":L"Sair e restaurar cores");
                POINT pt{}; GetCursorPos(&pt); SetForegroundWindow(window);
                auto action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,window,nullptr);
                DestroyMenu(menu);
                if(action==Exit) PostMessageW(window,WM_CLOSE,0,0);
                if(action==Settings) {app->restoreAll(); showSettings();}
                if(action==Toggle) { app->enabled=!app->enabled; app->evaluate(); app->updateTray(); }
            } return 0;
        case WM_CLOSE: app->enabled=false; app->restoreAll(); DestroyWindow(window); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    } catch(const std::exception& e) {
        log(e.what()); app->fault=true; app->enabled=false; app->restoreAll(); app->updateTray();
    }
    return DefWindowProcW(window,message,w,l);
}
std::wstring mutexName() {
    // Stable per user/path; local session scope permits distinct signed-in sessions.
    uint64_t hash=14695981039346656037ull;
    for(wchar_t c:dataDir.wstring()) {hash^=c; hash*=1099511628211ull;}
    return L"Local\\OledDesktopMode-"+std::to_wstring(hash);
}
HANDLE startGuardian() {
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,GetCurrentProcessId());
    HANDLE inheritedParent=nullptr;
    if(!parent || !DuplicateHandle(GetCurrentProcess(),parent,GetCurrentProcess(),&inheritedParent,SYNCHRONIZE,TRUE,0))
        throw std::runtime_error("Cannot create guardian process handle");
    CloseHandle(parent);
    HANDLE ready=CreateEventW(&sa,TRUE,FALSE,nullptr);
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
    std::wstring command=L"\""+std::wstring(executable)+L"\" --guard "+
        std::to_wstring(reinterpret_cast<uintptr_t>(inheritedParent))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(ready));
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    bool launched=ready && CreateProcessW(executable,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
    CloseHandle(inheritedParent);
    if(!launched) { if(ready) CloseHandle(ready); throw std::runtime_error("Cannot launch recovery guardian"); }
    CloseHandle(pi.hThread);
    HANDLE waits[]{ready,pi.hProcess};
    DWORD result=WaitForMultipleObjects(2,waits,FALSE,10000);
    CloseHandle(ready);
    if(result!=WAIT_OBJECT_0) { CloseHandle(pi.hProcess); throw std::runtime_error("Guardian not ready; colors untouched"); }
    return pi.hProcess;
}
int guard(HANDLE parent,HANDLE ready) {
    if(!parent || !ready || WaitForSingleObject(parent,0)!=WAIT_TIMEOUT) return 2;
    SetEvent(ready); CloseHandle(ready);
    if(WaitForSingleObject(parent,INFINITE)!=WAIT_OBJECT_0) return 2;
    CloseHandle(parent);
    HANDLE lock=CreateMutexW(nullptr,FALSE,mutexName().c_str());
    if(!lock) return 2;
    auto wait=WaitForSingleObject(lock,15000);
    if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED) {CloseHandle(lock); return 2;}
    int result=0;
    try {
        auto records=loadJournal();
        bool pending=false; for(const auto& [id,r]:records) pending|=r.pending;
        if(pending) {
            Amd amd; oled::Controller c(amd,saveJournal); c.records=records;
            for(const auto& [id,r]:c.records) if(r.pending) {
                if(!amd.monitors.count(id) || !c.restore(id)) result=3;
            }
            log(result?"Guardian: recovery incomplete, journal retained":"Guardian: normal colors restored");
        }
    } catch(const std::exception& e) {log(e.what()); result=3;}
    ReleaseMutex(lock); CloseHandle(lock); return result;
}
int main(int argc,char** argv) {
    wchar_t local[MAX_PATH]{};
    if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local))) return 2;
    dataDir=fs::path(local)/L"OledDesktopMode";
    try { fs::create_directories(dataDir); }
    catch(...) { std::cerr<<"Cannot create state directory\n"; return 2; }
    if(argc==4 && std::string(argv[1])=="--guard") {
        try {return guard(reinterpret_cast<HANDLE>(std::stoull(argv[2])),reinterpret_cast<HANDLE>(std::stoull(argv[3])));}
        catch(...) {return 2;}
    }
    if(argc==2 && std::string(argv[1])=="--diagnose") {
        try {Amd amd; amd.diagnose(); return 0;}
        catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 2;}
    }
    if(argc==2 && std::string(argv[1])=="--inspect-desktop") {
        std::cout << (desktopVisible()?"OLED_DESKTOP":"NORMAL") << '\n';
        wchar_t cls[128]{}; GetClassNameW(GetForegroundWindow(),cls,128);
        std::wcout<<L"Foreground class: "<<cls<<L"\n";
        EnumWindows(describeWindows,0); return 0;
    }
    bool background=argc==2 && std::string(argv[1])=="--background";
    bool crashTest=argc==2 && std::string(argv[1])=="--verify-crash";
    bool hardwareTest=crashTest || (argc==2 && std::string(argv[1])=="--verify-hardware");
    if(argc>1 && !hardwareTest && !background) {std::cerr<<"Usage: OledDesktopMode [--background | --diagnose | --inspect-desktop | --verify-hardware | --verify-crash]\n"; return 2;}
    HANDLE lock=CreateMutexW(nullptr,FALSE,mutexName().c_str());
    if(!lock) return 2;
    auto wait=WaitForSingleObject(lock,0);
    if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED) {
        if(!background && !hardwareTest) {try {showSettings();} catch(...) {}}
        CloseHandle(lock); return 1;
    }
    int result=0;
    try {
        WNDCLASSW cls{}; cls.lpfnWndProc=windowProc; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"OledDesktopModeEvents";
        if(!RegisterClassW(&cls)) throw std::runtime_error("Cannot register event window");
        // Hidden top-level window receives session/power/display/shutdown broadcasts.
        messageWindow=CreateWindowW(cls.lpszClassName,L"OLED Desktop Mode",WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,cls.hInstance,nullptr);
        if(!messageWindow) throw std::runtime_error("Cannot create event window");
        App instance; app=&instance;
        instance.controller.records=loadJournal();
        instance.recoverAndCapture();
        if(!hardwareTest) instance.controller.profiles=loadProfiles();
        if(instance.amd.monitors.empty()) throw std::runtime_error("No eligible AMD Custom Color display");
        instance.guardian=startGuardian();
        if(!RegisterWaitForSingleObject(&instance.guardianWait,instance.guardian,guardianEnded,nullptr,INFINITE,WT_EXECUTEONLYONCE))
            throw std::runtime_error("Cannot monitor guardian");
        if(hardwareTest) {
            std::cout<<"Guarded hardware test: applying preset once, then restoring original colors\n";
            for(const auto& [id,m]:instance.amd.monitors) {
                auto original=instance.controller.records.at(id).normal;
                if(!instance.controller.enter(id)) throw std::runtime_error("Hardware preset failed (rollback attempted)");
                if(crashTest) {
                    log("Intentional crash test: guardian must restore NormalState");
                    TerminateProcess(GetCurrentProcess(),99);
                }
                auto actual=instance.amd.read(id,m.mask);
                if(!instance.controller.restore(id)) throw std::runtime_error("Hardware restoration failed; journal retained");
                if(!oled::same(actual,oled::preset,m.mask) || !oled::same(instance.amd.read(id,m.mask),original,m.mask))
                    throw std::runtime_error("Hardware readback mismatch");
                std::cout<<m.name<<": all supported preset setters and exact restoration PASS\n";
                for(unsigned j=0;j<5;++j) if(m.mask&(1u<<j)) {
                    auto& record=instance.controller.records.at(id);
                    record.mask=1u<<j;
                    auto expected=original; expected[j]=oled::preset[j];
                    if(expected[j]==original[j]) {
                        // Hue may already be zero: test a real setter, not just a no-op.
                        ADLX_IntRange range{};
                        require((m.color.operator->()->*ranges[j])(&range),"Individual test range");
                        int step=range.step>0?range.step:1;
                        if(original[j]+step<=range.maxValue) expected[j]=original[j]+step;
                        else if(original[j]-step>=range.minValue) expected[j]=original[j]-step;
                        else throw std::runtime_error("Property has no alternate value for isolated setter test");
                        record.pending=true; saveJournal(instance.controller.records);
                        if(!instance.amd.write(id,expected,record.mask)) throw std::runtime_error("Individual alternate setter failed");
                    } else if(!instance.controller.enter(id)) throw std::runtime_error("Individual setter failed");
                    bool isolated=oled::same(instance.amd.read(id,m.mask),expected,m.mask);
                    if(!instance.controller.restore(id)) throw std::runtime_error("Individual property restoration failed");
                    record.mask=m.mask; saveJournal(instance.controller.records);
                    if(!isolated) throw std::runtime_error("Setter changed another Custom Color property");
                    std::cout<<"  "<<labels[j]<<": isolated setter PASS\n";
                }
            }
            app=nullptr;
        } else {
        require(instance.amd.services->GetDisplayChangedHandling(&instance.handling),"Display event handling");
        require(instance.handling->AddDisplaySettingsEventListener(&instance.settings),"Custom Color events");
        require(instance.handling->AddDisplayListEventListener(&instance.list),"Display list events");
        const std::pair<DWORD,DWORD> eventRanges[]{
            {EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND},{EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZEEND},
            {EVENT_SYSTEM_DESKTOPSWITCH,EVENT_SYSTEM_DESKTOPSWITCH},{EVENT_OBJECT_CREATE,EVENT_OBJECT_HIDE},
            {EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE}};
        for(auto [first,last]:eventRanges) {
            auto hook=SetWinEventHook(first,last,nullptr,winEvent,0,0,WINEVENT_OUTOFCONTEXT);
            if(!hook) throw std::runtime_error("Cannot register Windows desktop events");
            instance.hooks.push_back(hook);
        }
        if(!WTSRegisterSessionNotification(messageWindow,NOTIFY_FOR_THIS_SESSION)) throw std::runtime_error("Cannot register session events");
        instance.tray.cbSize=sizeof(instance.tray); instance.tray.hWnd=messageWindow; instance.tray.uID=1;
        instance.tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; instance.tray.uCallbackMessage=TrayMessage;
        instance.tray.hIcon=static_cast<HICON>(LoadImageW(cls.hInstance,MAKEINTRESOURCEW(101),IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
        if(!instance.tray.hIcon) throw std::runtime_error("Cannot load OLED notification icon");
        wcscpy_s(instance.tray.szTip,L"OLED Desktop Mode");
        if(!Shell_NotifyIconW(NIM_ADD,&instance.tray)) throw std::runtime_error("Cannot add tray icon");
        SetConsoleCtrlHandler(consoleHandler,TRUE);
        FreeConsole();
        if(!background) showSettings();
        instance.evaluate();
        MSG msg{}; int status;
        while((status=GetMessageW(&msg,nullptr,0,0))>0) {TranslateMessage(&msg); DispatchMessageW(&msg);}
        if(status<0) result=2;
        WTSUnRegisterSessionNotification(messageWindow);
        }
        // instance restores before the session mutex is released, guardian waits for process exit.
    } catch(const std::exception& e) {
        log(e.what()); std::cerr<<e.what()<<'\n';
        std::wstring message; for(unsigned char c:std::string(e.what())) message+=static_cast<wchar_t>(c);
        MessageBoxW(nullptr,message.c_str(),L"OLED Desktop Mode — erro",MB_OK|MB_ICONERROR); result=2;
    }
    app=nullptr; ReleaseMutex(lock); CloseHandle(lock); return result;
}
