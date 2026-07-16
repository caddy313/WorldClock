#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <wtypes.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <string>
#include <vector>
#include "../resources/resource.h"

using namespace Gdiplus;

namespace {
constexpr wchar_t APP[] = L"世界时钟";
constexpr wchar_t WIDGET_CLASS[] = L"WorldClockCppWidget";
constexpr wchar_t SETTINGS_CLASS[] = L"WorldClockCppSettings";
constexpr int MAX_CLOCKS = 10;

struct Clock { std::wstring label, zone; };
struct Config {
    int x=80, y=80, size=75, width=100, spacing=100;
    bool topmost=true, locked=true, startup=true;
    COLORREF outline=RGB(0,0,0);
    std::vector<Clock> clocks{{L"温哥华",L"Pacific Standard Time"},{L"巴塞尔",L"W. Europe Standard Time"},{L"本地时间",L"China Standard Time"}};
} cfg;

HINSTANCE inst{}; HWND widget{}, settings{}; ULONG_PTR gdip{};
std::filesystem::path ini;
std::vector<DYNAMIC_TIME_ZONE_INFORMATION> zones;
float dpi=1, userScale=.75f;
int ww=405, wh=0, header=26, cardH=78, gap=6, pad=12, lockedPad=14;
bool dragging=false; POINT dragOffset{};
std::array<HWND,MAX_CLOCKS> enabled{}, labels{}, zoneBoxes{};
HWND lockedBox{}, topmostBox{}, startupBox{}, sizeEdit{}, widthEdit{}, spacingEdit{}, colorEdit{};

int S(float n){ return (int)std::lround(n*dpi*userScale); }
int D(float n){ return (int)std::lround(n*dpi); }

std::wstring readIni(const wchar_t* section,const wchar_t* key,const wchar_t* fallback){
    std::array<wchar_t,1024> out{};
    GetPrivateProfileStringW(section,key,fallback,out.data(),(DWORD)out.size(),ini.c_str());
    return out.data();
}
int readInt(const wchar_t* s,const wchar_t* k,int fallback){ return GetPrivateProfileIntW(s,k,fallback,ini.c_str()); }
void writeIni(const wchar_t* s,const wchar_t* k,const std::wstring& v){ WritePrivateProfileStringW(s,k,v.c_str(),ini.c_str()); }
std::wstring colorText(COLORREF c){ return std::format(L"#{:02X}{:02X}{:02X}",GetRValue(c),GetGValue(c),GetBValue(c)); }
bool parseColor(const std::wstring& s,COLORREF& c){
    if(s.size()!=7||s[0]!=L'#') return false; wchar_t* end{}; unsigned long v=wcstoul(s.c_str()+1,&end,16);
    if(!end||*end) return false; c=RGB((v>>16)&255,(v>>8)&255,v&255); return true;
}

void loadConfig(){
    PWSTR appdata{};
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,KF_FLAG_CREATE,nullptr,&appdata))){ ini=std::filesystem::path(appdata)/L"TimezoneWidget"/L"config.ini"; CoTaskMemFree(appdata); }
    else ini=L"config.ini";
    std::error_code ec; std::filesystem::create_directories(ini.parent_path(),ec);
    cfg.x=readInt(L"Window",L"X",80); cfg.y=readInt(L"Window",L"Y",80);
    cfg.topmost=readInt(L"Window",L"Topmost",1)!=0; cfg.locked=readInt(L"Window",L"Locked",1)!=0; cfg.startup=readInt(L"Window",L"Startup",1)!=0;
    cfg.size=std::clamp(readInt(L"Window",L"Size",75),50,150); cfg.width=std::clamp(readInt(L"Window",L"Width",100),70,150); cfg.spacing=std::clamp(readInt(L"Window",L"Spacing",100),70,120);
    parseColor(readIni(L"Window",L"Outline",L"#000000"),cfg.outline);
    int count=std::clamp(readInt(L"Clocks",L"Count",0),0,MAX_CLOCKS);
    if(count){ cfg.clocks.clear(); for(int i=1;i<=count;i++){ auto sec=std::format(L"Clock{}",i); auto l=readIni(sec.c_str(),L"Label",L""); auto z=readIni(sec.c_str(),L"Timezone",L""); if(!l.empty()&&!z.empty()) cfg.clocks.push_back({l,z}); } }
    if(cfg.clocks.empty()) cfg.clocks={{L"温哥华",L"Pacific Standard Time"},{L"巴塞尔",L"W. Europe Standard Time"},{L"本地时间",L"China Standard Time"}};
}
void saveConfig(){
    writeIni(L"Window",L"X",std::to_wstring(cfg.x)); writeIni(L"Window",L"Y",std::to_wstring(cfg.y));
    writeIni(L"Window",L"Topmost",cfg.topmost?L"1":L"0"); writeIni(L"Window",L"Locked",cfg.locked?L"1":L"0"); writeIni(L"Window",L"Startup",cfg.startup?L"1":L"0");
    writeIni(L"Window",L"Size",std::to_wstring(cfg.size)); writeIni(L"Window",L"Width",std::to_wstring(cfg.width)); writeIni(L"Window",L"Spacing",std::to_wstring(cfg.spacing)); writeIni(L"Window",L"Outline",colorText(cfg.outline));
    writeIni(L"Clocks",L"Count",std::to_wstring(cfg.clocks.size()));
    for(size_t i=0;i<cfg.clocks.size();i++){ auto sec=std::format(L"Clock{}",i+1); writeIni(sec.c_str(),L"Label",cfg.clocks[i].label); writeIni(sec.c_str(),L"Timezone",cfg.clocks[i].zone); }
}
void syncStartup(){
    HKEY key{}; if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return;
    if(cfg.startup){ std::array<wchar_t,MAX_PATH> p{}; GetModuleFileNameW(nullptr,p.data(),(DWORD)p.size()); std::wstring cmd=L"\""+std::wstring(p.data())+L"\""; RegSetValueExW(key,L"TimezoneWidget",0,REG_SZ,(BYTE*)cmd.c_str(),(DWORD)((cmd.size()+1)*sizeof(wchar_t))); }
    else RegDeleteValueW(key,L"TimezoneWidget"); RegCloseKey(key);
}
void enumerateZones(){
    zones.clear(); DYNAMIC_TIME_ZONE_INFORMATION z{};
    for(DWORD i=0;;i++){ DWORD r=EnumDynamicTimeZoneInformation(i,&z); if(r==ERROR_NO_MORE_ITEMS)break; if(r!=ERROR_SUCCESS)break; zones.push_back(z); }
    std::sort(zones.begin(),zones.end(),[](const auto&a,const auto&b){return _wcsicmp(a.StandardName,b.StandardName)<0;});
}
const DYNAMIC_TIME_ZONE_INFORMATION* findZone(const std::wstring& key){ auto i=std::find_if(zones.begin(),zones.end(),[&](const auto&z){return key==z.TimeZoneKeyName;}); return i==zones.end()?nullptr:&*i; }

