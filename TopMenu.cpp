// TopMenu.cpp : Defines the exported functions for the DLL application.
//

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include "windows.h"
#include "windowsx.h"
#include "atlcoll.h"
#include "shellapi.h"

#include "TopMenu_Plugin.h"
#include "TopMenu_Util.h"

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

#pragma comment(linker, "/SECTION:.SHARED,RWS")
#pragma data_seg(".SHARED")
int shared_instanceCount = 0;
DWORD shared_hookPid = 0;
#pragma data_seg()

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
   UNREFERENCED_PARAMETER(hModule);
   UNREFERENCED_PARAMETER(lpReserved);

	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
   default:
		break;
	}
	return TRUE;
}

enum 
{
   TM_ALWAYS_ON_TOP        = 0x7010, // last digit must always be zero

   TM_PRIORITY_IDLE        = 0x7110,
   TM_PRIORITY_BN          = 0x7120,
   TM_PRIORITY_NORMAL      = 0x7130,
   TM_PRIORITY_AN          = 0x7140,
   TM_PRIORITY_HIGH        = 0x7150,
   TM_PRIORITY_RT          = 0x7160,

   TM_TRANSPARENT_100      = 0x7210,
   TM_TRANSPARENT_90       = 0x7220,
   TM_TRANSPARENT_80       = 0x7230,
   TM_TRANSPARENT_70       = 0x7240,
   TM_TRANSPARENT_60       = 0x7250,
   TM_TRANSPARENT_50       = 0x7260,
   TM_TRANSPARENT_40       = 0x7270,
   TM_TRANSPARENT_30       = 0x7280,
   TM_TRANSPARENT_20       = 0x7290,
   TM_TRANSPARENT_10       = 0x7300,
   TM_TRANSPARENT_0        = 0x7310,

   TM_MINIMIZE_TO_TRAY     = 0x7410
};

class TopMenu : public TopMenu_Plugin
{ //lint !e1790 JLD

protected:
   static TopMenu* s_pInstance;
   static TopMenu_CS s_cs;
   static TopMenu_Hook s_hCallHook;
   static TopMenu_Hook s_hGetMsgHook;

   static DWORD WINAPI beginRun(LPVOID lpParameter);

   static LRESULT CALLBACK CallWndProc(int nCode, WPARAM wParam, LPARAM lParam);
   static LRESULT CALLBACK CallWndRetProc(int nCode, WPARAM wParam, LPARAM lParam);
   static LRESULT CALLBACK GetMsgProc(int nCode, WPARAM wParam, LPARAM lParam);

   static LRESULT CALLBACK TrayIconProc(HWND hwndTray, UINT message, WPARAM wParam, LPARAM lParam);

   static bool runningElevated();

protected:
   HANDLE m_thread; // to wait for thread
   HANDLE m_stopEvent; // to unload cleanly
   HICON m_hProgIcon; // for minimize to tray

public:
   TopMenu();
   ~TopMenu();
   
protected:
   DWORD run();

   BOOL preCallWnd(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pRC);
   void postCallWnd(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT rc);
   void getMsg(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, DWORD time, POINT pt, UINT wRemoveMsg);
};

TopMenu* TopMenu::s_pInstance = 0;
TopMenu_CS TopMenu::s_cs;
TopMenu_Hook TopMenu::s_hCallHook;
TopMenu_Hook TopMenu::s_hGetMsgHook;

DWORD WINAPI TopMenu::beginRun(LPVOID lpParameter)
{
   TopMenu* pThis = (TopMenu*)lpParameter;
   if(pThis) {
      return pThis->run();
   }

   return UINT_MAX;
}

