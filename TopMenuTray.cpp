
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include <windows.h>
#include <shellapi.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include "TopMenuTray.h"
#include "TopMenu_Util.h"

BOOL InitInstance(HINSTANCE, int);
void ExitInstance(HINSTANCE);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

void load();
void refresh();
void unload();
void launch32();

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
   UNREFERENCED_PARAMETER(hPrevInstance);
   UNREFERENCED_PARAMETER(lpCmdLine);

   const char* first = NULL;
   if(__argc > 1) {
      if(_strnicmp(__argv[1], "load", 4) == 0) {
         load();
         return FALSE;
      }
      if(_strnicmp(__argv[1], "refresh", 4) == 0) {
         refresh();
         return FALSE;
      }
      else if(_strnicmp(__argv[1], "unload", 4) == 0) {
         unload();
         return FALSE;
      }
   }

   if(!InitInstance (hInstance, nCmdShow))
   {
      return FALSE;
   }

   HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TOPMENUTRAY));

   // Main message loop:
   MSG msg;
   while (GetMessage(&msg, NULL, 0, 0))
   {
      if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
      {
         TranslateMessage(&msg);
         DispatchMessage(&msg);
      }
   }

   ExitInstance(hInstance);

   return (int) msg.wParam;
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   WNDCLASSEX wcex;
   wcex.cbSize = sizeof(WNDCLASSEX);

   wcex.style        = CS_HREDRAW | CS_VREDRAW;
   wcex.lpfnWndProc  = WndProc;
   wcex.cbClsExtra   = 0;
   wcex.cbWndExtra   = 0;
   wcex.hInstance    = hInstance;
   wcex.hIcon        = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TOPMENUTRAY));
   wcex.hCursor      = LoadCursor(NULL, IDC_ARROW);
   wcex.hbrBackground= (HBRUSH)(COLOR_WINDOW+1);
   wcex.lpszMenuName = MAKEINTRESOURCE(IDC_TOPMENUTRAY);
   wcex.lpszClassName= "MainWindow";
   wcex.hIconSm      = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_TOPMENUTRAY));
   RegisterClassEx(&wcex);

   HWND hWnd = CreateWindow("MainWindow", "", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
   if (!hWnd)
   {
      return FALSE;
   }
   
   NOTIFYICONDATA trayData;
   ZeroMemory(&trayData, sizeof(trayData));
   trayData.cbSize = sizeof(trayData);
   trayData.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON | NIF_STATE;
   trayData.hWnd = hWnd;
   trayData.uID = IDI_TOPMENUTRAY;
   trayData.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TOPMENUTRAY));
   trayData.uCallbackMessage = WM_APP;
   trayData.uTimeout = 5000;
   trayData.dwInfoFlags = NIIF_NONE;
   trayData.dwState = 0;
   trayData.dwStateMask = 0;
   trayData.uVersion = NOTIFYICON_VERSION;
   Shell_NotifyIcon(NIM_ADD, &trayData);

   return TRUE;
}

