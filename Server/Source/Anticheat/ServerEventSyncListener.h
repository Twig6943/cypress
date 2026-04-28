#pragma once
#include <MemUtil.h>
#include <fb/Engine/ServerEventSyncEntity.h>
#include <fb/Engine/EventSyncReachedClientMessage.h>

#include <unordered_map>
#include <unordered_set>
#include <string>
DECLARE_HOOK(
	fb_ServerEventSyncEntity_Listener_onMessage,
	__fastcall,
	void,

	fb::ServerEventSyncEntity::Listener* a1,
	fb::EventSyncReachedClientMessage* message
);

//static std::unordered_set<uint32_t> g_eventSyncBlacklist {
//	fnvHashConstexpr("Gameplay/GameModes/Helper_Prefabs/MultiplayerEORLevelLauncher"), //map changer
//	fnvHashConstexpr("Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_Rush_InGameLogic"), //pause timer and round ender
//	fnvHashConstexpr("Gameplay/GameModes/GameModeLogic/CinematicLogic/GameModeLogic_CinematicLogic"), //server side input restrictions
//	fnvHashConstexpr("Gameplay/ComplexObjects/ZombieTeleporterExit"), //build all teleporters
//	fnvHashConstexpr("Gameplay/ComplexObjects/PlantTeleporterExit"), //build all teleporters
//	fnvHashConstexpr("Gameplay/Logic/OnInteractionHelperPrefab"), //interacts with all interactables in the level
//};

static std::unordered_map<std::string, std::unordered_set<uint32_t>> g_eventSyncBlacklist = {
	//map changer
	{"Gameplay/GameModes/Helper_Prefabs/MultiplayerEORLevelLauncher", {62989975}},

	//round ender
	{"Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_CoOps_InGameLogic", { 61172246, 43407616}}, //ops
	{"Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_GnomeBomb_InGameLogic", { 52273023, 42440086}}, //gnomebomb
	{"Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_InGameLogic_BossHunt", { 41709720, 36639676}}, //bosshunt
	{"Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_InGameLogic_EndlessOps", { 41295307, 61279254}}, //infinitytime
	{"Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_Rush_InGameLogic", { 43095794, 55212969}}, //turf
	{"Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_Suburbination_InGameLogic", { 48528559, 55353884}}, //suburbination
	{"Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_TacoBandits_InGameLogic", { 64459295, 58234406}}, //tacobandits
	{"Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_TeamVanquish_InGameLogic", { 38010343, 64422854}}, //teamvanquish
	{"Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_VanquishConfirmed_InGameLogic", { 46526970, 33637446}}, //vanquishcomfired

	//pausetimer
	{"Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_Rush_InGameLogic", { 43095794}}, //turf pausetimer
	//{"Gameplay/GameModes/GameModeLogic/InGameLogic/GameModeLogic_TacoBandits_InGameLogic", { 0}}, //different case, doesn't use eventsync

	{"Gameplay/GameModes/GameModeLogic/CinematicLogic/GameModeLogic_CinematicLogic", { 42199542}}, //server side input restrictions
	{"Gameplay/ComplexObjects/ZombieTeleporterExit", { 63478245}},
	{"Gameplay/ComplexObjects/PlantTeleporterExit", { 65825568}},
	// {"Gameplay/Logic/OnInteractionHelperPrefab", {59695446}}, //commented out for the time being due to actual interactions, such as diffusing gnome, ruining the zomburbia party, and turf teleporters using this flag

	//Crazy Options that apply to the server via QA Debug menu, such as Berserker, Crazy Regen, and Healing Auras
	{"Gameplay/GameModes/GameModePrefabs/CrazyOptions/CrazyOptions_Prefab", { 37379582, 63903114, 60042275}},
};

inline bool isEventSyncBlacklisted(const char* prefab, uint32_t flags)
{
	if (!prefab)
		return false;

	auto blacklistIt = g_eventSyncBlacklist.find(prefab);
	if (blacklistIt == g_eventSyncBlacklist.end())
		return false;

	const auto& flagsSet = blacklistIt->second;
	return flagsSet.find(flags) != flagsSet.end();
}
