#include "stdafx.h"
#include "cSpaceCombatMetrics.h"
#include "../cPersistenceState.h"
#include "../cDatabaseManager.h"
#include <Spore-Mod-Utils/Include/SporeModUtils.h>
#include <Spore/Simulator/cCity.h>
#include <cmath>

using namespace SporeModUtils;
using namespace Simulator;

enum AiGadgetBitFlags : uint32_t {
	kFlagLoyaltyBooster   = 1 << 0,
	kFlagHappinessBooster = 1 << 1,
	kFlagUberTurret       = 1 << 2,
	kFlagSpiceStorage     = 1 << 3,
	kFlagBioProtector     = 1 << 4,
	kFlagEmbassy          = 1 << 5
};

static uint32_t PackGadgetFlags(const SdoPlanetGadgetInfo& info)
{
	uint32_t flags = 0;
	if (info.hasLoyaltyBooster)   flags |= kFlagLoyaltyBooster;
	if (info.hasHappinessBooster) flags |= kFlagHappinessBooster;
	if (info.hasUberTurret)       flags |= kFlagUberTurret;
	if (info.hasSpiceStorage)     flags |= kFlagSpiceStorage;
	if (info.hasBioProtector)     flags |= kFlagBioProtector;
	if (info.hasEmbassy)          flags |= kFlagEmbassy;
	return flags;
}

static SdoPlanetGadgetInfo UnpackGadgetFlags(uint32_t flags, uint32_t embassyEmpireID)
{
	SdoPlanetGadgetInfo info;
	info.hasLoyaltyBooster   = (flags & kFlagLoyaltyBooster) != 0;
	info.hasHappinessBooster = (flags & kFlagHappinessBooster) != 0;
	info.hasUberTurret       = (flags & kFlagUberTurret) != 0;
	info.hasSpiceStorage     = (flags & kFlagSpiceStorage) != 0;
	info.hasBioProtector     = (flags & kFlagBioProtector) != 0;
	info.hasEmbassy          = (flags & kFlagEmbassy) != 0;
	info.aiEmbassyEmpireID   = embassyEmpireID;
	return info;
}

cSpaceCombatMetrics::cSpaceCombatMetrics(ResourceKey spaceCombatKey)
{
	persistenceImported = false;

	PropertyListPtr spaceCombatProp;
	PropManager.GetPropertyList(spaceCombatKey.instanceID, spaceCombatKey.groupID, spaceCombatProp);
	App::Property::GetArrayFloat(spaceCombatProp.get(), 0x033B0A90, healthBomber);
	App::Property::GetArrayFloat(spaceCombatProp.get(), 0x033B0A8D, healthFighter);
	App::Property::GetArrayFloat(spaceCombatProp.get(), 0x05D67A28, healthDefender);
	App::Property::GetArrayFloat(spaceCombatProp.get(), 0x057E2CFF, healthTurret);

	PropertyListPtr relationshipEffectsProp;
	PropManager.GetPropertyList(0x4e5855b9, 0x0568de14, relationshipEffectsProp);
	eastl::vector<int> defendersOutpost;
	eastl::vector<int> defendersColony;

	App::Property::GetArrayInt32(relationshipEffectsProp.get(), 0x067B69D0, defendersOutpost);
	App::Property::GetArrayInt32(relationshipEffectsProp.get(), 0x067B69EB, defendersColony);

	numDefendersInOutpost = !defendersOutpost.empty() ? defendersOutpost.front() : 1;
	numDefendersInColony = !defendersColony.empty() ? defendersColony.front() : 3;

	PropertyListPtr RaidWarProp;
	PropManager.GetPropertyList(0xbbae6b99, 0x02ae1a8e, RaidWarProp);

	int maxFighters = 2;
	int minFighters = 2;
	App::Property::GetInt32(RaidWarProp.get(), 0x0339BBC5, maxFighters);
	App::Property::GetInt32(RaidWarProp.get(), 0x0339BBC1, minFighters);

	numFightersPerBomber = max((maxFighters + minFighters) / 2, 1);

	PropertyListPtr terraformTuningProp;
	PropManager.GetPropertyList(0xa6d1bc70, 0x02ae0c7e, terraformTuningProp);

	int numBuildingsT1 = 8;
	int numBuildingsT2 = 12;
	int numBuildingsT3 = 12;
	App::Property::GetInt32(terraformTuningProp.get(), 0x04A5D473, numBuildingsT1);
	App::Property::GetInt32(terraformTuningProp.get(), 0x04A5D46D, numBuildingsT2);
	App::Property::GetInt32(terraformTuningProp.get(), 0x052F046E, numBuildingsT3);

	numTurretsInT1 = min(numBuildingsT1 / 2, 8);
	numTurretsInT2 = min(numBuildingsT2, 16);
	numTurretsInT3 = min((numBuildingsT3 / 2) * 3, 24);
}

