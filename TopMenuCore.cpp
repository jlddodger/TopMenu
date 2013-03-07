
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include "windows.h"
#include "Psapi.h"
#include "stdlib.h"

#include "TopMenu_Plugin.h"
#include "TopMenu_Util.h"

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

class TopMenuCore
{
public:
   static const char UNLOAD_EVENT_NAME[];
   
   static TopMenu_CS s_cs;
   static FILETIME s_lastLoadTime;
   
   static bool enable();
   static bool refresh();
   static bool loadInto(DWORD _pid);
   static void note(const char*);
   static void status(const char*);

   static HANDLE createStopEvent();

   static DWORD WINAPI beginRun(LPVOID lpParameter);

private:
   HMODULE m_hModule;
   HANDLE m_hStopEvent;

   struct TopMenu_PluginStackEntry* m_pTopEntry;

public:
   TopMenuCore(HMODULE _hModule);

   bool run();

   bool startup();
   bool shutdown();

   bool waitForStopEvent();
   bool setStopEvent();
};

struct TopMenu_PluginStackEntry
{
   HMODULE m_hModule;
   TopMenu_Plugin* m_pPlugin;
   struct TopMenu_PluginStackEntry* m_pNext;
};

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
   UNREFERENCED_PARAMETER(lpvReserved);

   if(fdwReason == DLL_PROCESS_ATTACH) {
      TopMenuCore::status("Activating");

      if(!CreateThread(NULL, 0, &TopMenuCore::beginRun, hinstDLL, 0, NULL)) {
         TopMenuCore::status("ActivateFailure_Thread");
      }
   }
   else if(fdwReason == DLL_PROCESS_DETACH) {
      TopMenuCore::status("Deactivated");
   }

   return TRUE;
}

extern "C"
{
   __declspec(dllexport) void enable()
   {
      if(!TopMenuCore::enable()) {
         TopMenuCore::note("could not load into initial processes");
      }
   }

   __declspec(dllexport) void refresh()
   {
      if(!TopMenuCore::refresh()) {
         TopMenuCore::note("could not load into new processes");
      }
   }

   __declspec(dllexport) void disable()
   {
      TopMenuCore core(NULL);
      if(!core.setStopEvent()) {
         TopMenuCore::note("could not set stop event");
      }
   }
}

const char TopMenuCore::UNLOAD_EVENT_NAME[] = "TopMenuCore_Unload";
TopMenu_CS TopMenuCore::s_cs;
FILETIME TopMenuCore::s_lastLoadTime;

bool TopMenuCore::enable()
{
   TopMenu_CSLock lock(s_cs);

   ZeroMemory(&s_lastLoadTime, sizeof(s_lastLoadTime));

   HANDLE hStopEvent = createStopEvent();
   if(!ResetEvent(hStopEvent)) {
      CloseHandle(hStopEvent);
      return false;
   }
   CloseHandle(hStopEvent);

   refresh();

   return true;
}

bool TopMenuCore::refresh()
{
   TopMenu_CSLock lock(s_cs);

   SYSTEMTIME time;
   GetSystemTime(&time);
   
   HANDLE hStopEvent = createStopEvent();
   if(WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) {
      CloseHandle(hStopEvent);
      return false;
   }
   CloseHandle(hStopEvent);

   char dbgPID[MAX_PATH];
   if(GetEnvironmentVariable("TopMenu_DebugPID", dbgPID, sizeof(dbgPID))) {
      DWORD pid = atoi(dbgPID);
      loadInto(pid);
      return true;
   }

   DWORD aProcesses[1024];
   DWORD cbNeeded;

   if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded))
   {
      return false;
   }

   DWORD cProcesses = cbNeeded / sizeof(DWORD);

   DWORD failProcs[1024];
   ZeroMemory(failProcs, 1024);

   DWORD* pFailProc = failProcs;
   for (DWORD i = 0; i < cProcesses; i++)
   {
      if(aProcesses[i] != 0)
      {
         if(!loadInto(aProcesses[i])) {
            *pFailProc = aProcesses[i];
            pFailProc++;
         }

      }
   }

   SystemTimeToFileTime(&time, &s_lastLoadTime);
   
   return true;
}

bool TopMenuCore::loadInto(DWORD _pid)
{
   TopMenu_CSLock lock(s_cs);

   SetLastError(ERROR_SUCCESS);
   DWORD le;

   char szLibPath[MAX_PATH];
   if(!GetModuleFileName((HINSTANCE)&__ImageBase, szLibPath, MAX_PATH)) {
      le = GetLastError();
      return false;
   }

   HMODULE hKernel32 = ::GetModuleHandle("Kernel32");

   HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, _pid);
   if(!hProcess) {
      le = GetLastError();
      return false;
   }
   
   FILETIME ftCreationTime;
   FILETIME ftExitTime;
   FILETIME ftKernelTime;
   FILETIME ftUserTime;
   if(!GetProcessTimes(hProcess, &ftCreationTime, &ftExitTime, &ftKernelTime, &ftUserTime)) {
      le = GetLastError();
      ::CloseHandle(hProcess);
      return false;
   }

   if(CompareFileTime(&s_lastLoadTime, &ftCreationTime) > 0) {
      le = GetLastError();
      ::CloseHandle(hProcess);
      return false;
   }

