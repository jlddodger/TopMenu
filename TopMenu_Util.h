
#pragma once

#include "windows.h"
#include "CrtDbg.h"
#include "aclapi.h"
#include "sddl.h"

#if defined(_M_X64)
   char dllPath[] = "TopMenuCore64.dll";
   char otherBitPath[] = "TopMenuTray32.exe";
#elif defined(_M_IX86)
   char dllPath[] = "TopMenuCore32.dll";
   //char otherBitPath[] = "TopMenuTest64.exe";
#endif

#ifdef _DEBUG
#define ASSERT_EXPR(expr, msg, ...) \
        (void) ((!!(expr)) || \
                (dbgRpt(_CRT_ASSERT, __FILE__, __LINE__, NULL, msg, __VA_ARGS__), 1) || \
                (1 != _CrtDbgReport(_CRT_ASSERT, __FILE__, __LINE__, NULL, msg, __VA_ARGS__)) || \
                (_CrtDbgBreak(), 0))

#define ASSERT(expr)   ASSERT_EXPR((expr), NULL)

#define ASSERTE(expr, ...)  ASSERT_EXPR((expr), #expr)

#define VERIFY(f) ASSERT(f)

int dbgRpt(int reportType, const char *filename, int linenumber, const char *moduleName, const char *format, ...)
{
   return 0;
}
#else
#define ASSERT_EXPR(expr, msg, ...) 
#define ASSERT(expr) 
#define ASSERTE(expr, ...) 
#define VERIFY(f) ((void)(f))
#endif

class TopMenu_CS
{
   CRITICAL_SECTION cs;

public:
   TopMenu_CS()
      : cs()
      { InitializeCriticalSection(&cs); }

   ~TopMenu_CS()
      { DeleteCriticalSection(&cs); }

   bool tryEnter()
      { return (TryEnterCriticalSection(&cs) != FALSE); }

   void enter()
      { EnterCriticalSection(&cs); }

   void leave()
      { LeaveCriticalSection(&cs); }
};

class TopMenu_CSLock
{
   TopMenu_CS& m_mutex;

public:
   TopMenu_CSLock(TopMenu_CS& _mutex)
      : m_mutex(_mutex)
      { m_mutex.enter(); }

   ~TopMenu_CSLock()
      { m_mutex.leave(); }
};

class TopMenu_Hook
{
   HHOOK m_hHook;

public:
   TopMenu_Hook()
      : m_hHook(NULL)
      { }

   void set(HHOOK _hHook)
      { m_hHook = _hHook; }

   void reset()
      { if(m_hHook) { BOOL b=UnhookWindowsHookEx(m_hHook); ASSERT(b); m_hHook = NULL; } }

   ~TopMenu_Hook()
      { reset(); }
};

class EveryoneAccess
{
   PSECURITY_DESCRIPTOR pLowIntegritySD;
   SECURITY_DESCRIPTOR sD;
   SECURITY_ATTRIBUTES attr;
   PACL pSacl;
   BOOL fSaclDefaulted;
   BOOL fSaclPresent;

public:
   EveryoneAccess()
      : pLowIntegritySD(NULL)
      , sD()
      , attr()
      , pSacl(NULL)
      , fSaclDefaulted(FALSE)
      , fSaclPresent(FALSE)
      {}

   LPSECURITY_ATTRIBUTES getSA()
   {
      // Set the security on the events and threads so that Acrobat under protected mode can open them
      VERIFY(InitializeSecurityDescriptor(&sD,SECURITY_DESCRIPTOR_REVISION));

      // Create a sacl that allows access from low-integrity processes, and add that to our Securoty Descriptor.
      if (ConvertStringSecurityDescriptorToSecurityDescriptor("S:(ML;;NW;;;LW)", SDDL_REVISION_1, &pLowIntegritySD, NULL)){
         if (GetSecurityDescriptorSacl(pLowIntegritySD, &fSaclPresent, &pSacl, &fSaclDefaulted)){
            SetSecurityDescriptorSacl(&sD,fSaclPresent,pSacl,fSaclDefaulted);  
         }
      }

      // set a NULL dacl so everyone has access
      VERIFY(SetSecurityDescriptorDacl(&sD,TRUE,NULL,FALSE));       
      attr.nLength = sizeof(SECURITY_ATTRIBUTES);
      attr.lpSecurityDescriptor = &sD;
      attr.bInheritHandle = FALSE;

      return &attr;
   }
};

BOOL IsWin7OrLater()
{
   // Initialize the OSVERSIONINFOEX structure.
   OSVERSIONINFOEX osvi;
   ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
   osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
   osvi.dwMajorVersion = 6;
   osvi.dwMinorVersion = 1;

   // Initialize the condition mask.
   DWORDLONG dwlConditionMask = 0;
   VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
   VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);

   // Perform the test.
   return VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION, dwlConditionMask);
}