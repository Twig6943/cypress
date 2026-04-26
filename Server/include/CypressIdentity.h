#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <json.hpp>

// monocypher for ed25519 curve math
extern "C" {
	void crypto_eddsa_reduce(uint8_t reduced[32], const uint8_t expanded[64]);
	int  crypto_eddsa_check_equation(const uint8_t signature[64], const uint8_t public_key[32], const uint8_t h_ram[32]);
}

namespace Cypress
{
	namespace Identity
	{
		namespace detail
		{
			// sha-512 via bcrypt (windows)
			inline bool sha512(const uint8_t* data, size_t len, uint8_t out[64])
			{
				BCRYPT_ALG_HANDLE hAlg = nullptr;
				BCRYPT_HASH_HANDLE hHash = nullptr;
				bool ok = false;

				if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA512_ALGORITHM, nullptr, 0) == 0)
				{
					if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) == 0)
					{
						BCryptHashData(hHash, (PUCHAR)data, (ULONG)len, 0);
						BCryptFinishHash(hHash, out, 64, 0);
						ok = true;
						BCryptDestroyHash(hHash);
					}
					BCryptCloseAlgorithmProvider(hAlg, 0);
				}
				return ok;
			}

			inline std::string base64url_decode(const std::string& input)
			{
				std::string b64 = input;
				for (auto& c : b64)
				{
					if (c == '-') c = '+';
					else if (c == '_') c = '/';
				}
				while (b64.size() % 4 != 0) b64 += '=';

				// decode
				static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
				auto val = [](char c) -> int {
					if (c >= 'A' && c <= 'Z') return c - 'A';
					if (c >= 'a' && c <= 'z') return c - 'a' + 26;
					if (c >= '0' && c <= '9') return c - '0' + 52;
					if (c == '+') return 62;
					if (c == '/') return 63;
					return -1;
				};

				std::string out;
				out.reserve(b64.size() * 3 / 4);
				for (size_t i = 0; i + 3 < b64.size(); i += 4)
				{
					int a = val(b64[i]), b = val(b64[i + 1]), c = val(b64[i + 2]), d = val(b64[i + 3]);
					if (a < 0 || b < 0) break;
					out += (char)((a << 2) | (b >> 4));
					if (c >= 0) out += (char)(((b & 0xF) << 4) | (c >> 2));
					if (d >= 0) out += (char)(((c & 3) << 6) | d);
				}
				return out;
			}

			inline std::vector<uint8_t> hex_decode(const std::string& hex)
			{
				std::vector<uint8_t> out;
				out.reserve(hex.size() / 2);
				for (size_t i = 0; i + 1 < hex.size(); i += 2)
				{
					auto nibble = [](char c) -> uint8_t {
						if (c >= '0' && c <= '9') return c - '0';
						if (c >= 'a' && c <= 'f') return c - 'a' + 10;
						if (c >= 'A' && c <= 'F') return c - 'A' + 10;
						return 0;
					};
					out.push_back((nibble(hex[i]) << 4) | nibble(hex[i + 1]));
				}
				return out;
			}

			inline std::string hex_encode(const uint8_t* data, size_t len)
			{
				std::string out;
				out.reserve(len * 2);
				for (size_t i = 0; i < len; ++i)
				{
					char buf[3];
					snprintf(buf, sizeof(buf), "%02x", data[i]);
					out += buf;
				}
				return out;
			}
		}

		// ed25519 verify using monocypher building blocks + bcrypt sha-512
		// ed25519 verify per rfc 8032
		inline bool ed25519_verify(const uint8_t signature[64], const uint8_t public_key[32],
			const uint8_t* message, size_t message_len)
		{
			// build SHA-512(R || A || M)
			BCRYPT_ALG_HANDLE hAlg = nullptr;
			BCRYPT_HASH_HANDLE hHash = nullptr;
			uint8_t hash[64] = {};

			if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA512_ALGORITHM, nullptr, 0) != 0)
				return false;
			if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) != 0)
			{
				BCryptCloseAlgorithmProvider(hAlg, 0);
				return false;
			}

			BCryptHashData(hHash, (PUCHAR)signature, 32, 0);      // R (first half of signature)
			BCryptHashData(hHash, (PUCHAR)public_key, 32, 0);     // A (public key)
			BCryptHashData(hHash, (PUCHAR)message, (ULONG)message_len, 0); // M (message)
			BCryptFinishHash(hHash, hash, 64, 0);
			BCryptDestroyHash(hHash);
			BCryptCloseAlgorithmProvider(hAlg, 0);

			// reduce hash mod L
			uint8_t h_ram[32];
			crypto_eddsa_reduce(h_ram, hash);

			return crypto_eddsa_check_equation(signature, public_key, h_ram) == 0;
		}

		struct JWTClaims
		{
			std::string sub;            // account_id
			std::string username;
			std::string nickname;
			std::string pk_fingerprint; // sha256 of public key (first 16 bytes, hex)
			std::string ea_pid;         // ea persona id
			std::string ea_name;        // ea display name
			int64_t iat = 0;
			int64_t exp = 0;
		};

		// verify jwt signed with ed25519 (EdDSA)
		// returns true and fills claims on success
		inline bool verify_jwt(const std::string& token, const uint8_t master_pubkey[32], JWTClaims& claims)
		{
			// split header.payload.signature
			size_t dot1 = token.find('.');
			if (dot1 == std::string::npos) { CYPRESS_LOGMESSAGE(LogLevel::Warning, "JWT: no first dot"); return false; }
			size_t dot2 = token.find('.', dot1 + 1);
			if (dot2 == std::string::npos) { CYPRESS_LOGMESSAGE(LogLevel::Warning, "JWT: no second dot"); return false; }

			std::string sigInput = token.substr(0, dot2);
			std::string sigB64 = token.substr(dot2 + 1);

			// decode signature
			std::string sigRaw = detail::base64url_decode(sigB64);
			if (sigRaw.size() != 64) { CYPRESS_LOGMESSAGE(LogLevel::Warning, "JWT: sig decode size={} (expected 64)", sigRaw.size()); return false; }

			// log pubkey fingerprint for comparison
			{
				BCRYPT_ALG_HANDLE hAlg = nullptr;
				BCRYPT_HASH_HANDLE hHash = nullptr;
				uint8_t hash[32] = {};
				if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
				{
					if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) == 0)
					{
						BCryptHashData(hHash, (PUCHAR)master_pubkey, 32, 0);
						BCryptFinishHash(hHash, hash, 32, 0);
						BCryptDestroyHash(hHash);
						std::string fp;
						for (int i = 0; i < 16; i++) { char hex[3]; snprintf(hex, 3, "%02x", hash[i]); fp += hex; }
						CYPRESS_LOGMESSAGE(LogLevel::Info, "JWT: verifying with pubkey fp={}", fp);
					}
					BCryptCloseAlgorithmProvider(hAlg, 0);
				}
			}

			// verify signature
			if (!ed25519_verify((const uint8_t*)sigRaw.data(), master_pubkey,
				(const uint8_t*)sigInput.data(), sigInput.size()))
			{
				CYPRESS_LOGMESSAGE(LogLevel::Warning, "JWT: ed25519 signature verification failed");
				return false;
			}

			// decode payload
			std::string payloadB64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
			std::string payloadJson = detail::base64url_decode(payloadB64);

			try
			{
				auto j = nlohmann::json::parse(payloadJson);
				claims.sub = j.value("sub", "").substr(0, 128);
				claims.username = j.value("username", "").substr(0, 64);
				claims.nickname = j.value("nickname", "").substr(0, 64);
				claims.pk_fingerprint = j.value("pk_fp", "").substr(0, 64);
				claims.ea_pid = j.value("ea_pid", "").substr(0, 64);
				claims.ea_name = j.value("ea_name", "").substr(0, 64);
				claims.iat = j.value("iat", (int64_t)0);
				claims.exp = j.value("exp", (int64_t)0);

				// check expiry
				auto now = std::chrono::system_clock::now();
				int64_t nowUnix = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
				if (nowUnix > claims.exp)
				{
					CYPRESS_LOGMESSAGE(LogLevel::Warning, "JWT: expired (now={}, exp={}, delta={}s)", nowUnix, claims.exp, nowUnix - claims.exp);
					return false;
				}

				CYPRESS_LOGMESSAGE(LogLevel::Info, "JWT: verified ok, sub={} user={}", claims.sub, claims.username);
				return true;
			}
			catch (...)
			{
				CYPRESS_LOGMESSAGE(LogLevel::Warning, "JWT: payload parse exception");
				return false;
			}
		}

		// verify ed25519 challenge-response: client signed the nonce with their private key
		inline bool verify_challenge(const uint8_t* public_key, const std::string& nonce, const std::string& sig_hex)
		{
			auto sig = detail::hex_decode(sig_hex);
			if (sig.size() != 64) return false;

			return ed25519_verify(sig.data(), public_key,
				(const uint8_t*)nonce.data(), nonce.size());
		}

		// ban list fetched from master server
		struct BanList
		{
			std::vector<std::string> banned_accounts;
			std::vector<std::string> banned_hwids;
			std::vector<std::string> banned_ea_pids;

			bool is_account_banned(const std::string& account_id) const
			{
				for (const auto& id : banned_accounts)
					if (id == account_id) return true;
				return false;
			}

			bool is_hwid_banned(const std::string& hwid_hash) const
			{
				for (const auto& h : banned_hwids)
					if (h == hwid_hash) return true;
				return false;
			}

			bool is_ea_pid_banned(const std::string& ea_pid) const
			{
				for (const auto& p : banned_ea_pids)
					if (p == ea_pid) return true;
				return false;
			}
		};

		inline BanList parse_banlist(const std::string& json_str)
		{
			BanList bl;
			try
			{
				auto j = nlohmann::json::parse(json_str);
				if (j.contains("banned_accounts") && j["banned_accounts"].is_array())
					for (const auto& v : j["banned_accounts"])
						bl.banned_accounts.push_back(v.get<std::string>());
				if (j.contains("banned_hwids") && j["banned_hwids"].is_array())
					for (const auto& v : j["banned_hwids"])
						bl.banned_hwids.push_back(v.get<std::string>());
				if (j.contains("banned_ea_pids") && j["banned_ea_pids"].is_array())
					for (const auto& v : j["banned_ea_pids"])
						bl.banned_ea_pids.push_back(v.get<std::string>());
			}
			catch (...) {}
			return bl;
		}
	}
}