cSpaceCombatMetrics::~cSpaceCombatMetrics()
{
}

int cSpaceCombatMetrics::AddRef()
{
	return DefaultRefCounted::AddRef();
}

int cSpaceCombatMetrics::Release()
{
	return DefaultRefCounted::Release();
}

void* cSpaceCombatMetrics::Cast(uint32_t type) const
{
	CLASS_CAST(Object);
	CLASS_CAST(cSpaceCombatMetrics);
	return nullptr;
}

void cSpaceCombatMetrics::EnsurePersistenceLoaded()
{
	if (persistenceImported || !IsSpaceGame()) return;

	cDatabaseManager* dbMgr = cDatabaseManager::Get();
	if (dbMgr != nullptr) {
		cPersistenceState* state = dbMgr->GetPersistenceState();
		if (state != nullptr) {
			ImportStateFromPersistence(state);
		}
	}
}

void cSpaceCombatMetrics::ExportStateToPersistence(cPersistenceState* state)
{
	if (state == nullptr) return;

	EnsurePersistenceLoaded();

	state->aiGadgetPlanetIDs.clear();
	state->aiGadgetFlags.clear();
	state->aiGadgetEmbassyEmpires.clear();

	for (const auto& kv : aiPurchasedGadgets) {
		if (kv.first == nullptr) continue;
		uint32_t pid = kv.first->GetID().internalValue;
		uint32_t flags = PackGadgetFlags(kv.second);
		if (pid == 0 || (flags == 0 && kv.second.aiEmbassyEmpireID == 0)) continue;

		state->aiGadgetPlanetIDs.push_back(pid);
		state->aiGadgetFlags.push_back(flags);
		state->aiGadgetEmbassyEmpires.push_back(kv.second.aiEmbassyEmpireID);
	}

	state->cachedDefensePlanetIDs.clear();
	state->cachedDefenseCities.clear();
	state->cachedDefenseTurrets.clear();

	for (const auto& kv : planetDefenseCache) {
		if (kv.first == nullptr) continue;
		uint32_t pid = kv.first->GetID().internalValue;
		if (pid == 0 || kv.second.first <= 0) continue;

		state->cachedDefensePlanetIDs.push_back(pid);
		state->cachedDefenseCities.push_back(kv.second.first);
		state->cachedDefenseTurrets.push_back(kv.second.second);
	}
}

