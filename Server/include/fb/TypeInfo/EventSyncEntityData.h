#ifndef EVENTSYNCENTITYDATA_HPP
#define EVENTSYNCENTITYDATA_HPP

namespace fb
{
	class EventSyncEntityData //lazy class
	{
	public:
		char pad_0000[0x10];
		unsigned int m_flags;

		static inline class ClassInfo* c_TypeInfo = reinterpret_cast<ClassInfo*>(0x1430D7270);
	}; //size = 0x18
} //namespace fb

#endif