#if defined(_M_X64)
   BOOL isWow64Proc = FALSE;
   if(!IsWow64Process(hProcess, &isWow64Proc) || isWow64Proc) {
      le = GetLastError();
      ::CloseHandle(hProcess);
      return false;
   }
#endif

   HMODULE hMods[1024];
   DWORD cbNeeded;
   if(!EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
   {
      le = GetLastError();
      ::CloseHandle(hProcess);
      return false;
   }

   for(DWORD i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
   {
      char szModName[MAX_PATH];
      if(GetModuleFileNameEx(hProcess, hMods[i], szModName, sizeof(szModName) / sizeof(char)))
      {
         if(_strnicmp(szModName, szLibPath, MAX_PATH) == 0) {
            ::CloseHandle(hProcess);
            return true; // already loaded
         }
      }
   }

   // 1. Allocate memory in the remote process for szLibPath
   void* pLibRemote = ::VirtualAllocEx(hProcess, NULL, sizeof(szLibPath), MEM_COMMIT, PAGE_READWRITE);
   if(!pLibRemote) {
      le = GetLastError();
      ::CloseHandle(hProcess);
      return false;
   }
   
   // 2. Write szLibPath to the allocated memory
   if(!::WriteProcessMemory(hProcess, pLibRemote, (void*)szLibPath, sizeof(szLibPath), NULL)) {
      le = GetLastError();
      ::VirtualFreeEx(hProcess, pLibRemote, 0, MEM_RELEASE); //lint !e534 JLD
      ::CloseHandle(hProcess);
      return false;
   }

   // Load into the remote process
   // (via CreateRemoteThread & LoadLibrary)
   HANDLE hThread = ::CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE) ::GetProcAddress(hKernel32,"LoadLibraryA"), pLibRemote, 0, NULL);
   if(!hThread) {
      le = GetLastError();
      ::VirtualFreeEx(hProcess, pLibRemote, 0, MEM_RELEASE); //lint !e534 JLD
      ::CloseHandle(hProcess);
      return false;
   }

   ::WaitForSingleObject(hThread, INFINITE); //lint !e534 JLD

   // Get handle of the loaded module
   DWORD hLibModule;
   ::GetExitCodeThread(hThread, &hLibModule); //lint !e534 JLD
   if(!hLibModule) {
      le = GetLastError();
      ::CloseHandle(hThread); //lint !e534 JLD
      ::VirtualFreeEx(hProcess, pLibRemote, 0, MEM_RELEASE); //lint !e534 JLD
      ::CloseHandle(hProcess);
      return false;
   }

   // Clean up
   ::CloseHandle(hThread); //lint !e534 JLD
   ::VirtualFreeEx(hProcess, pLibRemote, 0, MEM_RELEASE); //lint !e534 JLD
   ::CloseHandle(hProcess);

   return true;
} //lint !e550 JLD

void TopMenuCore::note(const char* _noteText)
{
   SetEnvironmentVariable("TopMenuCore_Note", _noteText); //lint !e534 JLD
}

void TopMenuCore::status(const char* _statusText)
{
   SetEnvironmentVariable("TopMenuCore_Status", _statusText); //lint !e534 JLD
}

HANDLE TopMenuCore::createStopEvent()
{
   EveryoneAccess ea;
   return CreateEvent(ea.getSA(), TRUE, FALSE, UNLOAD_EVENT_NAME);
}

DWORD WINAPI TopMenuCore::beginRun(LPVOID lpParameter)
{
   DWORD exitCode = 0;

   try {
      TopMenuCore core((HINSTANCE)lpParameter);
      if(!core.run()) {
         exitCode = 1;
      }
   }
   catch(...) { }
   
   FreeLibraryAndExitThread((HINSTANCE)lpParameter, exitCode);
   return exitCode;
}

TopMenuCore::TopMenuCore(HMODULE _hModule)
 : m_hModule(_hModule)
 , m_hStopEvent(NULL)
 , m_pTopEntry(NULL)
{
   note(""); // clear from any prior run
   m_hStopEvent = createStopEvent();
}

bool TopMenuCore::run()
{
   if(!startup()) {
      TopMenuCore::status("ActivateFailure");
   }

   TopMenuCore::status("Activated");
   waitForStopEvent(); //lint !e534 JLD
   TopMenuCore::status("Deactivating");

   if(!shutdown()) {
      TopMenuCore::status("DeactivateFailure");
      return false;
   }

   return true;
}

