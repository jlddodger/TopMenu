
#pragma once

#include "windows.h"

// function to be exported from dll to create instance of interface
#ifdef POWERMENUCORE
#	define DLL_DECLSPEC __declspec(dllimport)
#else
#	define DLL_DECLSPEC __declspec(dllexport)
#endif 

extern "C"
{
   typedef DWORD (VERSION_PROC)();
   const char VERSION_PROC_NAME[] = "TopMenu_GetPluginVersion";
   DLL_DECLSPEC DWORD TopMenu_GetPluginVersion();

   typedef class TopMenu_Plugin* (CREATE_PROC)();
   const char CREATE_PROC_NAME[] = "TopMenu_CreatePlugin";
   DLL_DECLSPEC TopMenu_Plugin* TopMenu_CreatePlugin();

   typedef void (DESTROY_PROC)(class TopMenu_Plugin*);
   const char DESTROY_PROC_NAME[] = "TopMenu_DestroyPlugin";
   DLL_DECLSPEC void TopMenu_DestroyPlugin(class TopMenu_Plugin* _pPlugin);
}

class TopMenu_Plugin
{
public:
   static DWORD GetPluginVersion() { return 1; }

public:
   virtual ~TopMenu_Plugin() {};
};