void cSpaceCombatMetrics::ImportStateFromPersistence(const cPersistenceState* state)
{
	if (state == nullptr || !IsSpaceGame()) return;

	persistenceImported = true;

	size_t gadgetCount = min(state->aiGadgetPlanetIDs.size(), state->aiGadgetFlags.size());
	for (size_t i = 0; i < gadgetCount; ++i) {
		PlanetID pid(state->aiGadgetPlanetIDs[i]);
		cPlanetRecord* planet = StarManager.GetPlanetRecord(pid);
		if (planet == nullptr) continue;

		uint32_t embID = (i < state->aiGadgetEmbassyEmpires.size()) ? state->aiGadgetEmbassyEmpires[i] : 0;
		SdoPlanetGadgetInfo unpacked = UnpackGadgetFlags(state->aiGadgetFlags[i], embID);
		RegisterAiGadgetPurchase(planet, unpacked);
	}

	size_t cacheCount = min(state->cachedDefensePlanetIDs.size(), min(state->cachedDefenseCities.size(), state->cachedDefenseTurrets.size()));
	for (size_t i = 0; i < cacheCount; ++i) {
		PlanetID pid(state->cachedDefensePlanetIDs[i]);
		cPlanetRecord* planet = StarManager.GetPlanetRecord(pid);
		if (planet == nullptr) continue;

		if (planetDefenseCache.find(cPlanetRecordPtr(planet)) == planetDefenseCache.end()) {
			planetDefenseCache[cPlanetRecordPtr(planet)] = eastl::make_pair(state->cachedDefenseCities[i], state->cachedDefenseTurrets[i]);
		}
	}
}

static bool MatchSdoGadgetKey(uint32_t idVal, const char* n1, const char* n2, const char* n3 = nullptr, uint32_t raw1 = 0, uint32_t raw2 = 0)
{
	if (idVal == 0) return false;
	if (n1 && idVal == id(n1)) return true;
	if (n2 && idVal == id(n2)) return true;
	if (n3 && idVal == id(n3)) return true;
	if (raw1 != 0 && idVal == raw1) return true;
	if (raw2 != 0 && idVal == raw2) return true;
	return false;
}

static float GetExternalGadgetMultiplier()
{
	float mult = 1.0f;
	PropertyListPtr propList;
	if (PropManager.GetPropertyList(id(u"GadgetConfig"), 0x02ae0c7e, propList) && propList != nullptr) {
		App::Property::GetFloat(propList.get(), 0x07A1BC06, mult);
	}
	if (mult < 0.0f || mult > 5.0f) mult = 1.0f;
	return mult;
}

void cSpaceCombatMetrics::RegisterAiGadgetPurchase(Simulator::cPlanetRecord* planet, const SdoPlanetGadgetInfo& purchasedInfo)
{
	if (planet == nullptr) return;
	SdoPlanetGadgetInfo& current = aiPurchasedGadgets[cPlanetRecordPtr(planet)];
	if (purchasedInfo.hasLoyaltyBooster) current.hasLoyaltyBooster = true;
	if (purchasedInfo.hasHappinessBooster) current.hasHappinessBooster = true;
	if (purchasedInfo.hasUberTurret) current.hasUberTurret = true;
	if (purchasedInfo.hasSpiceStorage) current.hasSpiceStorage = true;
	if (purchasedInfo.hasBioProtector) current.hasBioProtector = true;
	if (purchasedInfo.hasEmbassy) current.hasEmbassy = true;
	if (purchasedInfo.aiEmbassyEmpireID != 0) current.aiEmbassyEmpireID = purchasedInfo.aiEmbassyEmpireID;
}

