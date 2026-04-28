#pragma once
#include <fb/Engine/LevelSetup.h>
#include <EASTL/list.h>
#include <fb/Engine/String.h>
#ifdef CYPRESS_BFN
#include <fb/Engine/Array.h>
#endif

#define OFFSET_FB_SERVERCTOR CYPRESS_GW_SELECT(0x1406EEA10, 0x1405D5240, 0x140F12580)
#define OFFSET_FB_SERVERDTOR CYPRESS_GW_SELECT(0x1406EF8E0, 0x1405D5B30, 0x140F13570)
#define OFFSET_FB_SERVER_START CYPRESS_GW_SELECT(0x1406F8F80, 0x1405D5FE0, 0x140F23240)
#define OFFSET_FB_SERVER_UPDATE CYPRESS_GW_SELECT(0x1406F9C50, 0x14012D710, 0x140F24300)

#define OFFSET_FB_SERVERLOADLEVELMESSAGE_POST CYPRESS_GW_SELECT(0x1406F6FD0, 0x1405D85E0, 0x140F4D5D0)

#define OFFSET_MAINWND CYPRESS_GW_SELECT(0x141C28E08, 0x142932DF8, 0)
#define OFFSET_COMMANDBOX CYPRESS_GW_SELECT(0x141C28E20, 0x142932DE8, 0)
#define OFFSET_LISTBOX CYPRESS_GW_SELECT(0x141C28E10, 0x142932DA8, 0)
#define OFFSET_TOGGLELOGBUTTONBOX CYPRESS_GW_SELECT(0x141C28E18, 0x142932DE0, 0)
#define OFFSET_LOGOUTPUTENABLED 0x142932D9E

#define OFFSET_M_LOGOUTPUTENABLED 0x142932D9E
#define OFFSET_G_STATUSBOX CYPRESS_GW_SELECT(0x141C28E28, 0x142932DB8, 0)
#define OFFSET_WINDOWPROCEDURE CYPRESS_GW_SELECT(0x1400126C0, 0x140138790, 0x140590560)
#ifndef CYPRESS_BFN
#define OFFSET_EDITBOXWNDPROCPROXY CYPRESS_GW_SELECT(0x140009430, 0x140137DC0, 0)
#endif

#define WM_APP_UPDATETITLE					(WM_APP+1)
#define WM_APP_UPDATESTATUS					(WM_APP+2)
#define WM_APP_UPDATESTRINGLIST				(WM_APP+3)
#define WM_APP_SHOWAPPLICATION				(WM_APP+4)
#define WM_APP_SHOWCURSOR					(WM_APP+5)
#define WM_APP_SETCURSOR					(WM_APP+6)
#define WM_APP_CREATEWINDOW					(WM_APP+7)
#define WM_APP_CONFINECURSOR				(WM_APP+8)
#define WM_APP_ESCAPEPRESSED				(WM_APP+9)
#define WM_APP_ENABLEMINIMIZE				(WM_APP+10)
#define WM_APP_ACTIVATE_THREADSAFE			(WM_APP+11)
#define WM_APP_ACTIVATE						(WM_APP+12)
#define WM_APP_SHOWBUGSUBMITDIALOG			(WM_APP+13)
#define WM_APP_SETNORMALPRIO				(WM_APP+15)
#define WM_APP_SETLOWPRIO					(WM_APP+16)
#define WM_APP_SHUTDOWN						(WM_APP+17)
#define WM_APP_SHOWSURVEYDIALOG				(WM_APP+18)
#define WM_APP_CLOSEBUGSUBMITDIALOG			(WM_APP+19)
#define WM_APP_RESTOREWINDOW				(WM_APP+20)
#define WM_APP_DXGI_RESIZETARGET			(WM_APP+22)
#define WM_APP_DXGI_SETFULLSCREENSTATE		(WM_APP+23)
#define WM_APP_DXGI_RESIZEBUFFERS			(WM_APP+24)

namespace fb
{
    struct ServerSpawnInfo
    {
        ServerSpawnInfo(LevelSetup* setup)
            : levelSetup(setup)
            , tickFrequency(30)
            , isDedicated(true)
            , isSinglePlayer(false)
            , isLocalHost(false)
            , isEncrypted(false)
            , isCoop(false)
            , isMenu(false)
            , keepResources(false)
        {
#ifndef CYPRESS_BFN
            saveData = (__int64*)CYPRESS_GW_SELECT(0x141EAAFB8, 0x14294E450, 0);
#endif
        }

        void* fileSystem = nullptr;
        void* damageArbitrator = nullptr;
        void* playerManager = nullptr;
        LevelSetup* levelSetup;
        unsigned int tickFrequency = 0;
        bool isSinglePlayer = false;
        bool isLocalHost = false;
        bool isDedicated = false;
        bool isEncrypted = false;
        bool isCoop = false;
        bool isMenu = false;
        bool keepResources = false;
#ifdef CYPRESS_BFN
        Array<void*> saveData;
#else
        void* saveData = nullptr;
#endif
        void* serverCallbacks = nullptr;
        void* runtimeModules = nullptr;
        __int64* loadInfo = nullptr;
#ifdef CYPRESS_BFN
        void* unk1 = nullptr;
#endif
        unsigned int serverPort = 0;
        unsigned int validLocalPlayerMask = 1;
    };

    static void PostServerLoadLevelMessage(LevelSetup* setup, bool fadeOut, bool forceReloadResources)
    {
        auto ServerLoadLevelMessage__post = reinterpret_cast<void (*)(void* levelSetup, bool fadeOut, bool forceReloadResources)>(OFFSET_FB_SERVERLOADLEVELMESSAGE_POST);
        ServerLoadLevelMessage__post(setup, fadeOut, forceReloadResources);
    }
}
