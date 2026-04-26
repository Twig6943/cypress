#pragma once

#include "fb/TypeInfo/EventSyncEntityData.h"
#include "fb/Engine/EntityBus.h"
#include <EASTL/map.h>


namespace fb
{
	//or whatever it's called
	struct ServerEventSyncListenerIds
	{
		uintptr_t m_owner; //msg + 0x58
		uint32_t m_bus; //msg + 0x60
		uint32_t m_data; //msg + 0x48 

		//this was inlined somewhere, not sure what it does but it's needed
		//for the map iterator
		bool operator<(const ServerEventSyncListenerIds& listener) const
		{
			if (m_owner != listener.m_owner)
				return m_owner < listener.m_owner;
			if (m_bus != listener.m_bus)
				return m_bus < listener.m_bus;
			else
				return m_data < listener.m_data;

		}
	};

	class ServerEventSyncEntity
	{
	public:
		class Listener {
		public:
			char pad_0000[0x10];
			eastl::map<fb::ServerEventSyncListenerIds, fb::ServerEventSyncEntity*> m_entities;
		};
		virtual class ClassInfo* getType();

		char pad_0000[0x18];
		fb::EntityBus* m_entityBus;
		fb::EventSyncEntityData* m_data;

		static inline class ClassInfo* c_TypeInfo = reinterpret_cast<ClassInfo*>(0x14313A970);
	};
}