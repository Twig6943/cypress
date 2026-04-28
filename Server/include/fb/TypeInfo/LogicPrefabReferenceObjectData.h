#ifndef LOGICPREFABREFERENCEOBJECTDATA_HPP
#define LOGICPREFABREFERENCEOBJECTDATA_HPP

namespace fb
{
	class Asset;

	class LogicPrefabReferenceObjectData //lazy class
	{
	public:
		char pad_0000[0x18];
		fb::Asset* m_blueprint;

		static inline class ClassInfo* c_TypeInfo = reinterpret_cast<ClassInfo*>(0x1430DC8F0);
	}; //size = 0xA0
} //namespace fb

#endif
