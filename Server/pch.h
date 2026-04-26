#ifndef PCH_H
#define PCH_H

#include "framework.h"
#include <cstdint>
#include <MemUtil.h>
#include <MinHook.h>
#include <Core/VersionInfo.h>
#include <Core/Logging.h>
#include <Core/Assert.h>
#include <Core/Config.h>
#include <ServerBanlist.h>
#include <ServerPlaylist.h>
#include <StringUtil.h>

#define EASTL_SIZE_T_32BIT 1
#define EASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED 1
#define EA_HAVE_CPP11_INITIALIZER_LIST 1
#define CHAR8_T_DEFINED 1

#include "eastl_arena_allocator.h"

#define EASTLAllocatorType fb::eastl_arena_allocator
#define EASTLAllocatorDefault fb::GetDefaultAllocator

#endif //PCH_H