void cSpaceCombatMetrics::UpdatePlanetDefenseCache(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) return;

	EnsurePersistenceLoaded();

	if (GetCurrentContext() == SpaceContext::Planet && planet == GetActivePlanetRecord()) {
		int cityCount = 0;
		int turretCount = 0;
		const auto& cities = Simulator::GetData<cCity>();
		for (const auto& city : cities) {
			if (city) {
				cityCount++;
				turretCount += static_cast<int>(city->mTurrets.size());
			}
		}
		if (cityCount > 0) {
			planetDefenseCache[cPlanetRecordPtr(planet)] = eastl::make_pair(cityCount, turretCount);
		}

		SdoPlanetGadgetInfo info;
		if (GetExternalGadgetMultiplier() > 0.01f) {
			const auto& spatials = Simulator::GetDataByCast<cSpatialObject>();
			for (const auto& sp : spatials) {
				if (!sp) continue;
				cCombatant* asComb = object_cast<cCombatant>(sp.get());
				if (asComb && asComb->mHealthPoints <= 0.0f) continue;

				uint32_t modelId = sp->GetModelKey().instanceID;
				if (MatchSdoGadgetKey(modelId, "loyaltybooster", "b_loyaltybooster", "pl_loyalty_booster", 0x04668B8A, 0x03F3E4A1)) {
					info.hasLoyaltyBooster = true;
				} else if (MatchSdoGadgetKey(modelId, "happinessbooster", "b_happinessbooster", "pl_happiness_booster", 0x04668B89, 0x03F3E4A0)) {
					info.hasHappinessBooster = true;
				} else if (MatchSdoGadgetKey(modelId, "uberturret", "b_uberturret", "super_turret", 0x0246F65C, 0x04668B8B)) {
					info.hasUberTurret = true;
				} else if (MatchSdoGadgetKey(modelId, "spicestorage", "b_spicestorage", "pl_spice_storage", 0x04668B8C, 0x03F3E4A2)) {
					info.hasSpiceStorage = true;
				} else if (MatchSdoGadgetKey(modelId, "bioprotector", "biostabilizer", "b_bioprotector", 0x04668B87, 0x04668B88)) {
					info.hasBioProtector = true;
				} else if (MatchSdoGadgetKey(modelId, "embassy", "b_embassy", "pl_embassy", 0x04668B8E, 0x03F3E4A5)) {
					info.hasEmbassy = true;
				}
			}
		}
		planetGadgetCache[cPlanetRecordPtr(planet)] = info;
	}
}

SdoPlanetGadgetInfo cSpaceCombatMetrics::GetPlanetGadgetInfo(Simulator::cPlanetRecord* planet)
{
	SdoPlanetGadgetInfo mergedInfo;
	if (planet == nullptr || GetExternalGadgetMultiplier() <= 0.01f) {
		return mergedInfo;
	}

	EnsurePersistenceLoaded();

	if (GetCurrentContext() == SpaceContext::Planet && planet == GetActivePlanetRecord()) {
		UpdatePlanetDefenseCache(planet);
	}

	auto itPlaced = planetGadgetCache.find(cPlanetRecordPtr(planet));
	if (itPlaced != planetGadgetCache.end()) {
		mergedInfo = itPlaced->second;
	}

	auto itAi = aiPurchasedGadgets.find(cPlanetRecordPtr(planet));
	if (itAi != aiPurchasedGadgets.end()) {
		if (itAi->second.hasLoyaltyBooster) mergedInfo.hasLoyaltyBooster = true;
		if (itAi->second.hasHappinessBooster) mergedInfo.hasHappinessBooster = true;
		if (itAi->second.hasUberTurret) mergedInfo.hasUberTurret = true;
		if (itAi->second.hasSpiceStorage) mergedInfo.hasSpiceStorage = true;
		if (itAi->second.hasBioProtector) mergedInfo.hasBioProtector = true;
		if (itAi->second.hasEmbassy) mergedInfo.hasEmbassy = true;
		if (itAi->second.aiEmbassyEmpireID != 0) mergedInfo.aiEmbassyEmpireID = itAi->second.aiEmbassyEmpireID;
	}

	return mergedInfo;
}

int cSpaceCombatMetrics::GetCachedCityCount(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) return 0;
	EnsurePersistenceLoaded();
	auto it = planetDefenseCache.find(cPlanetRecordPtr(planet));
	if (it != planetDefenseCache.end()) {
		return it->second.first;
	}
	return 0;
}

int cSpaceCombatMetrics::GetCachedTurretCount(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) return 0;
	EnsurePersistenceLoaded();
	auto it = planetDefenseCache.find(cPlanetRecordPtr(planet));
	if (it != planetDefenseCache.end()) {
		return it->second.second;
	}
	return 0;
}

float cSpaceCombatMetrics::GetBomberHealthByLevel(int empireLevel)
{
	if (healthBomber.empty()) return 600.0f;
	if (empireLevel < 0) return healthBomber.front();
	if (empireLevel >= int(healthBomber.size())) return healthBomber.back();
	return healthBomber[empireLevel];
}

