#pragma once
#include <string>
#include <vector>
#include <Windows.h>
#include <bcrypt.h>
#include <iphlpapi.h>
#include <winioctl.h>
#include <sddl.h>
#include <nlohmann/json.hpp>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace Cypress
{
	struct HardwareFingerprint
	{
		std::vector<std::string> components;

		nlohmann::json toJson() const
		{
			return nlohmann::json(components);
		}

		static HardwareFingerprint fromJson(const nlohmann::json& j)
		{
			HardwareFingerprint fp;
			if (j.is_array())
			{
				for (const auto& v : j)
					if (v.is_string())
						fp.components.push_back(v.get<std::string>());
			}
			return fp;
		}
	};

	namespace detail
	{
		inline std::string sha256hex(const std::string& input)
		{
			BCRYPT_ALG_HANDLE hAlg = nullptr;
			BCRYPT_HASH_HANDLE hHash = nullptr;
			BYTE hash[32] = {};

			BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
			BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
			BCryptHashData(hHash, (PUCHAR)input.data(), (ULONG)input.size(), 0);
			BCryptFinishHash(hHash, hash, sizeof(hash), 0);
			BCryptDestroyHash(hHash);
			BCryptCloseAlgorithmProvider(hAlg, 0);

			char hex[65];
			for (int i = 0; i < 32; ++i)
				snprintf(hex + i * 2, 3, "%02x", hash[i]);
			hex[64] = '\0';
			return std::string(hex);
		}

		inline std::string hashComponent(const std::string& type, const std::string& value)
		{
			return sha256hex(type + ":" + value);
		}

		inline std::string getRegistryString(HKEY root, const char* subKey, const char* valueName)
		{
			char buf[512] = {};
			DWORD bufLen = sizeof(buf);
			if (RegGetValueA(root, subKey, valueName, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &bufLen) == ERROR_SUCCESS)
				return std::string(buf);
			return "";
		}

		inline std::string getWmiSingleValue(const char* wmiClass, const char* property)
		{
			// unused for now, keeping it simple without COM/WMI
			return "";
		}
	}

	inline HardwareFingerprint GenerateHardwareFingerprint()
	{
		HardwareFingerprint fp;

		// machine guid
		{
			std::string val = detail::getRegistryString(
				HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", "MachineGuid");
			if (!val.empty())
				fp.components.push_back(detail::hashComponent("machine_guid", val));
		}

		// physical disk serials (burned into firmware, survives format)
		{
			for (int i = 0; i < 16; ++i)
			{
				char path[32];
				snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%d", i);
				HANDLE hDrive = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
				if (hDrive == INVALID_HANDLE_VALUE) continue;

				STORAGE_PROPERTY_QUERY query = {};
				query.PropertyId = StorageDeviceProperty;
				query.QueryType = PropertyStandardQuery;

				BYTE outBuf[1024] = {};
				DWORD bytesReturned = 0;
				if (DeviceIoControl(hDrive, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
					outBuf, sizeof(outBuf), &bytesReturned, nullptr))
				{
					auto* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(outBuf);
					if (desc->SerialNumberOffset && desc->SerialNumberOffset < bytesReturned)
					{
						std::string serial(reinterpret_cast<const char*>(outBuf + desc->SerialNumberOffset));
						// trim whitespace
						while (!serial.empty() && serial.back() == ' ') serial.pop_back();
						while (!serial.empty() && serial.front() == ' ') serial.erase(serial.begin());
						if (!serial.empty())
							fp.components.push_back(detail::hashComponent("disk_serial", serial));
					}
				}
				CloseHandle(hDrive);
			}
		}

		// mac addresses (physical adapters only)
		{
			ULONG bufSize = 0;
			GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, nullptr, &bufSize);
			if (bufSize > 0)
			{
				std::vector<BYTE> buffer(bufSize);
				auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
				if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, adapters, &bufSize) == NO_ERROR)
				{
					for (auto* a = adapters; a; a = a->Next)
					{
						if (a->PhysicalAddressLength == 0) continue;
						if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
						if (a->IfType != IF_TYPE_ETHERNET_CSMACD && a->IfType != IF_TYPE_IEEE80211) continue;

						char mac[18];
						snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
							a->PhysicalAddress[0], a->PhysicalAddress[1], a->PhysicalAddress[2],
							a->PhysicalAddress[3], a->PhysicalAddress[4], a->PhysicalAddress[5]);
						fp.components.push_back(detail::hashComponent("mac_addr", std::string(mac)));
					}
				}
			}
		}

		// windows sid
		{
			HANDLE token = nullptr;
			if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			{
				DWORD sidSize = 0;
				GetTokenInformation(token, TokenUser, nullptr, 0, &sidSize);
				if (sidSize > 0)
				{
					std::vector<BYTE> sidBuf(sidSize);
					if (GetTokenInformation(token, TokenUser, sidBuf.data(), sidSize, &sidSize))
					{
						auto* tokenUser = reinterpret_cast<TOKEN_USER*>(sidBuf.data());
						char* sidStr = nullptr;
						if (ConvertSidToStringSidA(tokenUser->User.Sid, &sidStr))
						{
							fp.components.push_back(detail::hashComponent("windows_sid", std::string(sidStr)));
							LocalFree(sidStr);
						}
					}
				}
				CloseHandle(token);
			}
		}

		// windows product id
		{
			std::string val = detail::getRegistryString(
				HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "ProductId");
			if (!val.empty())
				fp.components.push_back(detail::hashComponent("product_id", val));
		}

		// bios serial
		{
			std::string val = detail::getRegistryString(
				HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "SystemSerialNumber");
			if (!val.empty() && val != "System Serial Number" && val != "To Be Filled By O.E.M.")
				fp.components.push_back(detail::hashComponent("bios_serial", val));
		}

		// motherboard serial
		{
			std::string val = detail::getRegistryString(
				HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", "BaseBoardSerialNumber");
			if (!val.empty() && val != "Base Board Serial Number" && val != "To Be Filled By O.E.M.")
				fp.components.push_back(detail::hashComponent("baseboard_serial", val));
		}

		return fp;
	}

	// legacy wrapper for mod auth (old combined hash)
	inline std::string GenerateHWID(const std::string& playerName)
	{
		auto fp = GenerateHardwareFingerprint();
		std::string combined = playerName;
		for (const auto& c : fp.components)
			combined += "|" + c;
		return detail::sha256hex(combined);
	}
}
