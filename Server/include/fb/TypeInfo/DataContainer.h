#ifndef DATACONTAINER_HPP
#define DATACONTAINER_HPP

#include <fb/Engine/ITypedObject.h>

namespace fb
{
	class DataContainer : public ITypedObject
	{
	public:
		char pad_0x0[0x8];

		static inline ClassInfo* c_TypeInfo = reinterpret_cast<ClassInfo*>(0x1430B4190);
	}; //size = 0x10
} //namespace fb

#endif
