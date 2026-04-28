#ifndef SETTINGENTITYDATA_HPP
#define SETTINGENTITYDATA_HPP

namespace fb
{
	//lazy class
	class SettingEntityData
	{
	public:
		char pad_0x0[0x10];
		int flags;
		char pad_0x10[0x4];
		int Realm; //0x18
		char pad_0x1C[0x4];
		const char* BoolSettingName; //0x20
		const char* IntSettingName; //0x28
		const char* UIntSettingName; //0x30
		const char* FloatSettingName; //0x38
		const char* StringSettingName; //0x40
		int IntSetting; //0x48
		float FloatSetting; //0x4C
		unsigned int UIntSetting; //0x50
		char pad_0x54[0x4];
		const char* StringSetting; //0x58
		int ClientIntSetting; //0x60
		float ClientFloatSetting; //0x64
		unsigned int ClientUIntSetting; //0x68
		char pad_0x6C[0x4];
		const char* ClientStringSetting; //0x70
		bool BoolSetting; //0x78
		bool ClientBoolSetting; //0x79
		bool SetOnPropertyChanged; //0x7A
		bool SetFromClientOnPropertyChanged; //0x7B
		bool SetOnInit; //0x7C
		bool ReSyncSettingsOnChanged; //0x7D
		char pad_0x7E[0x2];

		static inline class TypeInfo* c_TypeInfo = reinterpret_cast<TypeInfo*>(0x1430D7DB0);

		//scan the five SettingEntityData's fields for the setting name
		const char* getSettingName()
		{
			const char* result = nullptr;

			//not sure if SettingEntities support having more than one field written
			const char* fields[] = {
				this->BoolSettingName,
				this->IntSettingName,
				this->UIntSettingName,
				this->FloatSettingName,
				this->StringSettingName
			};

			for (const char* field : fields)
			{
				if (field != (const char*)0x14294ED54)
				{
					if (result)
						return "SettingEntityData has more than one field written, this is not supported.";
					result = field;
				}
			}

			return result ? result : "No setting field is set.";
		}

		//horrible code but it works :p
		std::string getSettingValue()
		{
			std::string result;

			if (BoolSettingName != (const char*)0x14294ED54)
				result = BoolSetting ? "true" : "false";

			if (IntSettingName != (const char*)0x14294ED54)
			{
				if (!result.empty()) return "Multiple fields set (unsupported)";
				result = std::format("{}", IntSetting);
			}

			if (UIntSettingName != (const char*)0x14294ED54)
			{
				if (!result.empty()) return "Multiple fields set (unsupported)";
				result = std::format("{}", UIntSetting);
			}

			if (FloatSettingName != (const char*)0x14294ED54)
			{
				if (!result.empty()) return "Multiple fields set (unsupported)";
				result = std::format("{}", FloatSetting);
			}

			if (StringSettingName != (const char*)0x14294ED54)
			{
				if (!result.empty()) return "Multiple fields set (unsupported)";
				result = StringSetting ? StringSetting : "null";
			}

			return result.empty() ? "No setting field is set." : result;
		}
	}; //size = 0x80
} //namespace fb

#endif