LRESULT CALLBACK TopMenu::CallWndProc(int nCode, WPARAM wParam, LPARAM lParam)
{
   if(nCode == HC_ACTION) {
      TopMenu_CSLock lock(s_cs);
      LRESULT lResult;
      CWPSTRUCT& cwp = *(CWPSTRUCT*)lParam;
      if(s_pInstance && s_pInstance->preCallWnd(cwp.hwnd, cwp.message, cwp.wParam, cwp.lParam, &lResult)) {
         return lResult;
      }
   }

   return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK TopMenu::CallWndRetProc(int nCode, WPARAM wParam, LPARAM lParam)
{
   if(nCode == HC_ACTION) {
      TopMenu_CSLock lock(s_cs);
      CWPRETSTRUCT& cwp = *(CWPRETSTRUCT*)lParam;
      if(s_pInstance) {
         s_pInstance->postCallWnd(cwp.hwnd, cwp.message, cwp.wParam, cwp.lParam, cwp.lResult);
      }
   }

   return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK TopMenu::GetMsgProc(int nCode, WPARAM wParam, LPARAM lParam)
{
   if(nCode == HC_ACTION) {
      TopMenu_CSLock lock(s_cs);
      MSG& msg = *(MSG*)lParam;
      if(s_pInstance) {
         s_pInstance->getMsg(msg.hwnd, msg.message, msg.wParam, msg.lParam, msg.time, msg.pt, (UINT)wParam);
      }
   }

   return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK TopMenu::TrayIconProc(HWND hwndTray, UINT message, WPARAM wParam, LPARAM lParam) 
{ 
   switch (message) 
   { 
      case WM_APP: {
         short nEvent = LOWORD(lParam);  // contains notification events, such as NIN_BALLOONSHOW, NIN_POPUPOPEN, or WM_CONTEXTMENU.
         short nIconID = HIWORD(lParam); // contains the icon ID. Icon IDs are restricted to a length of 16 bits.
         short x = GET_X_LPARAM(wParam); // returns the X anchor coordinate for notification events NIN_POPUPOPEN, NIN_SELECT, NIN_KEYSELECT, and all mouse messages between WM_MOUSEFIRST and WM_MOUSELAST. If any of those messages are generated by the keyboard, wParam is set to the upper-left corner of the target icon. For all other messages, wParam is undefined.
         short y = GET_Y_LPARAM(wParam); // returns the Y anchor coordinate for notification events and messages as defined for the X anchor.

         BOOL b;
         switch(nEvent)
         {
            case WM_LBUTTONDOWN:
               ShowWindow(GetParent(hwndTray), SW_SHOW);
               b=DestroyWindow(hwndTray); ASSERT(b);
               return 0;
            default:
               break;
         }
      }
      default:
         break;
   }

   return DefWindowProc(hwndTray, message, wParam, lParam); 
}

bool TopMenu::runningElevated()
{
   // Dynamically link for CheckTokenMembership
   HINSTANCE hinstLib; 
   typedef BOOL (WINAPI* DynCheckTokenMembership)(HANDLE,PSID,PBOOL);
   DynCheckTokenMembership pCheckTokenMembership;
   BOOL fRunTimeLinkSuccess = FALSE; 

   // Get a handle to the DLL module.
   hinstLib = LoadLibrary(TEXT("Advapi32.dll")); 

   // If the handle is valid, try to get the function address.
   if (hinstLib == NULL) { 
      return true;
   }

   pCheckTokenMembership = (DynCheckTokenMembership) GetProcAddress(hinstLib, "CheckTokenMembership"); 

   if (pCheckTokenMembership == NULL) {
      FreeLibrary(hinstLib); 
      return true;
   }

   // Prepare for membership check
   BOOL b;
   SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
   PSID AdministratorsGroup; 

   b = AllocateAndInitializeSid(
         &NtAuthority,
         2,
         SECURITY_BUILTIN_DOMAIN_RID,
         DOMAIN_ALIAS_RID_ADMINS,
         0, 0, 0, 0, 0, 0,
         &AdministratorsGroup); 

   if(b) {
      if (!pCheckTokenMembership(NULL, AdministratorsGroup, &b)) {
         b = FALSE;
      } 
      VERIFY(FreeSid(AdministratorsGroup) == NULL); 
   }

   // Free the DLL module.
   FreeLibrary(hinstLib); 

   return b != FALSE;
}

TopMenu::TopMenu()
  : m_thread(NULL)
  , m_stopEvent(CreateEvent(NULL, TRUE, FALSE, NULL))
{
   BOOL b;
   DWORD d;
   TopMenu_CSLock lock(s_cs);

   InterlockedIncrement((long*)&shared_instanceCount);

   WNDCLASS wndClass;
   ZeroMemory(&wndClass, sizeof(wndClass));
   wndClass.lpszClassName = "TrayIconMonitor";
   wndClass.hInstance = (HINSTANCE)&__ImageBase;
   wndClass.lpfnWndProc = TrayIconProc;
   d=RegisterClass(&wndClass); ASSERT(d != 0);

   char exePath[MAX_PATH];
   d=GetModuleFileName(NULL, exePath, sizeof(exePath)); ASSERT(d != 0);
   d=ExtractIconEx(exePath, 0, NULL, &m_hProgIcon, 1); ASSERT(d == 1);

   ASSERTE(m_stopEvent);

   m_thread = CreateThread(NULL, 0, &TopMenu::beginRun, this, 0, NULL); ASSERTE(m_thread);
   if(!m_thread) {
      b=SetEnvironmentVariable("TopMenu_Plugin", "Loaded - cannot start monitor thread"); ASSERT(b); //lint !e534 JLD
   }
   else {
      b=SetEnvironmentVariable("TopMenu_Plugin", "Loaded"); ASSERT(b); //lint !e534 JLD
   }

   ASSERTE(!s_pInstance);
   s_pInstance = this;
}

TopMenu::~TopMenu()
{
   BOOL b;
   DWORD d;

   b=SetEvent(m_stopEvent); ASSERT(b);
   d=WaitForSingleObject(m_thread, INFINITE); ASSERT(d != WAIT_FAILED && d != WAIT_TIMEOUT);

   b=DestroyIcon(m_hProgIcon); ASSERT(b);

   b=UnregisterClass("TrayIconMonitor", (HINSTANCE)&__ImageBase); ASSERT(b);

   InterlockedDecrement((long*)&shared_instanceCount);
   b=SetEnvironmentVariable("TopMenu_Plugin", "Unloaded"); ASSERT(b); //lint !e534 JLD
}

DWORD TopMenu::run()
{
   BOOL b;
   
#if defined(_M_X64)
   const char MUTEX_NAME[] = "TopMenu_Mutex64";
#elif defined(_M_IX86)
   const char MUTEX_NAME[] = "TopMenu_Mutex32";
#endif

   EveryoneAccess ea;
   HANDLE mutex = CreateMutex(ea.getSA(), FALSE, MUTEX_NAME); ASSERT(mutex);

   do
   {
      DWORD rc = WaitForSingleObject(mutex, INFINITE); ASSERT(rc != WAIT_FAILED && rc != WAIT_TIMEOUT);
      if(rc == WAIT_OBJECT_0 || rc == WAIT_ABANDONED) {
         TopMenu_CSLock lock(s_cs);

         shared_hookPid = GetCurrentProcessId();
         s_hCallHook.set(SetWindowsHookEx(WH_CALLWNDPROC, CallWndProc, (HINSTANCE)&__ImageBase, NULL));
         s_hGetMsgHook.set(SetWindowsHookEx(WH_GETMESSAGE, GetMsgProc, (HINSTANCE)&__ImageBase, NULL));
      
         b=SetEnvironmentVariable("TopMenu_Plugin", "Loaded - Primary"); ASSERT(b);
         s_cs.leave(); // allow other threads to takeover
      
         HMODULE hDll = LoadLibrary(dllPath);
         try
         {
            typedef bool (WINAPI *PBOOLFNC)();
            PBOOLFNC pRefreshFnc = (PBOOLFNC) GetProcAddress(hDll, "refresh");
            while(WaitForSingleObject(m_stopEvent, 1000) == WAIT_TIMEOUT) {
               if(pRefreshFnc) {
                  (*pRefreshFnc)(); // load into new processes
               }

               if(!runningElevated()) {
                  break; // let someone else try being primary
               }
            }
         }
         catch(...) { }
         b=FreeLibrary(hDll); ASSERT(b);

         s_cs.enter();

         s_hCallHook.reset();
         s_hGetMsgHook.reset();

         PostMessage(HWND_BROADCAST, WM_NULL, 0, 0); // run message pumps to unload the TopMenu64.dll

         b=ReleaseMutex(mutex); ASSERT(b);
         b=SetEnvironmentVariable("TopMenu_Plugin", "Loaded - Not primary"); ASSERT(b);
      }
   } while(WaitForSingleObject(m_stopEvent, 0) == WAIT_TIMEOUT);

   b=CloseHandle(mutex); ASSERT(b);

   return 0;
}

BOOL TopMenu::preCallWnd(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pRC)
{
   BOOL b;

   const ULONG_PTR POWER_MENU_ID = 0x50494552; //POWR in ASCII

   if(message == WM_INITMENUPOPUP) {   
      HMENU hMenu = (HMENU)wParam;
      DWORD dwStyle = (DWORD)GetWindowLong(hwnd, GWL_STYLE);
      DWORD dwStyleEx = (DWORD)GetWindowLong(hwnd, GWL_EXSTYLE);

      if(!(dwStyle & WS_SYSMENU)) {
         return FALSE;
      }

      MENUITEMINFO closeItem;
      ZeroMemory(&closeItem, sizeof(closeItem));
      closeItem.cbSize = sizeof(closeItem);
      if(GetMenuItemInfo(hMenu, SC_CLOSE, FALSE, &closeItem))
      {      
         MENUITEMINFO topMost;
         ZeroMemory(&topMost, sizeof(topMost));
         topMost.cbSize = sizeof(topMost);
         topMost.fMask = (MIIM_ID | MIIM_STATE | MIIM_TYPE | MIIM_DATA);
         topMost.fType = MFT_STRING;
         topMost.fState = (dwStyleEx & WS_EX_TOPMOST) ? MFS_CHECKED : MFS_UNCHECKED;
         topMost.wID = TM_ALWAYS_ON_TOP;
         topMost.dwTypeData = "Always On Top";
         topMost.dwItemData = POWER_MENU_ID;

         HMENU hPriorityMenu = CreatePopupMenu();
         AppendMenu(hPriorityMenu, MF_BYPOSITION, TM_PRIORITY_IDLE   , "4 - Idle"         );
         AppendMenu(hPriorityMenu, MF_BYPOSITION, TM_PRIORITY_BN     , "6 - Below Normal" );
         AppendMenu(hPriorityMenu, MF_BYPOSITION, TM_PRIORITY_NORMAL , "8 - Normal"       );
         AppendMenu(hPriorityMenu, MF_BYPOSITION, TM_PRIORITY_AN     , "10 - Above Normal");
         AppendMenu(hPriorityMenu, MF_BYPOSITION, TM_PRIORITY_HIGH   , "13 - High"        );
         AppendMenu(hPriorityMenu, MF_BYPOSITION, TM_PRIORITY_RT     , "24 - Realtime"    );

         MENUITEMINFO priority;
         ZeroMemory(&priority, sizeof(priority));
         priority.cbSize = sizeof(priority);
         priority.fMask = (MIIM_ID | MIIM_TYPE | MIIM_DATA | MIIM_SUBMENU);
         priority.fType = MFT_STRING;
         priority.wID = 0;
         priority.hSubMenu = hPriorityMenu;
         priority.dwTypeData = "Priority";
         priority.dwItemData = POWER_MENU_ID;
      
         HMENU hTransparencyMenu = CreatePopupMenu();
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, TM_TRANSPARENT_100   , "0% (opaque)"      );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, TM_TRANSPARENT_90    , "10%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, TM_TRANSPARENT_80    , "20%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, TM_TRANSPARENT_70    , "30%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, TM_TRANSPARENT_60    , "40%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, TM_TRANSPARENT_50    , "50%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, TM_TRANSPARENT_40    , "40%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, TM_TRANSPARENT_30    , "30%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, TM_TRANSPARENT_20    , "20%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, TM_TRANSPARENT_10    , "10%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, TM_TRANSPARENT_0     , "0% (transparent)" );

         MENUITEMINFO transparency;
         ZeroMemory(&transparency, sizeof(transparency));
         transparency.cbSize = sizeof(transparency);
         transparency.fMask = (MIIM_ID | MIIM_TYPE | MIIM_DATA | MIIM_SUBMENU);
         transparency.fType = MFT_STRING;
         transparency.wID = 0;
         transparency.hSubMenu = hTransparencyMenu;
         transparency.dwTypeData = "Transparency";
         transparency.dwItemData = POWER_MENU_ID;
                  
         MENUITEMINFO minToTray;
         ZeroMemory(&minToTray, sizeof(minToTray));
         minToTray.cbSize = sizeof(minToTray);
         minToTray.fMask = (MIIM_ID | MIIM_TYPE | MIIM_DATA);
         minToTray.fType = MFT_STRING;
         minToTray.wID = TM_MINIMIZE_TO_TRAY;
         minToTray.dwTypeData = "Minimize To Tray";
         minToTray.dwItemData = POWER_MENU_ID;

         MENUITEMINFO sep;
         ZeroMemory(&sep, sizeof(sep));
         sep.cbSize = sizeof(sep);
         sep.fMask = (MIIM_TYPE | MIIM_DATA);
         sep.fType = MFT_SEPARATOR;
         sep.dwItemData = POWER_MENU_ID;

         b=InsertMenuItem(hMenu, SC_CLOSE, FALSE, &topMost); ASSERT(b);
         b=InsertMenuItem(hMenu, SC_CLOSE, FALSE, &priority); ASSERT(b);
         b=InsertMenuItem(hMenu, SC_CLOSE, FALSE, &transparency); ASSERT(b);
         b=InsertMenuItem(hMenu, SC_CLOSE, FALSE, &minToTray); ASSERT(b);
         b=InsertMenuItem(hMenu, SC_CLOSE, FALSE, &sep); ASSERT(b);
      }
   }
   else if (message == WM_UNINITMENUPOPUP) {
      HMENU hMenu = (HMENU)wParam;
      DWORD dwStyle = (DWORD)GetWindowLong(hwnd, GWL_STYLE);
      if(!(dwStyle & WS_SYSMENU)) {
         return FALSE;
      }

      int menuCnt = GetMenuItemCount(hMenu);
      for(int i = 0; i < menuCnt; i++) {
         MENUITEMINFO menuItem;
         ZeroMemory(&menuItem, sizeof(menuItem));
         menuItem.cbSize = sizeof(menuItem);
         menuItem.fMask = (MIIM_ID | MIIM_STATE | MIIM_TYPE | MIIM_DATA);

         if(!GetMenuItemInfo(hMenu, i, TRUE, &menuItem)) {
            continue;
         }

         if(menuItem.dwItemData == POWER_MENU_ID) {
            b=DeleteMenu(hMenu, i, MF_BYPOSITION); ASSERT(b);
            i--; // back up menu index to not skip the entry now at position i
         }
      }
   }

   return FALSE;
}

void TopMenu::postCallWnd(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT rc)
{
   ;
}

void TopMenu::getMsg(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, DWORD time, POINT pt, UINT wRemoveMsg)
{
   BOOL b;

   BOOL removed = (wRemoveMsg & PM_REMOVE);
   if(removed && message == WM_SYSCOMMAND) {
      if((wParam&0xFFF0) == TM_ALWAYS_ON_TOP) {
         DWORD dwStyleEx = (DWORD)GetWindowLong(hwnd, GWL_EXSTYLE);
         if(dwStyleEx & WS_EX_TOPMOST) {
            if(!SetWindowPos(hwnd, HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)) {
               MessageBox(NULL, "Cannot unset top most order.", "TopMenu", MB_OK);
               return;
            }
         }
         else {
            if(!SetWindowPos(hwnd, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)) {
               MessageBox(NULL, "Cannot unset top most order.", "TopMenu", MB_OK);
               return;
            }
         }
      }
      if((wParam&0xFFF0) == TM_PRIORITY_IDLE) {
         if(!SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_PRIORITY_BN) {
         if(!SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_PRIORITY_NORMAL) {
         if(!SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_PRIORITY_AN) {
         if(!SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_PRIORITY_HIGH) {
         if(!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_PRIORITY_RT) {
         if(!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_TRANSPARENT_100) {
         SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
         if(!SetLayeredWindowAttributes(hwnd, RGB(0,0,0), (byte)((10 * 255)/10), LWA_ALPHA)) {
            MessageBox(NULL, "Cannot set layered window attributes.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_TRANSPARENT_90) {
         SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
         if(!SetLayeredWindowAttributes(hwnd, RGB(0,0,0), (byte)((9 * 255)/10), LWA_ALPHA)) {
            MessageBox(NULL, "Cannot set layered window attributes.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_TRANSPARENT_80) {
         SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
         if(!SetLayeredWindowAttributes(hwnd, RGB(0,0,0), (byte)((8 * 255)/10), LWA_ALPHA)) {
            MessageBox(NULL, "Cannot set layered window attributes.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_TRANSPARENT_70) {
         SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
         if(!SetLayeredWindowAttributes(hwnd, RGB(0,0,0), (byte)((7 * 255)/10), LWA_ALPHA)) {
            MessageBox(NULL, "Cannot set layered window attributes.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_TRANSPARENT_60) {
         SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
         if(!SetLayeredWindowAttributes(hwnd, RGB(0,0,0), (byte)((6 * 255)/10), LWA_ALPHA)) {
            MessageBox(NULL, "Cannot set layered window attributes.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_TRANSPARENT_50) {
         SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
         if(!SetLayeredWindowAttributes(hwnd, RGB(0,0,0), (byte)((5 * 255)/10), LWA_ALPHA)) {
            MessageBox(NULL, "Cannot set layered window attributes.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_TRANSPARENT_40) {
         SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
         if(!SetLayeredWindowAttributes(hwnd, RGB(0,0,0), (byte)((4 * 255)/10), LWA_ALPHA)) {
            MessageBox(NULL, "Cannot set layered window attributes.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_TRANSPARENT_30) {
         SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
         if(!SetLayeredWindowAttributes(hwnd, RGB(0,0,0), (byte)((3 * 255)/10), LWA_ALPHA)) {
            MessageBox(NULL, "Cannot set layered window attributes.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_TRANSPARENT_20) {
         SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
         if(!SetLayeredWindowAttributes(hwnd, RGB(0,0,0), (byte)((2 * 255)/10), LWA_ALPHA)) {
            MessageBox(NULL, "Cannot set layered window attributes.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_TRANSPARENT_10) {
         SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
         if(!SetLayeredWindowAttributes(hwnd, RGB(0,0,0), (byte)((1 * 255)/10), LWA_ALPHA)) {
            MessageBox(NULL, "Cannot set layered window attributes.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_TRANSPARENT_0) {
         SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
         if(!SetLayeredWindowAttributes(hwnd, RGB(0,0,0), (byte)((0 * 255)/10), LWA_ALPHA)) {
            MessageBox(NULL, "Cannot set layered window attributes.", "TopMenu", MB_OK);
            return;
         }
      }
      if((wParam&0xFFF0) == TM_MINIMIZE_TO_TRAY) {
         HWND hwndTray = CreateWindow("TrayIconMonitor", NULL, WS_POPUP, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
         if(!hwndTray) {
            MessageBox(NULL, "Cannot create tray icon window.", "TopMenu", MB_OK);
            return;
         }

         NOTIFYICONDATA iconData;
         ZeroMemory(&iconData, sizeof(iconData));
         iconData.cbSize = sizeof(NOTIFYICONDATA);
         iconData.hWnd = hwndTray;
         iconData.uID = GetWindowLong(hwnd, GWL_ID);
         iconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
         iconData.uCallbackMessage = WM_APP;
         iconData.hIcon = m_hProgIcon;
         GetWindowText(hwnd, iconData.szTip, sizeof(iconData.szTip));
         iconData.uVersion = NOTIFYICON_VERSION;

         if(!Shell_NotifyIcon(NIM_ADD, &iconData)) {
            b=DestroyWindow(hwndTray); ASSERT(b);
            MessageBox(NULL, "Cannot submit tray icon.", "TopMenu", MB_OK);
            return;
         }
         ShowWindow(hwnd, SW_HIDE);
      }
   }
}

extern "C"
{
   DLL_DECLSPEC DWORD TopMenu_GetPluginVersion()
   {
      return TopMenu_Plugin::GetPluginVersion();
   }

   DLL_DECLSPEC TopMenu_Plugin* TopMenu_CreatePlugin()
   {
      return new TopMenu();
   }

   DLL_DECLSPEC void TopMenu_DestroyPlugin(TopMenu_Plugin* _pPlugin)
   {
      delete _pPlugin;
   }
}