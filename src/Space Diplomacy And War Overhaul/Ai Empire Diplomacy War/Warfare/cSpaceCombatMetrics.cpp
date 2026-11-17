#include "stdafx.h"
#include "cSpaceCombatMetrics.h"
#include "../cPersistenceState.h"
#include "../cDatabaseManager.h"
#include "../cCompositionRoot.h"
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

static float ReadDynamicSpiceCostFromEngine(uint32_t spiceID)
{
	if (spiceID == 0) return 0.0f;

	PropertyListPtr spiceProp;
	if (PropManager.GetPropertyList(spiceID, 0x034D97FA, spiceProp) && spiceProp != nullptr) {
		float baseCost = 0.0f;
		if (App::Property::GetFloat(spiceProp.get(), 0x00B2CCCB, baseCost) && baseCost > 0.0f) {
			return baseCost;
		}
	}

	float fallbackRef = 1.0f;
	PropertyListPtr redProp;
	if (PropManager.GetPropertyList(0x0289B83D, 0x034D97FA, redProp) && redProp != nullptr) {
		App::Property::GetFloat(redProp.get(), 0x00B2CCCB, fallbackRef);
	}
	return max(fallbackRef, 1.0f);
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

SdoTradeConfigValues cSpaceCombatMetrics::GetTradeConfig() const
{
	SdoTradeConfigValues cfg;
	PropertyListPtr propList;
	if (PropManager.GetPropertyList(id(u"TradeConfig"), 0x02ae0c7e, propList) && propList != nullptr) {
		App::Property::GetFloat(propList.get(), 0x07A1BC07, cfg.incomeMultiplier);
		App::Property::GetFloat(propList.get(), 0x07A1BC08, cfg.spiceArbitrageWeight);
		App::Property::GetFloat(propList.get(), 0x07A1BC09, cfg.warDeterrenceStrength);

		float maxRoutesF = 5.0f;
		if (App::Property::GetFloat(propList.get(), 0x07A1BC0A, maxRoutesF) && maxRoutesF >= 1.0f) {
			cfg.maxRoutesPerEmpire = static_cast<int>(round(maxRoutesF));
		}
		float affRoutesF = 2.0f;
		if (App::Property::GetFloat(propList.get(), 0x07A1BC0B, affRoutesF) && affRoutesF >= 1.0f) {
			cfg.routesPerAffinityPoint = static_cast<int>(round(affRoutesF));
		}

		App::Property::GetFloat(propList.get(), 0x07A1BC0C, cfg.convoyDensity);
		App::Property::GetFloat(propList.get(), 0x07A1BC0D, cfg.convoyBaseScale);
		App::Property::GetFloat(propList.get(), 0x07A1BC0E, cfg.convoyScalePerSpice);
	}

	if (cfg.incomeMultiplier < 0.0f || cfg.incomeMultiplier > 5.0f) cfg.incomeMultiplier = 1.0f;
	if (cfg.spiceArbitrageWeight < 0.0f || cfg.spiceArbitrageWeight > 5.0f) cfg.spiceArbitrageWeight = 1.0f;
	if (cfg.warDeterrenceStrength < 0.0f || cfg.warDeterrenceStrength > 5.0f) cfg.warDeterrenceStrength = 1.0f;
	if (cfg.convoyDensity < 0.0f || cfg.convoyDensity > 5.0f) cfg.convoyDensity = 1.0f;
	if (cfg.convoyBaseScale < 0.5f || cfg.convoyBaseScale > 4.0f) cfg.convoyBaseScale = 1.25f;
	if (cfg.convoyScalePerSpice < 0.0f || cfg.convoyScalePerSpice > 1.0f) cfg.convoyScalePerSpice = 0.08f;
	return cfg;
}

float cSpaceCombatMetrics::GetNormalizedStarSpiceRatio(Simulator::cStarRecord* star) const
{
	if (!star) return 1.0f;

	float referenceRedPrice = max(ReadDynamicSpiceCostFromEngine(0x0289B83D), 1.0f);
	float bestRatio = 1.0f;

	for (const auto& planet : star->GetPlanetRecords()) {
		if (!planet || planet->GetTechLevel() != TechLevel::Empire) continue;
		float rawCost = ReadDynamicSpiceCostFromEngine(planet->mSpiceGen.instanceID);
		if (rawCost > 0.0f) {
			float ratio = rawCost / referenceRedPrice;
			if (ratio > bestRatio) bestRatio = ratio;
		}
	}
	return max(bestRatio, 0.5f);
}

bool cSpaceCombatMetrics::IsStarBlockaded(Simulator::cStarRecord* star) const
{
	if (!star) return true;
	for (const auto& planet : star->GetPlanetRecords()) {
		if (planet && MissionManager.ThereIsEventInPlanet(planet.get())) {
			return true;
		}
	}
	return false;
}

bool cSpaceCombatMetrics::HasTradeRouteBetweenStars(uint32_t starA, uint32_t starB) const
{
	if (starA == 0 || starB == 0 || starA == starB) return false;
	uint32_t lo = min(starA, starB);
	uint32_t hi = max(starA, starB);

	for (const auto& route : galacticTradeRoutes) {
		if (route.starIdA == lo && route.starIdB == hi) {
			return true;
		}
	}
	return false;
}

void cSpaceCombatMetrics::PruneInvalidTradeRoutes()
{
	for (size_t i = 0; i < galacticTradeRoutes.size(); ) {
		cStarRecord* sA = StarManager.GetStarRecord(StarID(galacticTradeRoutes[i].starIdA));
		cStarRecord* sB = StarManager.GetStarRecord(StarID(galacticTradeRoutes[i].starIdB));

		bool valid = (sA != nullptr && sB != nullptr && sA->mEmpireID != 0 && sB->mEmpireID != 0);
		if (valid && sA->mEmpireID != sB->mEmpireID) {
			cEmpire* eA = StarManager.GetEmpire(sA->mEmpireID);
			cEmpire* eB = StarManager.GetEmpire(sB->mEmpireID);
			if (!eA || !eB || RelationshipManager.IsAtWar(eA, eB)) {
				valid = false;
			}
		}

		if (!valid) {
			galacticTradeRoutes.erase(galacticTradeRoutes.begin() + i);
		} else {
			++i;
		}
	}
}

int cSpaceCombatMetrics::GetEmpireTotalTradeRoutes(Simulator::cEmpire* empire)
{
	if (!empire) return 0;
	EnsurePersistenceLoaded();

	uint32_t empID = empire->GetEmpireID();
	int count = 0;
	for (const auto& route : galacticTradeRoutes) {
		cStarRecord* sA = StarManager.GetStarRecord(StarID(route.starIdA));
		cStarRecord* sB = StarManager.GetStarRecord(StarID(route.starIdB));
		if (!sA || !sB) continue;
		if (sA->mEmpireID == empID || sB->mEmpireID == empID) {
			count++;
		}
	}
	return count;
}

int cSpaceCombatMetrics::GetActiveTradeRoutesBetweenEmpires(
	Simulator::cEmpire* emp1,
	Simulator::cEmpire* emp2,
	bool includeBlockaded)
{
	if (!emp1 || !emp2 || emp1 == emp2) return 0;
	if (GetTradeConfig().incomeMultiplier <= 0.01f) return 0;

	EnsurePersistenceLoaded();

	uint32_t id1 = emp1->GetEmpireID();
	uint32_t id2 = emp2->GetEmpireID();
	int count = 0;

	if (!RelationshipManager.IsAtWar(emp1, emp2)) {
		for (const auto& st1 : emp1->mStars) {
			if (st1 != nullptr && StarManager.mTradeRouteManager.HasTradeRoute(st1->GetID(), id2)) {
				if (includeBlockaded || !IsStarBlockaded(st1.get())) {
					count++;
				}
			}
		}
		for (const auto& st2 : emp2->mStars) {
			if (st2 != nullptr && StarManager.mTradeRouteManager.HasTradeRoute(st2->GetID(), id1)) {
				if (includeBlockaded || !IsStarBlockaded(st2.get())) {
					count++;
				}
			}
		}
	}

	for (const auto& route : galacticTradeRoutes) {
		cStarRecord* sA = StarManager.GetStarRecord(StarID(route.starIdA));
		cStarRecord* sB = StarManager.GetStarRecord(StarID(route.starIdB));
		if (!sA || !sB) continue;

		bool match = (sA->mEmpireID == id1 && sB->mEmpireID == id2) ||
		             (sA->mEmpireID == id2 && sB->mEmpireID == id1);
		if (match) {
			if (includeBlockaded || (!IsStarBlockaded(sA) && !IsStarBlockaded(sB))) {
				count++;
			}
		}
	}
	return count;
}

float cSpaceCombatMetrics::GetEmpirePollutionIndex(Simulator::cEmpire* targetEmpire)
{
	if (!targetEmpire) return 1.0f;
	float crusadeXml = 0.35f;
	PropertyListPtr gProp;
	if (PropManager.GetPropertyList(id(u"GadgetConfig"), 0x02ae0c7e, gProp) && gProp != nullptr) {
		App::Property::GetFloat(gProp.get(), 0x07A1BC17, crusadeXml);
	}
	if (crusadeXml <= 0.01f) return 1.0f;

	if (targetEmpire != GetPlayerEmpire()) {
		Simulator::Archetypes arch = ArchetypeUtils::GetBaseArchetype(targetEmpire->mArchetype);
		if (arch == Archetypes::kArchetypeGrob) return 1.0f + crusadeXml * 1.70f;
		if (arch == Archetypes::kArchetypeWarrior || arch == Archetypes::kArchetypeTrader || arch == Archetypes::kArchetypeScientist) {
			return 1.0f + crusadeXml;
		}
		return 1.0f;
	}

	// Для игрока оцениваем реальную урбанизацию и долю мертвых шахт T0/T1 по кэшу планет!
	int pollutedWorlds = 0;
	int greenSanctuaries = 0;
	for (const auto& kv : planetDefenseCache) {
		if (!kv.first || !kv.first->GetStarRecord()) continue;
		if (kv.first->GetStarRecord()->mEmpireID != targetEmpire->GetEmpireID()) continue;
		int cities = kv.second.first;
		if ((kv.first->mType == PlanetType::T0 || kv.first->mType == PlanetType::T1) && cities >= 2) {
			pollutedWorlds += cities;
		} else if (kv.first->mType == PlanetType::T3) {
			greenSanctuaries += 2;
		}
	}

	if (pollutedWorlds > greenSanctuaries + 2) {
		return min(1.0f + crusadeXml * 1.15f, 1.75f);
	}
	return 1.0f;
}

int cSpaceCombatMetrics::GetEcologistEcoAffinityModifier(Simulator::cEmpire* empire1, Simulator::cEmpire* empire2)
{
	if (!empire1 || !empire2 || empire1 == empire2) return 0;
	cEmpire* playerEmp = GetPlayerEmpire();
	if (empire1 != playerEmp && empire2 != playerEmp) return 0;

	cEmpire* aiEmp = (empire1 == playerEmp) ? empire2 : empire1;
	if (ArchetypeUtils::GetBaseArchetype(aiEmp->mArchetype) != Archetypes::kArchetypeEcologist) return 0;

	float crusadeXml = 0.35f;
	PropertyListPtr gProp;
	if (PropManager.GetPropertyList(id(u"GadgetConfig"), 0x02ae0c7e, gProp) && gProp != nullptr) {
		App::Property::GetFloat(gProp.get(), 0x07A1BC17, crusadeXml);
	}
	if (crusadeXml <= 0.01f) return 0;

	int pollutedScore = 0;
	int greenScore = 0;
	for (const auto& kv : planetDefenseCache) {
		if (!kv.first || !kv.first->GetStarRecord()) continue;
		if (kv.first->GetStarRecord()->mEmpireID != playerEmp->GetEmpireID()) continue;
		if ((kv.first->mType == PlanetType::T0 || kv.first->mType == PlanetType::T1) && kv.second.first >= 2) {
			pollutedScore += kv.second.first;
		} else if (kv.first->mType == PlanetType::T3) {
			greenScore += 2;
		}
	}

	if (pollutedScore > greenScore + 3) return -1; // Экологи ненавидят твои дымящие шахты!
	if (greenScore >= 4 && pollutedScore == 0) return 1; // Экологи восхищаются твоими заповедниками T3!
	return 0;
}

int cSpaceCombatMetrics::GetTradeAffinityBonus(Simulator::cEmpire* emp1, Simulator::cEmpire* emp2)
{
	SdoTradeConfigValues cfg = GetTradeConfig();
	if (cfg.incomeMultiplier <= 0.01f) return 0;
	int routes = GetActiveTradeRoutesBetweenEmpires(emp1, emp2, true);
	if (routes <= 0) return 0;
	if (routes >= cfg.routesPerAffinityPoint * 2) return 2;
	return 1;
}

float cSpaceCombatMetrics::GetTradeWarDeterrenceMultiplier(Simulator::cEmpire* attacker, Simulator::cEmpire* target)
{
	SdoTradeConfigValues cfg = GetTradeConfig();
	if (!attacker || !target || cfg.incomeMultiplier <= 0.01f || cfg.warDeterrenceStrength <= 0.01f) {
		return 1.0f;
	}
	int routes = GetActiveTradeRoutesBetweenEmpires(attacker, target, true);
	if (routes <= 0) return 1.0f;

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(attacker->mArchetype);
	float archSensitivity = 1.0f;
	if (baseArch == Archetypes::kArchetypeTrader || baseArch == Archetypes::kArchetypeDiplomat) {
		archSensitivity = 2.0f;
	} else if (baseArch == Archetypes::kArchetypeWarrior || baseArch == Archetypes::kArchetypeZealot || baseArch == Archetypes::kArchetypeGrob) {
		archSensitivity = 0.35f;
	}

	float deterrence = 1.0f / (1.0f + static_cast<float>(routes) * 0.45f * cfg.warDeterrenceStrength * archSensitivity);
	return max(deterrence, 0.05f);
}

int cSpaceCombatMetrics::ManageAndCalculateEmpireTradeIncome(Simulator::cEmpire* empire, int baseUnitCost, float tradeRange)
{
	SdoTradeConfigValues cfg = GetTradeConfig();
	if (!empire || cfg.incomeMultiplier <= 0.01f) return 0;

	EnsurePersistenceLoaded();
	PruneInvalidTradeRoutes();

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
	int maxAllowedRoutes = cfg.maxRoutesPerEmpire;
	if (baseArch == Archetypes::kArchetypeTrader) maxAllowedRoutes += 3;
	else if (baseArch == Archetypes::kArchetypeBard || baseArch == Archetypes::kArchetypeDiplomat) maxAllowedRoutes += 2;
	else if (baseArch == Archetypes::kArchetypeScientist) maxAllowedRoutes += 1;
	else if (baseArch == Archetypes::kArchetypeZealot || baseArch == Archetypes::kArchetypeGrob) maxAllowedRoutes = max(1, maxAllowedRoutes / 2);

	int currentRoutes = GetEmpireTotalTradeRoutes(empire);
	cCompositionRoot* root = cCompositionRoot::Get();

	if (currentRoutes < maxAllowedRoutes && !empire->mStars.empty() && baseArch != Archetypes::kArchetypeGrob) {
		cStarRecord* originStar = empire->mStars[Math::rand(static_cast<int>(empire->mStars.size()))].get();
		if (originStar && !IsStarBlockaded(originStar)) {
			float rangeMult = 1.35f;
			if (baseArch == Archetypes::kArchetypeScientist) {
				rangeMult = 1.80f;
			} else if (baseArch == Archetypes::kArchetypeShaman) {
				cStarRecord* homeSt = EmpireUtils::GetHomeStar(empire);
				bool homeSevered = (!homeSt || homeSt->mEmpireID != empire->GetEmpireID() || IsStarBlockaded(homeSt));
				rangeMult = homeSevered ? 0.90f : 1.65f; // Return Ticket сворачивает пространство, пока цел Исток!
			}
			float searchRadius = max(tradeRange * rangeMult, 15.0f);
			eastl::vector<cStarRecordPtr> nearbyStars;
			StarRequestFilter filter;
			filter.RemoveStarType(Simulator::StarType::None);
			filter.RemoveStarType(Simulator::StarType::GalacticCore);
			filter.RemoveStarType(Simulator::StarType::ProtoPlanetary);
			filter.RemoveStarType(Simulator::StarType::BlackHole);
			filter.techLevels = 0;
			filter.AddTechLevel(Simulator::TechLevel::Empire);
			filter.minDistance = 0.1f;
			filter.maxDistance = searchRadius;
			StarManager.FindStars(originStar->mPosition, filter, nearbyStars);

			float originSpice = GetNormalizedStarSpiceRatio(originStar);
			cStarRecord* bestPartnerStar = nullptr;
			float bestRouteScore = 0.0f;

			for (const auto& candStar : nearbyStars) {
				if (!candStar || candStar.get() == originStar || candStar->mEmpireID == 0) continue;
				if (IsStarBlockaded(candStar.get())) continue;

				uint32_t idA = originStar->GetID().internalValue;
				uint32_t idB = candStar->GetID().internalValue;
				if (HasTradeRouteBetweenStars(idA, idB)) continue;

				cEmpire* otherEmp = StarManager.GetEmpire(candStar->mEmpireID);
				if (!otherEmp) continue;

				float candSpice = GetNormalizedStarSpiceRatio(candStar.get());
				float spiceDiffRatio = max(originSpice, candSpice) / max(min(originSpice, candSpice), 0.1f);

				// Экологи ценят терраформированные зеленые миры T2/T3 при выборе торгового узла
				float ecoTerraBonus = 1.0f;
				if (baseArch == Archetypes::kArchetypeEcologist) {
					for (const auto& pl : candStar->GetPlanetRecords()) {
						if (pl && pl->GetTechLevel() == TechLevel::Empire && (pl->mType == PlanetType::T2 || pl->mType == PlanetType::T3)) {
							ecoTerraBonus = 1.35f;
							break;
						}
					}
				}

				if (otherEmp == empire) {
					float minDiffNeeded = (baseArch == Archetypes::kArchetypeWarrior) ? 1.05f : 1.25f;
					if (spiceDiffRatio > minDiffNeeded) {
						float internalWeight = (baseArch == Archetypes::kArchetypeWarrior) ? 1.10f : 0.75f;
						float internalScore = spiceDiffRatio * internalWeight * ecoTerraBonus;
						if (internalScore > bestRouteScore) {
							bestRouteScore = internalScore;
							bestPartnerStar = candStar.get();
						}
					}
				} else {
					if (RelationshipManager.IsAtWar(empire, otherEmp)) continue;
					if (root && root->simulationValidator && !root->simulationValidator->ValidEmpire(otherEmp, true)) continue;
					if (GetEmpireTotalTradeRoutes(otherEmp) >= cfg.maxRoutesPerEmpire + 3) continue;

					int affinity = (root && root->empireRelationsAnalyzer)
						? root->empireRelationsAnalyzer->EmpiresAffinity(empire, otherEmp)
						: 0;
					bool allied = RelationshipManager.IsAllied(empire, otherEmp);
					bool openMinded = (baseArch == Archetypes::kArchetypeTrader || baseArch == Archetypes::kArchetypeBard);

					if (allied || affinity >= 1 || (openMinded && affinity >= 0)) {
						float foreignScore = spiceDiffRatio * (allied ? 1.6f : 1.15f) * ecoTerraBonus;
						if (foreignScore > bestRouteScore) {
							bestRouteScore = foreignScore;
							bestPartnerStar = candStar.get();
						}
					}
				}
			}

			if (bestPartnerStar != nullptr) {
				SdoTradeRouteEntry newEntry;
				newEntry.starIdA = min(originStar->GetID().internalValue, bestPartnerStar->GetID().internalValue);
				newEntry.starIdB = max(originStar->GetID().internalValue, bestPartnerStar->GetID().internalValue);
				galacticTradeRoutes.push_back(newEntry);
			}
		}
	}

	uint32_t empID = empire->GetEmpireID();
	float totalTradeProfit = 0.0f;

	for (const auto& route : galacticTradeRoutes) {
		cStarRecord* sA = StarManager.GetStarRecord(StarID(route.starIdA));
		cStarRecord* sB = StarManager.GetStarRecord(StarID(route.starIdB));
		if (!sA || !sB) continue;
		if (sA->mEmpireID != empID && sB->mEmpireID != empID) continue;
		if (IsStarBlockaded(sA) || IsStarBlockaded(sB)) continue;

		float spiceA = GetNormalizedStarSpiceRatio(sA);
		float spiceB = GetNormalizedStarSpiceRatio(sB);
		float arbitrageRatio = max(spiceA, spiceB) / max(min(spiceA, spiceB), 0.1f);
		float avgWealth = (spiceA + spiceB) * 0.5f;

		bool isInternal = (sA->mEmpireID == sB->mEmpireID);
		float routeTypeMult = isInternal ? ((baseArch == Archetypes::kArchetypeWarrior) ? 0.85f : 0.55f) : 1.0f;

		// Шаманы извлекают повышенную выгоду из редких кристаллов пряности
		float shamanCrystalMult = (baseArch == Archetypes::kArchetypeShaman && max(spiceA, spiceB) >= 2.5f) ? 1.25f : 1.0f;

		float routeValue = static_cast<float>(baseUnitCost) * 0.45f * routeTypeMult * shamanCrystalMult *
			(1.0f + (arbitrageRatio - 1.0f) * 0.40f * cfg.spiceArbitrageWeight) *
			(0.75f + min(avgWealth, 8.0f) * 0.15f);
		totalTradeProfit += routeValue;
	}

	float archTradeBonus = 1.0f;
	if (baseArch == Archetypes::kArchetypeTrader) archTradeBonus = 1.45f;
	else if (baseArch == Archetypes::kArchetypeScientist) archTradeBonus = 1.20f;
	else if (baseArch == Archetypes::kArchetypeBard) archTradeBonus = 1.15f;

	return max(static_cast<int>(round(totalTradeProfit * archTradeBonus * cfg.incomeMultiplier)), 0);
}

int cSpaceCombatMetrics::BreakTradeRoutesOnWar(Simulator::cEmpire* emp1, Simulator::cEmpire* emp2, int baseUnitCost)
{
	if (!emp1 || !emp2 || emp1 == emp2) return 0;
	EnsurePersistenceLoaded();

	uint32_t id1 = emp1->GetEmpireID();
	uint32_t id2 = emp2->GetEmpireID();
	int brokenCount = 0;

	for (size_t i = 0; i < galacticTradeRoutes.size(); ) {
		cStarRecord* sA = StarManager.GetStarRecord(StarID(galacticTradeRoutes[i].starIdA));
		cStarRecord* sB = StarManager.GetStarRecord(StarID(galacticTradeRoutes[i].starIdB));
		bool betweenThem = sA && sB &&
			((sA->mEmpireID == id1 && sB->mEmpireID == id2) ||
			 (sA->mEmpireID == id2 && sB->mEmpireID == id1));

		if (betweenThem) {
			galacticTradeRoutes.erase(galacticTradeRoutes.begin() + i);
			brokenCount++;
		} else {
			++i;
		}
	}

	if (brokenCount > 0) {
		SdoTradeConfigValues cfg = GetTradeConfig();
		int baseShock = static_cast<int>(round(static_cast<float>(brokenCount * baseUnitCost * 3) * cfg.warDeterrenceStrength));
		float mult1 = (ArchetypeUtils::GetBaseArchetype(emp1->mArchetype) == Archetypes::kArchetypeDiplomat) ? 2.0f : 1.0f;
		float mult2 = (ArchetypeUtils::GetBaseArchetype(emp2->mArchetype) == Archetypes::kArchetypeDiplomat) ? 2.0f : 1.0f;
		emp1->mEmpireMoney = max(emp1->mEmpireMoney - static_cast<int>(round(baseShock * mult1)), 0);
		emp2->mEmpireMoney = max(emp2->mEmpireMoney - static_cast<int>(round(baseShock * mult2)), 0);
	}
	return brokenCount;
}

void cSpaceCombatMetrics::GetStarActiveTradePartners(
	Simulator::cStarRecord* star,
	eastl::vector<Simulator::cEmpire*>& outPartners,
	bool& outBlockaded)
{
	outPartners.clear();
	outBlockaded = false;
	if (!star || GetTradeConfig().incomeMultiplier <= 0.01f) return;

	EnsurePersistenceLoaded();
	if (IsStarBlockaded(star)) {
		outBlockaded = true;
	}

	auto addUniquePartner = [&](cEmpire* emp) {
		if (!emp || emp->GetEmpireID() == star->mEmpireID) return;
		cEmpire* owner = StarManager.GetEmpire(star->mEmpireID);
		if (owner && RelationshipManager.IsAtWar(owner, emp)) return;
		for (cEmpire* existing : outPartners) {
			if (existing == emp) return;
		}
		outPartners.push_back(emp);
	};

	cEmpire* starOwner = StarManager.GetEmpire(star->mEmpireID);
	cEmpire* playerEmp = GetPlayerEmpire();

	// 1. Все ванильные синие линии Spore в обе стороны (Игрок <-> НПС)
	if (starOwner && playerEmp) {
		if (starOwner != playerEmp) {
			if (!RelationshipManager.IsAtWar(playerEmp, starOwner)) {
				if (StarManager.mTradeRouteManager.HasTradeRoute(star->GetID(), playerEmp->GetEmpireID())) {
					addUniquePartner(playerEmp);
				} else {
					for (const auto& pStar : playerEmp->mStars) {
						if (pStar != nullptr &&
						    StarManager.mTradeRouteManager.HasTradeRoute(pStar->GetID(), starOwner->GetEmpireID())) {
							if (IsStarBlockaded(pStar.get())) outBlockaded = true;
							addUniquePartner(playerEmp);
							break;
						}
					}
				}
			}
		} else {
			eastl::vector<cStarRecordPtr> nearStars;
			StarRequestFilter trFilter;
			trFilter.techLevels = 0;
			trFilter.AddTechLevel(Simulator::TechLevel::Empire);
			trFilter.minDistance = 0.1f;
			trFilter.maxDistance = 25.0f;
			StarManager.FindStars(star->mPosition, trFilter, nearStars);

			for (const auto& nStar : nearStars) {
				if (nStar == nullptr || nStar->mEmpireID == 0 || nStar->mEmpireID == star->mEmpireID) continue;
				cEmpire* partnerEmp = StarManager.GetEmpire(nStar->mEmpireID);
				if (!partnerEmp || RelationshipManager.IsAtWar(playerEmp, partnerEmp)) continue;

				if (StarManager.mTradeRouteManager.HasTradeRoute(star->GetID(), nStar->mEmpireID) ||
				    StarManager.mTradeRouteManager.HasTradeRoute(nStar->GetID(), star->mEmpireID)) {
					if (!IsStarBlockaded(nStar.get())) {
						addUniquePartner(partnerEmp);
					}
				}
			}
		}
	}

	// 2. Все маршруты ИИ <-> ИИ и ИИ <-> Игрок из нашей торговой сети
	// Определяем, является ли эта звезда крупным торговым хабом империи (Столица или планета T3)
	bool isMajorEmpireHub = false;
	if (starOwner && EmpireUtils::GetHomeStar(starOwner) == star) {
		isMajorEmpireHub = true;
	} else {
		for (const auto& pl : star->GetPlanetRecords()) {
			if (pl && pl->GetTechLevel() == TechLevel::Empire && pl->mType == PlanetType::T3) {
				isMajorEmpireHub = true;
				break;
			}
		}
	}

	// 2. Маршруты ИИ <-> ИИ и ИИ <-> Игрок (на крупных хабах Столиц/T3 собираются корабли ВСЕХ партнеров державы!)
	uint32_t targetStarId = star->GetID().internalValue;
	uint32_t ownerEmpireId = star->mEmpireID;

	for (const auto& route : galacticTradeRoutes) {
		cStarRecord* sA = StarManager.GetStarRecord(StarID(route.starIdA));
		cStarRecord* sB = StarManager.GetStarRecord(StarID(route.starIdB));
		if (!sA || !sB || sA->mEmpireID == 0 || sB->mEmpireID == 0) continue;

		bool directStarMatch = (route.starIdA == targetStarId || route.starIdB == targetStarId);
		bool empireHubMatch = isMajorEmpireHub && (sA->mEmpireID == ownerEmpireId || sB->mEmpireID == ownerEmpireId);
		if (!directStarMatch && !empireHubMatch) continue;

		cStarRecord* otherStar = (sA->mEmpireID == ownerEmpireId) ? sB : sA;
		if (directStarMatch) {
			otherStar = (route.starIdA == targetStarId) ? sB : sA;
		}

		if (otherStar && otherStar->mEmpireID != ownerEmpireId && !IsStarBlockaded(otherStar)) {
			addUniquePartner(StarManager.GetEmpire(otherStar->mEmpireID));
		}
	}
}

Simulator::cEmpire* cSpaceCombatMetrics::GetStarActiveTradePartner(Simulator::cStarRecord* star, bool& outBlockaded)
{
	eastl::vector<Simulator::cEmpire*> partners;
	GetStarActiveTradePartners(star, partners, outBlockaded);
	return partners.empty() ? nullptr : partners.front();
}


bool cSpaceCombatMetrics::StarHasAnyActiveTradeRoute(Simulator::cStarRecord* star, bool& outBlockaded)
{
	outBlockaded = false;
	if (!star || GetTradeConfig().incomeMultiplier <= 0.01f) return false;
	if (GetStarActiveTradePartner(star, outBlockaded) != nullptr) {
		return true;
	}

	EnsurePersistenceLoaded();
	uint32_t targetStarId = star->GetID().internalValue;
	for (const auto& route : galacticTradeRoutes) {
		if (route.starIdA != targetStarId && route.starIdB != targetStarId) continue;
		uint32_t otherStarId = (route.starIdA == targetStarId) ? route.starIdB : route.starIdA;
		cStarRecord* otherStar = StarManager.GetStarRecord(StarID(otherStarId));
		if (!otherStar || otherStar->mEmpireID == 0) continue;

		outBlockaded = IsStarBlockaded(star) || IsStarBlockaded(otherStar);
		return true;
	}
	return false;
}

void cSpaceCombatMetrics::CalculateDynamicConvoySpecs(
	Simulator::cPlanetRecord* planet,
	Simulator::cEmpire* convoyEmpire,
	int& outShipCount,
	float& outScaleMult,
	float& outHpMult)
{
	SdoTradeConfigValues cfg = GetTradeConfig();
	if (!planet || !convoyEmpire || cfg.incomeMultiplier <= 0.01f || cfg.convoyDensity <= 0.01f) {
		outShipCount = 0;
		outScaleMult = 1.0f;
		outHpMult = 1.0f;
		return;
	}

	float tierVolume = 0.8f;
	if (PlanetUtils::Homeworld(planet)) tierVolume = 2.2f;
	else if (planet->mType == PlanetType::T3) tierVolume = 1.8f;
	else if (planet->mType == PlanetType::T2) tierVolume = 1.3f;
	else if (planet->mType == PlanetType::T1) tierVolume = 1.0f;

	int cities = max(GetCachedCityCount(planet), 1);
	float cityBonus = 1.0f + min(static_cast<float>(cities - 1) * 0.15f, 1.2f);

	SdoPlanetGadgetInfo gadgets = GetPlanetGadgetInfo(planet);
	float depotBonus = gadgets.hasSpiceStorage ? 1.35f : 1.0f;

	Simulator::Archetypes arch = ArchetypeUtils::GetBaseArchetype(convoyEmpire->mArchetype);
	bool isPlayerKnight = (convoyEmpire->mArchetype == Archetypes::kArchetypePlayerKnight);

	float archFleetBonus = 1.0f;
	if (arch == Archetypes::kArchetypeTrader) archFleetBonus = 1.35f;
	else if (arch == Archetypes::kArchetypeBard) archFleetBonus = 1.25f;
	else if (isPlayerKnight) archFleetBonus = 1.20f;
	else if (arch == Archetypes::kArchetypeEcologist && (planet->mType == PlanetType::T3 || PlanetUtils::Homeworld(planet))) {
		archFleetBonus = 1.25f;
	}

	cEmpire* hostEmpire = planet->GetStarRecord() ? StarManager.GetEmpire(planet->GetStarRecord()->mEmpireID) : nullptr;
	bool isDiplomatHostEscort = (hostEmpire != nullptr && hostEmpire != convoyEmpire &&
		ArchetypeUtils::GetBaseArchetype(hostEmpire->mArchetype) == Archetypes::kArchetypeDiplomat);

	int rawCount = static_cast<int>(round(tierVolume * cityBonus * depotBonus * archFleetBonus * cfg.convoyDensity));
	if (isDiplomatHostEscort) rawCount += 1;
	outShipCount = min(max(rawCount, 1), max(cfg.maxRoutesPerEmpire, 6));

	float spiceRatio = planet->GetStarRecord() ? GetNormalizedStarSpiceRatio(planet->GetStarRecord()) : 1.0f;
	float logSpice = std::log2(max(spiceRatio, 1.0f));
	if (arch == Archetypes::kArchetypeShaman) {
		logSpice *= 1.25f;
	}

	outScaleMult = cfg.convoyBaseScale + (logSpice * cfg.convoyScalePerSpice * cfg.spiceArbitrageWeight);
	outScaleMult = min(max(outScaleMult, 1.05f), 2.20f);

	float knightEscortArmor = isPlayerKnight ? 1.15f : 1.0f;
	float diplomatFederationShield = isDiplomatHostEscort ? 1.35f : 1.0f;
	outHpMult = outScaleMult * (gadgets.hasUberTurret ? 1.25f : 1.10f) * knightEscortArmor * diplomatFederationShield;
}

void cSpaceCombatMetrics::BootstrapInitialTradeNetwork()
{
	SdoTradeConfigValues cfg = GetTradeConfig();
	if (cfg.incomeMultiplier <= 0.01f || !IsSpaceGame()) return;

	if (static_cast<int>(galacticTradeRoutes.size()) >= cfg.maxRoutesPerEmpire * 2) {
		return;
	}

	cCompositionRoot* root = cCompositionRoot::Get();
	cEmpire* playerEmp = GetPlayerEmpire();
	if (!playerEmp || playerEmp->mStars.empty()) return;

	StarRequestFilter empStarsFilter;
	empStarsFilter.RemoveStarType(Simulator::StarType::None);
	empStarsFilter.RemoveStarType(Simulator::StarType::GalacticCore);
	empStarsFilter.RemoveStarType(Simulator::StarType::ProtoPlanetary);
	empStarsFilter.RemoveStarType(Simulator::StarType::BlackHole);
	empStarsFilter.techLevels = 0;
	empStarsFilter.AddTechLevel(Simulator::TechLevel::Empire);
	empStarsFilter.minDistance = 0.0f;
	empStarsFilter.maxDistance = 40.0f;

	eastl::vector<cStarRecordPtr> sectorStars;
	StarManager.FindStars(playerEmp->mStars.front()->mPosition, empStarsFilter, sectorStars);

	eastl::vector<cEmpire*> activeEmpires;
	for (const auto& st : sectorStars) {
		if (!st || st->mEmpireID == 0) continue;
		cEmpire* emp = StarManager.GetEmpire(st->mEmpireID);
		if (!emp || emp == playerEmp) continue;
		if (root && root->simulationValidator && !root->simulationValidator->ValidEmpire(emp, true)) continue;

		bool exists = false;
		for (cEmpire* e : activeEmpires) {
			if (e == emp) { exists = true; break; }
		}
		if (!exists) activeEmpires.push_back(emp);
	}

	int initialPassCount = max(1, cfg.maxRoutesPerEmpire / 2);
	for (int pass = 0; pass < initialPassCount; ++pass) {
		for (cEmpire* emp : activeEmpires) {
			ManageAndCalculateEmpireTradeIncome(emp, 1000, 18.0f);
		}
	}
}

void cSpaceCombatMetrics::EnsurePersistenceLoaded()
{
	if (persistenceImported || !IsSpaceGame()) return;
	cDatabaseManager* dbMgr = cDatabaseManager::Get();
	if (dbMgr != nullptr) {
		cPersistenceState* state = dbMgr->GetPersistenceState();
		if (state != nullptr) {
			ImportStateFromPersistence(state);
			BootstrapInitialTradeNetwork();
		}
	}
}

void cSpaceCombatMetrics::ExportStateToPersistence(cPersistenceState* state)
{
	if (state == nullptr) return;
	EnsurePersistenceLoaded();
	PruneInvalidTradeRoutes();

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

	state->tradeRouteStarA.clear();
	state->tradeRouteStarB.clear();

	for (const auto& route : galacticTradeRoutes) {
		if (route.starIdA != 0 && route.starIdB != 0) {
			state->tradeRouteStarA.push_back(route.starIdA);
			state->tradeRouteStarB.push_back(route.starIdB);
		}
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

	galacticTradeRoutes.clear();
	size_t tradeCount = min(state->tradeRouteStarA.size(), state->tradeRouteStarB.size());
	for (size_t i = 0; i < tradeCount; ++i) {
		uint32_t sA = state->tradeRouteStarA[i];
		uint32_t sB = state->tradeRouteStarB[i];
		if (sA != 0 && sB != 0 && !HasTradeRouteBetweenStars(sA, sB)) {
			SdoTradeRouteEntry entry;
			entry.starIdA = min(sA, sB);
			entry.starIdB = max(sA, sB);
			galacticTradeRoutes.push_back(entry);
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
	if (purchasedInfo.hasLoyaltyBooster)   current.hasLoyaltyBooster = true;
	if (purchasedInfo.hasHappinessBooster) current.hasHappinessBooster = true;
	if (purchasedInfo.hasUberTurret)       current.hasUberTurret = true;
	if (purchasedInfo.hasSpiceStorage)     current.hasSpiceStorage = true;
	if (purchasedInfo.hasBioProtector)     current.hasBioProtector = true;
	if (purchasedInfo.hasEmbassy)          current.hasEmbassy = true;
	if (purchasedInfo.aiEmbassyEmpireID != 0) current.aiEmbassyEmpireID = purchasedInfo.aiEmbassyEmpireID;
}

void cSpaceCombatMetrics::UpdatePlanetDefenseCache(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) return;
	EnsurePersistenceLoaded();

	if (GetCurrentContext() == SpaceContext::Planet && planet == GetActivePlanetRecord()) {
		int cityCount = 0;
		int turretCount = 0;
		const auto& cities = Simulator::GetData<Simulator::cCity>();
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
			const auto& spatials = Simulator::GetDataByCast<Simulator::cSpatialObject>();
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
		if (itAi->second.hasLoyaltyBooster)   mergedInfo.hasLoyaltyBooster = true;
		if (itAi->second.hasHappinessBooster) mergedInfo.hasHappinessBooster = true;
		if (itAi->second.hasUberTurret)       mergedInfo.hasUberTurret = true;
		if (itAi->second.hasSpiceStorage)     mergedInfo.hasSpiceStorage = true;
		if (itAi->second.hasBioProtector)     mergedInfo.hasBioProtector = true;
		if (itAi->second.hasEmbassy)          mergedInfo.hasEmbassy = true;
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

int cSpaceCombatMetrics::GetDefendersInOutpost() { return numDefendersInOutpost; }
int cSpaceCombatMetrics::GetDefendersInColony()  { return numDefendersInColony; }
int cSpaceCombatMetrics::GetFightersPerBomber()  { return numFightersPerBomber; }
int cSpaceCombatMetrics::GetTurretsInT1()        { return numTurretsInT1; }
int cSpaceCombatMetrics::GetTurretsInT2()        { return numTurretsInT2; }
int cSpaceCombatMetrics::GetTurretsInT3()        { return numTurretsInT3; }

float cSpaceCombatMetrics::GetBomberHealthByEmpire(Simulator::cEmpire* empire)
{
	if (empire == nullptr) return GetBomberHealthByLevel(0);
	float baseHealth = GetBomberHealthByLevel(empire->GetWeaponryLevel());
	float levelFactor = static_cast<float>(empire->GetWeaponryLevel() + 1) / 5.0f;
	Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);

	switch (baseArchetype) {
	case Archetypes::kArchetypeWarrior:
	case Archetypes::kArchetypeGrob:
		return baseHealth * (1.0f + (0.30f * levelFactor));
	case Archetypes::kArchetypeZealot:
	case Archetypes::kArchetypeScientist:
		return baseHealth * (1.0f + (0.10f * levelFactor));
	case Archetypes::kArchetypeBard:
	case Archetypes::kArchetypeDiplomat:
		return baseHealth * (1.0f - (0.20f * (1.0f - levelFactor * 0.5f)));
	case Archetypes::kArchetypeShaman: {
		cStarRecord* homeSt = EmpireUtils::GetHomeStar(empire);
		bool homeSevered = (!homeSt || homeSt->mEmpireID != empire->GetEmpireID() || IsStarBlockaded(homeSt));
		if (homeSevered) {
			float nexusPenalty = 0.30f;
			PropertyListPtr gProp;
			if (PropManager.GetPropertyList(id(u"GadgetConfig"), 0x02ae0c7e, gProp) && gProp != nullptr) {
				App::Property::GetFloat(gProp.get(), 0x07A1BC13, nexusPenalty);
			}
			return baseHealth * max(0.45f, 1.0f - nexusPenalty);
		}
		return baseHealth;
	}
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
	case Archetypes::kArchetypeGrob:
		return baseHealth * (1.0f + (0.30f * levelFactor));
	case Archetypes::kArchetypeZealot:
	case Archetypes::kArchetypeScientist:
		return baseHealth * (1.0f + (0.10f * levelFactor));
	case Archetypes::kArchetypeBard:
	case Archetypes::kArchetypeDiplomat:
		return baseHealth * (1.0f - (0.20f * (1.0f - levelFactor * 0.5f)));
	case Archetypes::kArchetypeShaman: {
		cStarRecord* homeSt = EmpireUtils::GetHomeStar(empire);
		bool homeSevered = (!homeSt || homeSt->mEmpireID != empire->GetEmpireID() || IsStarBlockaded(homeSt));
		if (homeSevered) {
			float nexusPenalty = 0.30f;
			PropertyListPtr gProp;
			if (PropManager.GetPropertyList(id(u"GadgetConfig"), 0x02ae0c7e, gProp) && gProp != nullptr) {
				App::Property::GetFloat(gProp.get(), 0x07A1BC13, nexusPenalty);
			}
			return baseHealth * max(0.45f, 1.0f - nexusPenalty);
		}
		return baseHealth;
	}
	default:
		return baseHealth;
	}
}

float cSpaceCombatMetrics::GetDefenderHealthByEmpire(Simulator::cEmpire* empire)
{
	if (empire == nullptr) return GetDefenderHealthByLevel(0);

	float baseHealth = GetDefenderHealthByLevel(empire->GetWeaponryLevel());
	if (empire == GetPlayerEmpire()) {
		float empScale = 1.0f + min(static_cast<float>(empire->mStars.size()) * 0.012f, 0.35f);
		baseHealth *= empScale;
		if (empire->mArchetype == Archetypes::kArchetypePlayerKnight) {
			float lvlF = static_cast<float>(empire->GetWeaponryLevel() + 1) / 5.0f;
			return baseHealth * (1.0f + 0.30f * lvlF);
		}
	}


	float levelFactor = static_cast<float>(empire->GetWeaponryLevel() + 1) / 5.0f;
	Simulator::Archetypes baseArchetype = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);

	switch (baseArchetype) {
	case Archetypes::kArchetypeWarrior:
	case Archetypes::kArchetypeGrob:
		return baseHealth * (1.0f + (0.30f * levelFactor));
	case Archetypes::kArchetypeZealot:
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
	float baseTurretHP = GetTurretHealthByLevel(empire->GetWeaponryLevel());
	if (ArchetypeUtils::GetBaseArchetype(empire->mArchetype) == Archetypes::kArchetypeScientist) {
		return baseTurretHP * 1.35f; // Высокотехнологичные ИИ-турели Ученых
	}
	return baseTurretHP;
}

int cSpaceCombatMetrics::GetDefendersInPlanet(Simulator::cPlanetRecord* planet)
{
	if (planet == nullptr) return GetDefendersInColony();

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
			if (planet->mType == PlanetType::T0) archetypeMultiplier = 0.55f; // Коллапс биосферы на мертвом камне T0!
			else if (planet->mType == PlanetType::T3 || PlanetUtils::Homeworld(planet)) {
				archetypeMultiplier = (GetCurrentContext() != SpaceContext::Planet) ? 2.35f : 2.0f; // В автобое учитываем регенерацию роя!
			} else {
				archetypeMultiplier = 1.25f;
			}
			break;
		default:
			archetypeMultiplier = 0.8f;
			break;
		}

		float weaponLevelBonus = (empire->GetWeaponryLevel() + 1) * 0.6f;
		baseDefenders = static_cast<int>(round((baseDefenders + weaponLevelBonus) * archetypeMultiplier));
	}
	else if (planet->mType == PlanetType::T0 && empire != nullptr &&
	         ArchetypeUtils::GetBaseArchetype(empire->mArchetype) == Archetypes::kArchetypeEcologist) {
		baseDefenders = max(1, static_cast<int>(round(static_cast<float>(baseDefenders) * 0.55f)));
	}

	int standardCities = (planet->mType == PlanetType::T3) ? 3 : ((planet->mType == PlanetType::T2) ? 2 : 1);
	if (cachedCities > standardCities) {
		baseDefenders += (cachedCities - standardCities) * max(baseColonyDefenders / 2, 1);
	}
	if (gadgets.hasSpiceStorage) {
		baseDefenders = static_cast<int>(round(static_cast<float>(baseDefenders) * (1.0f + 0.25f * GetExternalGadgetMultiplier())));
	}

	// В фоновых войнах НПС <-> НПС на карте галактики учитываем Астрального Стража и пул Реинкарнации Шаманов!
	// (В 3D на самой планете не плюсуем в общий лимит, так как Страж и перерождения спавнятся там отдельно со спецэффектами)
	if (GetCurrentContext() != SpaceContext::Planet && empire != nullptr &&
	    ArchetypeUtils::GetBaseArchetype(empire->mArchetype) == Archetypes::kArchetypeShaman &&
	    GetExternalGadgetMultiplier() > 0.01f) {

		cStarRecord* homeSt = EmpireUtils::GetHomeStar(empire);
		bool homeSevered = (!homeSt || homeSt->mEmpireID != empire->GetEmpireID() || IsStarBlockaded(homeSt));
		if (!homeSevered && planet->mType != PlanetType::T0) {
			float reincXml = 0.38f;
			float nexusXml = 0.30f;
			PropertyListPtr gProp;
			if (PropManager.GetPropertyList(id(u"GadgetConfig"), 0x02ae0c7e, gProp) && gProp != nullptr) {
				App::Property::GetFloat(gProp.get(), 0x07A1BC12, reincXml);
				App::Property::GetFloat(gProp.get(), 0x07A1BC13, nexusXml);
			}

			// 1. Эквивалент телепортирующегося со Столицы Астрального Стража (~2-3 обычных защитника)
			int astralWardenEquiv = (!PlanetUtils::Homeworld(planet) && nexusXml > 0.01f)
				? max(1, static_cast<int>(round(2.0f * (nexusXml / 0.30f))))
				: 0;

			// 2. Математическое ожидание числа воскресших кораблей на живой биосфере (T1..T3)
			int tVal = (planet->mType == PlanetType::T3) ? 3 : ((planet->mType == PlanetType::T2) ? 2 : 1);
			float gaiaFactor = std::pow(static_cast<float>(tVal) / 3.0f, 1.25f) * (gadgets.hasBioProtector ? 1.25f : 1.0f);
			int expectedRebirths = static_cast<int>(round(static_cast<float>(baseDefenders) * reincXml * gaiaFactor * 0.65f));

			baseDefenders += (astralWardenEquiv + expectedRebirths);
		}
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
