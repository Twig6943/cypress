#ifndef GAMEPLATFORM_H
#define GAMEPLATFORM_H

namespace fb
{
#ifdef CYPRESS_GW1
	enum GamePlatform
	{
		GamePlatform_Ps3 = 0x0,
		GamePlatform_Win32 = 0x1,
		GamePlatform_Xenon = 0x2,
		GamePlatform_Gen4a = 0x3,
		GamePlatform_Gen4b = 0x4,
		GamePlatform_Any = 0x5,
		GamePlatform_Invalid = 0x6,
		GamePlatformCount = 0x7
	};
#elif defined CYPRESS_GW2
	enum GamePlatform
	{
		GamePlatform_Ps3 = 0x0,
		GamePlatform_Win32 = 0x1,
		GamePlatform_Xenon = 0x2,
		GamePlatform_Gen4a = 0x3,
		GamePlatform_Gen4b = 0x4,
		GamePlatform_Android = 0x5,
		GamePlatform_iOS = 0x6,
		GamePlatform_OSX = 0x7,
		GamePlatform_Linux = 0x8,
		GamePlatform_Any = 0x9,
		GamePlatform_Invalid = 0xA,
		GamePlatformCount = 0xB
	};
#endif

	const char* toString(GamePlatform platform)
	{
		return reinterpret_cast<const char*(*)(GamePlatform)>(CYPRESS_GW_SELECT(0x141449180, 0x141DE68D0, 0))(platform);
	}

} //namespace fb

#endif
