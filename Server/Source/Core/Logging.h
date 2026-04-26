#pragma once
#include <cstdio>
#include <iostream>
#include <format>

#include <Core/VersionInfo.h>
#include <Core/Config.h>

enum class LogLevel
{
	Info,
	Warning,
	Error,
	Debug
};

constexpr const char* Cypress_LogLevelToStr(LogLevel inLevel)
{
	switch (inLevel)
	{
	case LogLevel::Info: return "Info";
	case LogLevel::Warning: return "Warning";
	case LogLevel::Error: return "Error";
	case LogLevel::Debug: return "Debug";
	}
}

constexpr const char* Cypress_GetColorForLogLevel(LogLevel inLevel)
{
	switch (inLevel)
	{
	case LogLevel::Info: return "\x1B[32m";
	case LogLevel::Warning: return "\x1B[33m";
	case LogLevel::Error: return "\x1B[31m";
	case LogLevel::Debug: return "\x1B[36m";
	}
}

void Cypress_LogToServer(const char* msg, const char* fileName, int lineNumber, LogLevel logLevel);
void Cypress_EmitJsonLog(const char* msg, LogLevel logLevel);
void Cypress_EmitJsonServerLog(const char* msg, LogLevel logLevel);
void Cypress_EmitJsonStatus(const char* map, const char* mode, const char* col1, const char* col2);
void Cypress_EmitJsonPlayerEvent(const char* event, int playerId, const char* name, const char* extra = nullptr);
void Cypress_WriteRawStdout(const std::string& data);
bool Cypress_IsEmbeddedMode();

#if(HAS_DEDICATED_SERVER)
#define CYPRESS_LOGTOSERVER(logLevel, fmt, ...) \
do { \
	std::string formattedMsg = std::format(fmt, ##__VA_ARGS__); \
	Cypress_LogToServer(formattedMsg.c_str(), __FILE__, __LINE__, logLevel); \
} while(0)
#else
#define CYPRESS_LOGTOSERVER(logLevel, fmt, ...)
#endif

#ifdef _DEBUG
#define CYPRESS_LOG_INTERNAL(msg, logLevel) \
do { \
	if (Cypress_IsEmbeddedMode()) { \
		Cypress_EmitJsonLog(msg, logLevel); \
	} else { \
		std::cout << Cypress_GetColorForLogLevel(logLevel) \
		<< "[Cypress-" \
		<< CYPRESS_BUILD_CONFIG \
		<< "] [" \
		<< Cypress_LogLevelToStr(logLevel) \
		<< "] [" \
		<< __FILE__ \
		<< ":" \
		<< __LINE__ \
		<< "] " \
		<< msg \
		<< "\x1B[0m" \
		<< std::endl; \
	} \
} while(0)
#else
#define CYPRESS_LOG_INTERNAL(msg, logLevel) \
do { \
	if (Cypress_IsEmbeddedMode()) { \
		Cypress_EmitJsonLog(msg, logLevel); \
	} else { \
		std::cout << Cypress_GetColorForLogLevel(logLevel) \
		<< "[Cypress-" \
		<< CYPRESS_BUILD_CONFIG \
		<< "] [" \
		<< Cypress_LogLevelToStr(logLevel) \
		<< "] " \
		<< msg \
		<< "\x1B[0m" \
		<< std::endl; \
	} \
} while(0)
#endif

#define CYPRESS_LOGMESSAGE(logLevel, fmt, ...) \
do { \
	std::string formattedMsg = std::format(fmt, ##__VA_ARGS__); \
	CYPRESS_LOG_INTERNAL(formattedMsg.c_str(), logLevel); \
} while(0)

// Debug log calls will be completely stripped out of release builds.
#ifdef _DEBUG
#define CYPRESS_DEBUG_LOGMESSAGE CYPRESS_LOGMESSAGE
#else
#define CYPRESS_DEBUG_LOGMESSAGE
#endif