bool TopMenuCore::startup()
{
   SetLastError(ERROR_SUCCESS);
   DWORD le;

   if(!m_hStopEvent) {
      note("could not create stop event");
      return false;
   }
   
   char szLibPath[MAX_PATH];
   if(!GetModuleFileName((HINSTANCE)&__ImageBase, szLibPath, MAX_PATH)) {
      le = GetLastError();
      return false;
   }

   char szLibDrive[MAX_PATH];
   char szLibDir[MAX_PATH];
   char szLibFname[MAX_PATH];
   char szLibExt[MAX_PATH];
   _splitpath_s(szLibPath, szLibDrive, szLibDir, szLibFname, szLibExt); //lint !e534 JLD
   _makepath_s(szLibPath, szLibDrive, szLibDir, "*", "dll"); //lint !e534 JLD
   
   WIN32_FIND_DATA findData;
   HANDLE hSearch = FindFirstFile(szLibPath, &findData);
   if(hSearch) {
      do 
      {
         char szPath[MAX_PATH];
         _makepath_s(szPath, szLibDrive, szLibDir, findData.cFileName, ""); //lint !e534 JLD

         HMODULE hPluginMod = LoadLibrary(szPath);
         if(!hPluginMod) {
            le = GetLastError();
            continue;
         }

         FARPROC createProc = GetProcAddress(hPluginMod, CREATE_PROC_NAME);
         if(!createProc) {
            le = GetLastError();
            FreeLibrary(hPluginMod); //lint !e534 JLD
            continue;
         }

         FARPROC versionProc = GetProcAddress(hPluginMod, VERSION_PROC_NAME);
         if(!versionProc) {
            le = GetLastError();
            FreeLibrary(hPluginMod); //lint !e534 JLD
            continue;
         }
         
         TopMenu_PluginStackEntry* pStackPtr = m_pTopEntry;
         while(pStackPtr) {
            if(pStackPtr->m_hModule == hPluginMod) {
               break; // module already in stack
            }
         }
         if(pStackPtr) {
            FreeLibrary(hPluginMod); //lint !e534 JLD
            continue; // plugin already loaded
         }
      
         VERSION_PROC& GetPluginVersion = (VERSION_PROC&)*versionProc;
         DWORD pluginVersion = GetPluginVersion();
         if(pluginVersion < TopMenu_Plugin::GetPluginVersion()) {
            le = GetLastError();
            FreeLibrary(hPluginMod); //lint !e534 JLD
            continue;
         }

         CREATE_PROC& CreatePlugin = (CREATE_PROC&)*createProc;
         TopMenu_Plugin* pPlugin = CreatePlugin();
         if(!pPlugin) {
            le = GetLastError();
            FreeLibrary(hPluginMod); //lint !e534 JLD
            continue;
         }

         TopMenu_PluginStackEntry* pPluginEntry = new TopMenu_PluginStackEntry();
         pPluginEntry->m_hModule = hPluginMod;
         pPluginEntry->m_pPlugin = pPlugin;
         if(m_pTopEntry) {
            pPluginEntry->m_pNext = m_pTopEntry;
         }
         m_pTopEntry = pPluginEntry;
      } while(FindNextFile(hSearch, &findData));

      FindClose(hSearch); //lint !e534 JLD
      hSearch = NULL;
   }

   return true;
} //lint !e550 JLD

bool TopMenuCore::shutdown()
{
   TopMenu_PluginStackEntry* pStackPtr = m_pTopEntry;
   while(pStackPtr) {
      HMODULE hMod = pStackPtr->m_hModule;

      FARPROC destroyProc = GetProcAddress(hMod, DESTROY_PROC_NAME);
      if(destroyProc) {
         DESTROY_PROC& DestroyPlugin = (DESTROY_PROC&)*destroyProc;
         DestroyPlugin(pStackPtr->m_pPlugin);
      }

      FreeLibrary(hMod); //lint !e534 JLD

      TopMenu_PluginStackEntry* pCurrent = pStackPtr;
      pStackPtr = pStackPtr->m_pNext;
      delete pCurrent;
      pCurrent = NULL;
   }

   CloseHandle(m_hStopEvent); //lint !e534 JLD
   m_hStopEvent = NULL;

   return true;
}

bool TopMenuCore::waitForStopEvent()
{
   DWORD dwReason = WaitForSingleObject(m_hStopEvent, INFINITE);
   return (dwReason == WAIT_OBJECT_0 || dwReason == WAIT_ABANDONED);
}

bool TopMenuCore::setStopEvent()
{
   if(!m_hStopEvent) {
      m_hStopEvent = createStopEvent();
   }
   if(!SetEvent(m_hStopEvent)) {
      return false;
   }

   return true;
}