float cSpaceCombatMetrics::GetFighterHealthByLevel(int empireLevel)
{
	if (healthFighter.empty()) return 250.0f;
	if (empireLevel < 0) return healthFighter.front();
	if (empireLevel >= int(healthFighter.size())) return healthFighter.back();
	return healthFighter[empireLevel];
}

float cSpaceCombatMetrics::GetDefenderHealthByLevel(int empireLevel)
{
	if (healthDefender.empty()) return 500.0f;
	if (empireLevel < 0) return healthDefender.front();
	if (empireLevel >= int(healthDefender.size())) return healthDefender.back();
	return healthDefender[empireLevel];
}

float cSpaceCombatMetrics::GetTurretHealthByLevel(int empireLevel)
{
	if (healthTurret.empty()) return 800.0f;
	if (empireLevel < 0) return healthTurret.front();
	if (empireLevel >= int(healthTurret.size())) return healthTurret.back();
	return healthTurret[empireLevel];
}

int cSpaceCombatMetrics::GetDefendersInOutpost()
{
	return numDefendersInOutpost;
}

int cSpaceCombatMetrics::GetDefendersInColony()
{
	return numDefendersInColony;
}

int cSpaceCombatMetrics::GetFightersPerBomber()
{
	return numFightersPerBomber;
}

int cSpaceCombatMetrics::GetTurretsInT1()
{
	return numTurretsInT1;
}

int cSpaceCombatMetrics::GetTurretsInT2()
{
	return numTurretsInT2;
}

int cSpaceCombatMetrics::GetTurretsInT3()
{
	return numTurretsInT3;
}

float cSpaceCombatMetrics::GetBomberHealthByEmpire(Simulator::cEmpire* empire)
{
	if (empire == nullptr) return GetBomberHealthByLevel(0);

	float baseHealth = GetBomberHealthByLevel(empire->GetWeaponryLevel());
	float levelFactor = static_cast<float>(empire->GetWeaponryLevel() + 1) / 5.0f;
	Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);

	switch (baseArchetype) {
		case Archetypes::kArchetypeWarrior:
		case Archetypes::kArchetypeZealot:
		case Archetypes::kArchetypeGrob:
			return baseHealth * (1.0f + (0.25f * levelFactor));
		case Archetypes::kArchetypeScientist:
			return baseHealth * (1.0f + (0.10f * levelFactor));
		case Archetypes::kArchetypeBard:
		case Archetypes::kArchetypeDiplomat:
			return baseHealth * (1.0f - (0.20f * (1.0f - levelFactor * 0.5f)));
		default:
			return baseHealth;
	}
}

float cSpaceCombatMetrics::GetFighterHealthByEmpire(Simulator::cEmpire* empire)
{
	if (empire == nullptr) return GetFighterHealthByLevel(0);

	float baseHealth = GetFighterHealthByLevel(empire->GetWeaponryLevel());
	float levelFactor = static_cast<float>(empire->GetWeaponryLevel() + 1) / 5.0f;
	Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);

	switch (baseArchetype) {
		case Archetypes::kArchetypeWarrior:
		case Archetypes::kArchetypeZealot:
		case Archetypes::kArchetypeGrob:
			return baseHealth * (1.0f + (0.20f * levelFactor));
		case Archetypes::kArchetypeScientist:
			return baseHealth * (1.0f + (0.10f * levelFactor));
		case Archetypes::kArchetypeBard:
		case Archetypes::kArchetypeDiplomat:
			return baseHealth * (1.0f - (0.15f * (1.0f - levelFactor * 0.5f)));
		default:
			return baseHealth;
	}
}