void applyScale(){
    userScale=std::clamp(cfg.size/100.f,.5f,1.5f); ww=S(540*std::clamp(cfg.width/100.f,.7f,1.5f));
    header=S(34); cardH=S(104); gap=S(8); pad=S(16); lockedPad=std::max(3,S(18)); int n=(int)cfg.clocks.size();
    wh=(cfg.locked?2*lockedPad:header+2*pad)+n*cardH+std::max(0,n-1)*gap;
}
void clampMove(){
    RECT r{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&r,0); int left=(int)r.left,top=(int)r.top,right=(int)r.right,bottom=(int)r.bottom; cfg.x=std::clamp(cfg.x,left,std::max(left,right-ww)); cfg.y=std::clamp(cfg.y,top,std::max(top,bottom-wh));
    SetWindowPos(widget,cfg.topmost?HWND_TOPMOST:HWND_NOTOPMOST,cfg.x,cfg.y,ww,wh,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}

Color gc(COLORREF c){return Color(255,GetRValue(c),GetGValue(c),GetBValue(c));}
void rounded(Graphics& g,RectF r,float radius,Color fill,const Color* line=nullptr,float lw=1){
    GraphicsPath p; float d=radius*2; p.AddArc(r.X,r.Y,d,d,180,90); p.AddArc(r.GetRight()-d,r.Y,d,d,270,90); p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90); p.AddArc(r.X,r.GetBottom()-d,d,d,90,90); p.CloseFigure();
    SolidBrush b(fill); g.FillPath(&b,&p); if(line){Pen pen(*line,lw);g.DrawPath(&pen,&p);}
}
void text(Graphics&g,const std::wstring&s,const wchar_t*family,float size,int style,Color c,RectF r,StringAlignment align=StringAlignmentCenter){
    Font f(family,size,style,UnitPixel); SolidBrush b(c); StringFormat fmt; fmt.SetAlignment(align);fmt.SetLineAlignment(StringAlignmentCenter);fmt.SetTrimming(StringTrimmingEllipsisCharacter);g.DrawString(s.c_str(),-1,&f,r,&fmt,&b);
}
void render(){
    if(!widget)return; HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen); BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=ww;bi.bmiHeader.biHeight=-wh;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    void* bits{};HBITMAP bmp=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&bits,nullptr,0);HGDIOBJ old=SelectObject(mem,bmp);
    { Graphics g(mem);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.Clear(Color(0,0,0,0));
      float border=(float)std::max(2,S(2));rounded(g,RectF(1,1,(float)ww-2,(float)wh-2),(float)S(22),gc(cfg.outline));rounded(g,RectF(1+border,1+border,ww-2-2*border,wh-2-2*border),std::max(2.f,S(22)-border),Color(255,242,244,247));
      int y,cp;if(cfg.locked){y=lockedPad;cp=lockedPad;}else{ text(g,L"世界时钟",L"Microsoft YaHei UI",(float)S(12),FontStyleRegular,Color(255,116,119,123),RectF((float)S(18),0,(float)S(130),(float)header),StringAlignmentNear);text(g,L"⚙",L"Segoe UI Symbol",(float)S(17),0,Color(255,116,119,123),RectF((float)(ww-S(68)),0,(float)S(34),(float)header));text(g,L"×",L"Segoe UI",(float)S(18),0,Color(255,116,119,123),RectF((float)(ww-S(34)),0,(float)S(30),(float)header));y=header+pad;cp=pad;}
      float sp=std::clamp(cfg.spacing/100.f,.7f,1.2f),center=ww/2.f,ix=center+S(68-270)*sp,tx=center+S(235-270)*sp,lx=center+S(380-270)*sp;SYSTEMTIME utc{};GetSystemTime(&utc);
      for(auto& c:cfg.clocks){Color line(255,223,227,231);rounded(g,RectF((float)cp,(float)y,(float)(ww-2*cp),(float)cardH),(float)S(12),Color(255,255,255,255),&line,(float)std::max(1,S(1)));SYSTEMTIME local{};bool ok=false;if(auto*z=findZone(c.zone))ok=SystemTimeToTzSpecificLocalTimeEx(z,&utc,&local)!=FALSE;auto time=ok?std::format(L"{:02}:{:02}",local.wHour,local.wMinute):L"--:--";auto icon=!ok?L"?":(local.wHour>=7&&local.wHour<19?L"☀":L"☾");
        text(g,icon,L"Segoe UI Symbol",(float)S(28),0,Color(255,32,33,36),RectF(ix-S(38),(float)y,(float)S(76),(float)cardH));text(g,time,L"Segoe UI",(float)S(34),FontStyleBold,Color(255,91,93,96),RectF(tx-S(82),(float)y,(float)S(164),(float)cardH));text(g,c.label,L"Microsoft YaHei UI",(float)S(18),0,Color(255,32,33,36),RectF(lx,(float)y,std::max(10.f,ww-lx-cp-S(8)),(float)cardH),StringAlignmentNear);y+=cardH+gap; }
    }
    POINT dst{cfg.x,cfg.y},src{};SIZE sz{ww,wh};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(widget,screen,&dst,&sz,mem,&src,0,&blend,ULW_ALPHA);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);
}
void schedule(){SYSTEMTIME n{};GetLocalTime(&n);SetTimer(widget,1,std::max<UINT>(1000,(60-n.wSecond)*1000-n.wMilliseconds+50),nullptr);}