void ExitInstance(HINSTANCE hInstance)
{
   NOTIFYICONDATA trayData;
   ZeroMemory(&trayData, sizeof(trayData));
   trayData.cbSize = sizeof(trayData);
   trayData.uID = IDI_TOPMENUTRAY;
   Shell_NotifyIcon(NIM_DELETE, &trayData);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
   int wmId, wmEvent, x, y;
   PAINTSTRUCT ps;
   HDC hdc;
   DWORD rc;

   switch (message)
   {
   case WM_COMMAND:
      wmId    = LOWORD(wParam);
      wmEvent = HIWORD(wParam);
      // Parse the menu selections:
      switch (wmId)
      {
      case IDM_LOAD:
         rc = (DWORD) ShellExecute(NULL, "open", __argv[0], "load", NULL, SW_HIDE);
         if(rc <= 32) {
            MessageBox(NULL, "Could not launch loader.", __argv[0], MB_OK);
         }
         break;
      case IDM_UNLOAD:
         rc = (DWORD) ShellExecute(NULL, "open", __argv[0], "unload", NULL, SW_HIDE);
         if(rc <= 32) {
            MessageBox(NULL, "Could not launch unloader.", __argv[0], MB_OK);
         }
         break;
      case IDM_ABOUT:
         DialogBox(NULL, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
         break;
      case IDM_EXIT:
         DestroyWindow(hWnd);
         break;
      default:
         return DefWindowProc(hWnd, message, wParam, lParam);
      }
      break;
   case WM_APP:
      wmEvent = LOWORD(lParam); //contains notification events, such as NIN_BALLOONSHOW, NIN_POPUPOPEN, or WM_CONTEXTMENU.
      wmId = HIWORD(lParam); //contains the icon ID. Icon IDs are restricted to a length of 16 bits.
      x = LOWORD(wParam);
      y = HIWORD(wParam);
      if(wmEvent == WM_RBUTTONUP) {
         POINT pos;
         GetCursorPos(&pos);

         HMENU hMenu = CreatePopupMenu();
         AppendMenu(hMenu, MF_STRING,     IDM_LOAD,   "Load");
         AppendMenu(hMenu, MF_STRING,     IDM_UNLOAD, "Unload");
         AppendMenu(hMenu, MF_SEPARATOR,  0,          NULL);
         AppendMenu(hMenu, MF_STRING,     IDM_ABOUT,  "About");
         AppendMenu(hMenu, MF_SEPARATOR,  0,          NULL);
         AppendMenu(hMenu, MF_STRING,     IDM_EXIT,   "Exit");
         TrackPopupMenu(hMenu, TPM_LEFTALIGN|TPM_BOTTOMALIGN, pos.x, pos.y, 0, hWnd, NULL);
         DestroyMenu(hMenu);
      }
      break;
   case WM_PAINT:
      hdc = BeginPaint(hWnd, &ps);
      // TODO: Add any drawing code here...
      EndPaint(hWnd, &ps);
      break;
   case WM_DESTROY:
      PostQuitMessage(0);
      break;
   default:
      return DefWindowProc(hWnd, message, wParam, lParam);
   }
   return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
   UNREFERENCED_PARAMETER(lParam);
   switch (message)
   {
   case WM_INITDIALOG:
      return (INT_PTR)TRUE;

   case WM_COMMAND:
      if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
      {
         EndDialog(hDlg, LOWORD(wParam));
         return (INT_PTR)TRUE;
      }
      break;
   }
   return (INT_PTR)FALSE;
}

void load()
{
   launch32();

   HMODULE hDll = LoadLibrary(dllPath);

   typedef bool (WINAPI *PBOOLFNC)();
   PBOOLFNC pEnableFnc = (PBOOLFNC) GetProcAddress(hDll, "enable");
   if(pEnableFnc) {
      (*pEnableFnc)();
   }

   FreeLibrary(hDll); //lint !e534 JLD
}

void refresh()
{
   launch32();

   HMODULE hDll = LoadLibrary(dllPath);

   typedef bool (WINAPI *PBOOLFNC)();
   PBOOLFNC pRefreshFnc = (PBOOLFNC) GetProcAddress(hDll, "refresh");
   if(pRefreshFnc) {
      (*pRefreshFnc)();
   }

   FreeLibrary(hDll); //lint !e534 JLD
}

void unload()
{
   HMODULE hDll = LoadLibrary(dllPath);

   typedef bool (WINAPI *PBOOLFNC)();
   PBOOLFNC pDisableFnc = (PBOOLFNC) GetProcAddress(hDll, "disable");
   if(pDisableFnc) {
      (*pDisableFnc)();
   }

   FreeLibrary(hDll); //lint !e534 JLD
}

void launch32()
{   
#if defined(_M_X64)
   char modPath[MAX_PATH];
   GetModuleFileName(NULL, modPath, sizeof(modPath));

   char modDrive[MAX_PATH];
   char modDir[MAX_PATH];
   char modFile[MAX_PATH];
   char modExt[MAX_PATH];
   _splitpath_s(modPath, modDrive, modDir, modFile, modExt);
   
   char otherPath[MAX_PATH];
   otherPath[0] = '\0';
   strcat_s(otherPath, modDrive);
   strcat_s(otherPath, modDir);
   strcat_s(otherPath, otherBitPath);
   
   char args[MAX_PATH];
   args[0] = '\0';
   if(__argv) {
      for(int i=1; i < __argc; i++) {
         strcat_s(args, "\"");
         strcat_s(args, __argv[i]);
         strcat_s(args, "\" ");
      }
   }

   if(GetFileAttributes(otherPath) != INVALID_FILE_ATTRIBUTES) {
      DWORD rc = (DWORD) ShellExecute(NULL, "open", otherPath, args, NULL, SW_HIDE);
      if(rc <= 32) {
         MessageBox(NULL, "Could not launch 32-bit loader.", __argv[0], MB_OK);
      }
   }
#endif
}