float cSpaceCombatMetrics::GetDefenderHealthByEmpire(Simulator::cEmpire* empire)
{
	if (empire == nullptr) return GetDefenderHealthByLevel(0);

	if (empire == GetPlayerEmpire()) {
		PropertyListPtr propListFleet;
		if (PropManager.GetPropertyList(id(u"FleetConfig"), 0x02ae0c7e, propListFleet) && propListFleet != nullptr) {
			int weaponryRank = max(empire->GetWeaponryLevel(), 1);
			int totalStars = static_cast<int>(empire->mStars.size());
			float industrialBonus = min(static_cast<float>(totalStars) * 8.0f, 400.0f);
			float dynBase = 450.0f + (static_cast<float>(weaponryRank) * 120.0f) + industrialBonus;

			int arch = empire->mArchetype;
			float archMult = 0.85f;
			if (arch == 1 || arch == 2 || arch == 3 || arch == 10 || arch == 11 || arch == 12 || arch == 13) {
				archMult = 1.35f;
			} else if (arch == 4 || arch == 5 || arch == 6 || arch == 14 || arch == 15 || arch == 16) {
				archMult = 1.10f;
			}
			return dynBase * archMult;
		}
	}

	float baseHealth = GetDefenderHealthByLevel(empire->GetWeaponryLevel());
	float levelFactor = static_cast<float>(empire->GetWeaponryLevel() + 1) / 5.0f;
	Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);

	switch (baseArchetype) {
		case Archetypes::kArchetypeWarrior:
		case Archetypes::kArchetypeZealot:
		case Archetypes::kArchetypeGrob:
			return baseHealth * (1.0f + (0.25f * levelFactor));
		case Archetypes::kArchetypeScientist:
			return baseHealth * (1.0f + (0.10f * levelFactor));
		case Archetypes::kArchetypeBard:
		case Archetypes::kArchetypeDiplomat:
			return baseHealth * (1.0f - (0.20f * (1.0f - levelFactor * 0.5f)));
		default:
			return baseHealth;
	}
}

float cSpaceCombatMetrics::GetTurretHealthByEmpire(Simulator::cEmpire* empire)
{
	if (empire == nullptr) return GetTurretHealthByLevel(0);
	return GetTurretHealthByLevel(empire->GetWeaponryLevel());
}