std::wstring getText(HWND h){int n=GetWindowTextLengthW(h);std::wstring s((size_t)n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
void setFont(HWND h,int pt=9,int weight=FW_NORMAL){HFONT f=CreateFontW(-MulDiv(pt,(int)(96*dpi),72),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");SendMessageW(h,WM_SETFONT,(WPARAM)f,TRUE);}
HWND control(const wchar_t*cls,const wchar_t*caption,DWORD style,int x,int y,int w,int h,HWND parent,int id){HWND c=CreateWindowExW(0,cls,caption,WS_CHILD|WS_VISIBLE|style,D(x),D(y),D(w),D(h),parent,(HMENU)(INT_PTR)id,inst,nullptr);setFont(c);return c;}
void closeSettings(){if(settings)DestroyWindow(settings);}
bool number(HWND h,int lo,int hi,int& out){try{out=std::clamp(std::stoi(getText(h)),lo,hi);return true;}catch(...){return false;}}
void saveSettings(){
    std::vector<Clock> clocks;for(int i=0;i<MAX_CLOCKS;i++){if(Button_GetCheck(enabled[i])!=BST_CHECKED)continue;auto l=getText(labels[i]);int sel=ComboBox_GetCurSel(zoneBoxes[i]);if(l.empty()||sel==CB_ERR){MessageBoxW(settings,L"每个启用的时钟都需要填写名称并选择时区。",L"无法保存",MB_ICONERROR);return;}size_t z=(size_t)ComboBox_GetItemData(zoneBoxes[i],sel);if(z>=zones.size())return;clocks.push_back({l,zones[z].TimeZoneKeyName});}
    if(clocks.empty()){MessageBoxW(settings,L"请至少启用一个时钟。",L"无法保存",MB_ICONERROR);return;}int sz,w,sp;COLORREF c;if(!number(sizeEdit,50,150,sz)||!number(widthEdit,70,150,w)||!number(spacingEdit,70,120,sp)){MessageBoxW(settings,L"大小、宽度或间距不是有效数字。",L"无法保存",MB_ICONERROR);return;}if(!parseColor(getText(colorEdit),c)){MessageBoxW(settings,L"颜色应为 #RRGGBB 格式。",L"无法保存",MB_ICONERROR);return;}
    cfg.clocks=std::move(clocks);cfg.locked=Button_GetCheck(lockedBox)==BST_CHECKED;cfg.topmost=Button_GetCheck(topmostBox)==BST_CHECKED;cfg.startup=Button_GetCheck(startupBox)==BST_CHECKED;cfg.size=sz;cfg.width=w;cfg.spacing=sp;cfg.outline=c;saveConfig();syncStartup();applyScale();clampMove();render();closeSettings();
}
void chooseColor(){CHOOSECOLORW cc{sizeof(cc)};std::array<COLORREF,16> custom{};cc.hwndOwner=settings;cc.rgbResult=cfg.outline;cc.lpCustColors=custom.data();cc.Flags=CC_FULLOPEN|CC_RGBINIT;if(ChooseColorW(&cc))SetWindowTextW(colorEdit,colorText(cc.rgbResult).c_str());}

LRESULT CALLBACK SettingsProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        auto title=control(L"STATIC",L"时区与城市",SS_LEFT,18,14,130,28,h,900);setFont(title,14,FW_BOLD);
        control(L"STATIC",L"勾选要显示的时钟；名称可以填写国家、城市或自定义文字",SS_LEFT,155,18,560,24,h,901);
        control(L"STATIC",L"显示",SS_LEFT,18,52,46,22,h,902);control(L"STATIC",L"显示名称",SS_LEFT,66,52,190,22,h,903);control(L"STATIC",L"Windows 时区",SS_LEFT,266,52,450,22,h,904);
        for(int i=0;i<MAX_CLOCKS;i++){
            int y=76+i*32;enabled[i]=control(L"BUTTON",L"",BS_AUTOCHECKBOX,25,y,28,25,h,1000+i);labels[i]=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,66,y,190,25,h,1100+i);zoneBoxes[i]=control(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL,266,y,458,300,h,1200+i);
            for(size_t z=0;z<zones.size();z++){auto shown=std::wstring(zones[z].StandardName)+L" — "+zones[z].TimeZoneKeyName;int item=ComboBox_AddString(zoneBoxes[i],shown.c_str());ComboBox_SetItemData(zoneBoxes[i],item,z);}
            if(i<(int)cfg.clocks.size()){Button_SetCheck(enabled[i],BST_CHECKED);SetWindowTextW(labels[i],cfg.clocks[i].label.c_str());for(int z=0;z<ComboBox_GetCount(zoneBoxes[i]);z++){size_t data=(size_t)ComboBox_GetItemData(zoneBoxes[i],z);if(data<zones.size()&&cfg.clocks[i].zone==zones[data].TimeZoneKeyName){ComboBox_SetCurSel(zoneBoxes[i],z);break;}}}else ComboBox_SetCurSel(zoneBoxes[i],0);
        }
        lockedBox=control(L"BUTTON",L"锁定位置",BS_AUTOCHECKBOX,18,405,105,26,h,1301);topmostBox=control(L"BUTTON",L"始终置顶",BS_AUTOCHECKBOX,126,405,105,26,h,1302);startupBox=control(L"BUTTON",L"开机启动",BS_AUTOCHECKBOX,234,405,105,26,h,1303);
        Button_SetCheck(lockedBox,cfg.locked?BST_CHECKED:BST_UNCHECKED);Button_SetCheck(topmostBox,cfg.topmost?BST_CHECKED:BST_UNCHECKED);Button_SetCheck(startupBox,cfg.startup?BST_CHECKED:BST_UNCHECKED);
        control(L"STATIC",L"大小 %",SS_LEFT,354,409,58,22,h,0);sizeEdit=control(L"EDIT",std::to_wstring(cfg.size).c_str(),WS_BORDER|ES_NUMBER,414,405,48,25,h,1310);
        control(L"STATIC",L"宽度 %",SS_LEFT,474,409,58,22,h,0);widthEdit=control(L"EDIT",std::to_wstring(cfg.width).c_str(),WS_BORDER|ES_NUMBER,534,405,48,25,h,1311);
        control(L"STATIC",L"间距 %",SS_LEFT,594,409,58,22,h,0);spacingEdit=control(L"EDIT",std::to_wstring(cfg.spacing).c_str(),WS_BORDER|ES_NUMBER,654,405,48,25,h,1312);
        control(L"STATIC",L"轮廓颜色",SS_LEFT,18,445,76,22,h,0);colorEdit=control(L"EDIT",colorText(cfg.outline).c_str(),WS_BORDER|ES_AUTOHSCROLL,96,441,94,25,h,1320);control(L"BUTTON",L"选择颜色",BS_PUSHBUTTON,198,440,92,28,h,1321);
        control(L"BUTTON",L"保存",BS_DEFPUSHBUTTON,548,440,82,30,h,IDOK);control(L"BUTTON",L"取消",BS_PUSHBUTTON,642,440,82,30,h,IDCANCEL);return 0;}
    case WM_COMMAND:switch(LOWORD(wp)){case IDOK:saveSettings();return 0;case IDCANCEL:closeSettings();return 0;case 1321:chooseColor();return 0;}break;
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:settings=nullptr;return 0;
    }return DefWindowProcW(h,msg,wp,lp);
}
void openSettings(){
    if(settings){ShowWindow(settings,SW_RESTORE);SetForegroundWindow(settings);return;}int w=D(760),height=D(525);RECT r{};SystemParametersInfoW(SPI_GETWORKAREA,0,&r,0);
    settings=CreateWindowExW(WS_EX_TOPMOST,SETTINGS_CLASS,L"世界时钟设置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,r.left+(r.right-r.left-w)/2,r.top+(r.bottom-r.top-height)/2,w,height,widget,nullptr,inst,nullptr);ShowWindow(settings,SW_SHOW);UpdateWindow(settings);
}
LRESULT CALLBACK WidgetProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_RBUTTONUP:openSettings();return 0;
    case WM_LBUTTONDOWN:{int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);if(!cfg.locked&&y<=header&&x>=ww-S(34)){DestroyWindow(h);return 0;}if(!cfg.locked&&y<=header&&x>=ww-S(70)){openSettings();return 0;}if(!cfg.locked){POINT p{};RECT r{};GetCursorPos(&p);GetWindowRect(h,&r);dragOffset={p.x-r.left,p.y-r.top};dragging=true;SetCapture(h);}return 0;}
    case WM_MOUSEMOVE:if(dragging&&(wp&MK_LBUTTON)){POINT p{};GetCursorPos(&p);cfg.x=p.x-dragOffset.x;cfg.y=p.y-dragOffset.y;SetWindowPos(h,nullptr,cfg.x,cfg.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);render();}return 0;
    case WM_LBUTTONUP:if(dragging){dragging=false;ReleaseCapture();saveConfig();}return 0;
    case WM_TIMER:if(wp==1){KillTimer(h,1);render();schedule();}return 0;
    case WM_DPICHANGED:dpi=HIWORD(wp)/96.f;applyScale();clampMove();render();return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE)DestroyWindow(h);return 0;
    case WM_DESTROY:closeSettings();PostQuitMessage(0);return 0;
    }return DefWindowProcW(h,msg,wp,lp);
}
bool registerClasses(){
    WNDCLASSEXW w{sizeof(w)};w.lpfnWndProc=WidgetProc;w.hInstance=inst;w.hCursor=LoadCursorW(nullptr,IDC_ARROW);w.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(IDI_APP_ICON));w.hIconSm=w.hIcon;w.lpszClassName=WIDGET_CLASS;if(!RegisterClassExW(&w))return false;
    WNDCLASSEXW s{sizeof(s)};s.lpfnWndProc=SettingsProc;s.hInstance=inst;s.hCursor=LoadCursorW(nullptr,IDC_ARROW);s.hIcon=w.hIcon;s.hIconSm=w.hIcon;s.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);s.lpszClassName=SETTINGS_CLASS;return RegisterClassExW(&s)!=0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int){
    inst=h;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);GdiplusStartupInput input;if(GdiplusStartup(&gdip,&input,nullptr)!=Ok)return 1;
    loadConfig();enumerateZones();HDC dc=GetDC(nullptr);dpi=GetDeviceCaps(dc,LOGPIXELSX)/96.f;ReleaseDC(nullptr,dc);applyScale();if(!registerClasses())return 2;
    widget=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|(cfg.topmost?WS_EX_TOPMOST:0),WIDGET_CLASS,APP,WS_POPUP,cfg.x,cfg.y,ww,wh,nullptr,nullptr,inst,nullptr);if(!widget)return 3;
    syncStartup();clampMove();render();schedule();ShowWindow(widget,SW_SHOWNOACTIVATE);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}GdiplusShutdown(gdip);return (int)msg.wParam;
}
