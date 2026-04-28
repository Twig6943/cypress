#ifndef ASSET_HPP
#define ASSET_HPP

#include "DataContainer.h"

namespace fb
{
	class Asset : public DataContainer // size = 0x10
	{
	public:
		const char* Name; //0x10

		static inline class ClassInfo* c_TypeInfo = reinterpret_cast<ClassInfo*>(0x1430B42D0);
	}; //size = 0x18
} //namespace fb

#endif
