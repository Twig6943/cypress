#pragma once
#include <fb/TypeInfo/Asset.h>
#include <fb/Engine/Array.h>
#include "WeaponSlot.h"
#include "PVZCharacterWeaponUnlockAsset.h"
#include <set>

namespace fb
{
	//very lazy implementation of this class
	//made it this way to avoid including unnecessary typeinfo
	class PVZCharacterCustomizationAsset : public fb::Asset
	{
	public:
		char pad_0000[56]; //0x0000
		fb::Asset* m_soldier; //0x0038
		void* m_weaponTable; //0x0040

		fb::Array<void*> getUnlockParts() {
			fb::Array<void*> UnlockParts = ptrread<fb::Array<void*>>(m_weaponTable, 0x10);
			if (UnlockParts.size() == 0)
			{
				CYPRESS_LOGMESSAGE(LogLevel::Error, "{} kit has no UnlockParts!", this->Name);
			}

			return UnlockParts;
		}

		std::set<PVZCharacterWeaponUnlockAsset*> getWeaponsInSlot(fb::WeaponSlot inSlot) {
			fb::Array<void*> UnlockParts = getUnlockParts();
			std::set<PVZCharacterWeaponUnlockAsset*> result;

			for (int i = 0; i < UnlockParts.size(); i++)
			{
				void* weaponCustomizationUnlockParts = UnlockParts.at(i);
				if (!weaponCustomizationUnlockParts)
					continue;

				fb::WeaponSlot slot = ptrread<fb::WeaponSlot>(weaponCustomizationUnlockParts, 0x30);

				if (slot != inSlot)
					continue;

				fb::Array<void*> SelectableUnlock = ptrread<fb::Array<void*>>(weaponCustomizationUnlockParts, 0x20);

				for (int j = 0; j < SelectableUnlock.size(); j++)
				{
					result.insert(reinterpret_cast<fb::PVZCharacterWeaponUnlockAsset*>(SelectableUnlock[j]));
				}
			}

			return result;
		}

		static inline class ClassInfo* c_TypeInfo = reinterpret_cast<ClassInfo*>(0x1430D3F70);
	};
}
