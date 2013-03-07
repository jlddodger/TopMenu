// TopMenu.cpp : Defines the exported functions for the DLL application.
//

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include "windows.h"
#include "atlcoll.h"

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

class TopMenuItem
{
   virtual bool appendMenuItem(MENUITEMINFO* pMenuItemInfo) = 0;
   virtual LRESULT run(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) = 0;
}

class TMI_AlwaysOnTop : public TopMenuItem
{
   virtual bool TMI_AlwaysOnTop::appendMenuItem(MENUITEMINFO* pMenuItemInfo) override;
   virtual LRESULT TMI_AlwaysOnTop::run(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) override;
};

bool TMI_AlwaysOnTop::appendMenuItem(MENUITEMINFO* pMenuItemInfo)
{
   MENUITEMINFO& topMost = *pMenuItemInfo;
   ZeroMemory(&topMost, sizeof(topMost));
   topMost.cbSize = sizeof(topMost);
   topMost.fMask = (MIIM_ID | MIIM_STATE | MIIM_TYPE | MIIM_DATA);
   topMost.fType = MFT_STRING;
   topMost.fState = (dwStyleEx & WS_EX_TOPMOST) ? MFS_CHECKED : MFS_UNCHECKED;
   topMost.wID = PM_ALWAYS_ON_TOP;
   topMost.dwTypeData = "Always On Top";
   topMost.dwItemData = POWER_MENU_ID;
}

LRESULT TMI_AlwaysOnTop::run(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
   HMENU hMenu = (HMENU)wParam;
   DWORD dwStyle = (DWORD)GetWindowLong(hwnd, GWL_STYLE);
   DWORD dwStyleEx = (DWORD)GetWindowLong(hwnd, GWL_EXSTYLE);
   if(!(dwStyle & WS_SYSMENU)) {
      return FALSE;
   }         
   DWORD dwStyleEx = (DWORD)GetWindowLong(hwnd, GWL_EXSTYLE);
   if(dwStyleEx & WS_EX_TOPMOST) {
      SetWindowPos(hwnd, HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
   }
   else {
      SetWindowPos(hwnd, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
   }

   return ERROR_SUCCESS;
}

class TopMenuItemMap : public CMap<TopMenuItem*, TopMenuItem*, bool, bool>
{
   TopMenuItemMap();

   DWORD getId(TopMenuItem* pItem);
   TopMenuItem* getItem(DWORD id);
}

TopMenuItemMap::TopMenuItemMap()
{
   SetAt(new TMI_AlwaysOnTop(), true);
   SetAt(new TMI_Priority(), true);
   SetAt(new TMI_Transparency(), true);
   SetAt(new TMI_MinimizeToTray(), true);


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
}

class TopMenu : public TopMenu_Plugin
{ //lint !e1790 JLD

protected:
   static TopMenu* s_pInstance;
   static TopMenu_CS s_cs;

   static DWORD WINAPI beginRun(LPVOID lpParameter);

   static LRESULT CALLBACK CallWndProc(int nCode, WPARAM wParam, LPARAM lParam);
   static LRESULT CALLBACK CallWndRetProc(int nCode, WPARAM wParam, LPARAM lParam);
   static LRESULT CALLBACK GetMsgProc(int nCode, WPARAM wParam, LPARAM lParam);

   static bool runningElevated();

protected:
   const TopMenuItemMap m_items;

   HANDLE m_thread; // to wait for thread
   HANDLE m_stopEvent; // to unload cleanly

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
  : m_items()
  , m_thread(NULL)
  , m_stopEvent(CreateEvent(NULL, TRUE, FALSE, NULL))
{
   BOOL b;
   TopMenu_CSLock lock(s_cs);

   InterlockedIncrement((long*)&shared_instanceCount);

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
         HHOOK hCallHook = SetWindowsHookEx(WH_CALLWNDPROC, CallWndProc, (HINSTANCE)&__ImageBase, NULL);
         HHOOK hGetMsgHook = SetWindowsHookEx(WH_GETMESSAGE, GetMsgProc, (HINSTANCE)&__ImageBase, NULL);
      
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
         FreeLibrary(hDll); //lint !e534 JLD

         s_cs.enter();

         b=UnhookWindowsHookEx(hCallHook); ASSERT(b);
         hCallHook = NULL;

         b=UnhookWindowsHookEx(hGetMsgHook); ASSERT(b);
         hGetMsgHook = NULL;

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
            
      MENUITEMINFO closeItem;
      ZeroMemory(&closeItem, sizeof(closeItem));
      closeItem.cbSize = sizeof(closeItem);
      if(GetMenuItemInfo(hMenu, SC_CLOSE, FALSE, &closeItem))
      {      
         HMENU hPriorityMenu = CreatePopupMenu();
         AppendMenu(hPriorityMenu, MF_BYPOSITION, PM_PRIORITY_IDLE   , "4 - Idle"         );
         AppendMenu(hPriorityMenu, MF_BYPOSITION, PM_PRIORITY_BN     , "6 - Below Normal" );
         AppendMenu(hPriorityMenu, MF_BYPOSITION, PM_PRIORITY_NORMAL , "8 - Normal"       );
         AppendMenu(hPriorityMenu, MF_BYPOSITION, PM_PRIORITY_AN     , "10 - Above Normal");
         AppendMenu(hPriorityMenu, MF_BYPOSITION, PM_PRIORITY_HIGH   , "13 - High"        );
         AppendMenu(hPriorityMenu, MF_BYPOSITION, PM_PRIORITY_RT     , "24 - Realtime"    );

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
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, PM_TRANSPARENT_100   , "0% (opaque)"      );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, PM_TRANSPARENT_90    , "10%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, PM_TRANSPARENT_80    , "20%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, PM_TRANSPARENT_70    , "30%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, PM_TRANSPARENT_60    , "40%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, PM_TRANSPARENT_50    , "50%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, PM_TRANSPARENT_40    , "40%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, PM_TRANSPARENT_30    , "30%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, PM_TRANSPARENT_20    , "20%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, PM_TRANSPARENT_10    , "10%"              );
         AppendMenu(hTransparencyMenu, MF_BYPOSITION, PM_TRANSPARENT_0     , "0% (transparent)" );

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
         minToTray.wID = PM_MINIMIZE_TO_TRAY;
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
   BOOL removed = (wRemoveMsg & PM_REMOVE);
   if(removed && message == WM_SYSCOMMAND) {
      if((wParam&0xFFF0) == PM_ALWAYS_ON_TOP) {
         DWORD dwStyleEx = (DWORD)GetWindowLong(hwnd, GWL_EXSTYLE);
         if(dwStyleEx & WS_EX_TOPMOST) {
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
         }
         else {
            SetWindowPos(hwnd, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
         }
      }
      if((wParam&0xFFF0) == PM_PRIORITY_IDLE) {
         if(!SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
         }
      }
      if((wParam&0xFFF0) == PM_PRIORITY_BN) {
         if(!SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
         }
      }
      if((wParam&0xFFF0) == PM_PRIORITY_NORMAL) {
         if(!SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
         }
      }
      if((wParam&0xFFF0) == PM_PRIORITY_AN) {
         if(!SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
         }
      }
      if((wParam&0xFFF0) == PM_PRIORITY_HIGH) {
         if(!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
         }
      }
      if((wParam&0xFFF0) == PM_PRIORITY_RT) {
         if(!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
            MessageBox(NULL, "Cannot set priority class.", "TopMenu", MB_OK);
         }
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