int cSpaceCombatMetrics::GetDefendersInPlanet(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) {
		return GetDefendersInColony();
	}

	UpdatePlanetDefenseCache(planet);
	int cachedCities = GetCachedCityCount(planet);
	SdoPlanetGadgetInfo gadgets = GetPlanetGadgetInfo(planet);

	cEmpire* empire = nullptr;
	if (planet->GetStarRecord() != nullptr) {
		empire = StarManager.GetEmpire(planet->GetStarRecord()->mEmpireID);
	}

	if (empire != nullptr && empire == GetPlayerEmpire()) {
		PropertyListPtr propListFleet;
		if (PropManager.GetPropertyList(id(u"FleetConfig"), 0x02ae0c7e, propListFleet) && propListFleet != nullptr) {
			float fleetMult = 1.0f;
			App::Property::GetFloat(propListFleet.get(), 0x07A1BC01, fleetMult);
			if (fleetMult <= 0.05f || fleetMult > 5.0f) fleetMult = 1.0f;

			int colonyCount = cachedCities;
			if (colonyCount <= 0) {
				if (PlanetUtils::Homeworld(planet) || planet->mType == PlanetType::T3) colonyCount = 3;
				else if (planet->mType == PlanetType::T2) colonyCount = 2;
				else colonyCount = 1;
			}

			int stepBonus = 1;
			if (planet->mType == PlanetType::T1) stepBonus = 2;
			else if (planet->mType == PlanetType::T2) stepBonus = 3;
			else if (planet->mType == PlanetType::T3) stepBonus = 4;

			int basePlayerDef = 10;
			if (PlanetUtils::Homeworld(planet)) basePlayerDef = 20;
			else if (planet->mType == PlanetType::T0) basePlayerDef = 5;

			int rawTotal = basePlayerDef + (colonyCount - 1) * stepBonus;
			if (gadgets.hasSpiceStorage) {
				rawTotal = static_cast<int>(round(static_cast<float>(rawTotal) * (1.0f + 0.25f * GetExternalGadgetMultiplier())));
			}
			rawTotal = static_cast<int>(round(static_cast<float>(rawTotal) * fleetMult));

			int totalSystems = static_cast<int>(empire->mStars.size());
			int weaponryRank = max(empire->GetWeaponryLevel(), 1);
			int dynamicCap = static_cast<int>(round(static_cast<float>(20 + static_cast<int>(totalSystems * 0.8f) + weaponryRank * 3) * fleetMult));
			if (dynamicCap < 12) dynamicCap = 12;
			if (dynamicCap > 120) dynamicCap = 120;

			if (rawTotal > dynamicCap) {
				float excess = static_cast<float>(rawTotal - dynamicCap);
				rawTotal = dynamicCap + static_cast<int>(round(std::sqrt(excess) * 4.0f));
			}
			return max(rawTotal, 1);
		}
	}

	int baseDefenders = (planet->mType == PlanetType::T0) ? GetDefendersInOutpost() : GetDefendersInColony();
	int baseColonyDefenders = GetDefendersInColony();

	if (planet->mType != PlanetType::T0 && empire != nullptr) {
		Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
		float archetypeMultiplier = 1.0f;

		switch (baseArchetype) {
			case Archetypes::kArchetypeWarrior:
			case Archetypes::kArchetypeZealot:
			case Archetypes::kArchetypeGrob:
				archetypeMultiplier = 2.5f;
				break;
			case Archetypes::kArchetypeScientist:
			case Archetypes::kArchetypeTrader:
				archetypeMultiplier = 1.6f;
				break;
			case Archetypes::kArchetypeEcologist:
				archetypeMultiplier = 1.2f;
				break;
			default:
				archetypeMultiplier = 0.8f;
				break;
		}

		float tScoreFactor = (planet->mType == PlanetType::T3) ? 1.5f : ((planet->mType == PlanetType::T2) ? 1.0f : 0.6f);
		if (PlanetUtils::Homeworld(planet)) {
			tScoreFactor *= 1.35f;
		}
		baseDefenders = max(static_cast<int>(round(static_cast<float>(baseColonyDefenders) * archetypeMultiplier * tScoreFactor)), 2);
	}

	int standardCities = (planet->mType == PlanetType::T3) ? 3 : ((planet->mType == PlanetType::T2) ? 2 : 1);
	if (cachedCities > standardCities) {
		baseDefenders += (cachedCities - standardCities) * max(baseColonyDefenders / 2, 1);
	}

	if (gadgets.hasSpiceStorage) {
		baseDefenders = static_cast<int>(round(static_cast<float>(baseDefenders) * (1.0f + 0.25f * GetExternalGadgetMultiplier())));
	}

	return baseDefenders;
}

int cSpaceCombatMetrics::GetTurretsInPlanet(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) return 0;

	UpdatePlanetDefenseCache(planet);
	int cachedCities = GetCachedCityCount(planet);
	int cachedTurrets = GetCachedTurretCount(planet);

	int baseTurrets = 0;
	int standardCities = 1;
	switch (planet->mType) {
		case(PlanetType::T1):
			baseTurrets = GetTurretsInT1();
			standardCities = 1;
			break;
		case(PlanetType::T2):
			baseTurrets = GetTurretsInT2();
			standardCities = 2;
			break;
		case(PlanetType::T3):
			baseTurrets = GetTurretsInT3();
			standardCities = 3;
			break;
		default:
			baseTurrets = 0;
			standardCities = 1;
			break;
	}

	int finalTurrets = baseTurrets;
	if (cachedCities > 0) {
		if (cachedCities > standardCities || cachedTurrets > baseTurrets || planet->mType == PlanetType::T0) {
			finalTurrets = cachedTurrets;
		}
	}

	if (GetPlanetGadgetInfo(planet).hasUberTurret) {
		finalTurrets += static_cast<int>(round(14.0f * GetExternalGadgetMultiplier()));
	}

	return finalTurrets;
}
