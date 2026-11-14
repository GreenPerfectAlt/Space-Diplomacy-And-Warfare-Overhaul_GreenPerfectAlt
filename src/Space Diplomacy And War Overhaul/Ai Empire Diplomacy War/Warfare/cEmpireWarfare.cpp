#include "stdafx.h"
#include "cEmpireWarfare.h"
#include "../cCompositionRoot.h"
#include <Spore-Mod-Utils/Include/SporeModUtils.h>
#include <algorithm>
#include <EASTL/sort.h>
#include <cmath>

using namespace SporeModUtils;
using namespace Simulator;

static float GetAiPlanetSpiceValue(Simulator::cPlanetRecord* planet)
{
	if (!planet) return 0.0f;
	uint32_t spiceID = planet->mSpiceGen.instanceID;
	if (spiceID == 0) return 0.0f;

	PropertyListPtr spiceProp;
	if (PropManager.GetPropertyList(spiceID, 0x034D97FA, spiceProp) && spiceProp != nullptr) {
		float baseCost = 0.0f;
		if (App::Property::GetFloat(spiceProp.get(), 0x00B2CCCB, baseCost) && baseCost > 0.0f) {
			return baseCost;
		}
	}
	if (spiceID == 0x0289B841 || spiceID == 0x0289B842) return 3000.0f;
	if (spiceID == 0x0289B83F || spiceID == 0x0289B840) return 1200.0f;
	return 300.0f;
}

static void InvestAiSurplusInColonyGadgets(
	Simulator::cEmpire* empire,
	Simulator::Archetypes baseArch,
	int dynamicMaxTreasury,
	int baseUnitCost,
	float logisticsRange)
{
	if (!empire) return;

	float gadgetMult = 1.0f;
	PropertyListPtr gadgetProp;
	if (PropManager.GetPropertyList(id(u"GadgetConfig"), 0x02ae0c7e, gadgetProp) && gadgetProp != nullptr) {
		App::Property::GetFloat(gadgetProp.get(), 0x07A1BC06, gadgetMult);
	}
	if (gadgetMult <= 0.01f) return;

	cCompositionRoot* root = cCompositionRoot::Get();
	if (!root || !root->spaceCombatMetrics) return;

	// Инвестиции начинаются только при профиците бюджета (> 55% от потолка казны, либо > 45% в полном мире)
	float investRatio = empire->mEnemies.empty() ? 0.45f : 0.65f;
	int investThreshold = static_cast<int>(round(static_cast<float>(dynamicMaxTreasury) * investRatio));
	if (empire->mEmpireMoney < investThreshold) return;

	int costBioOrHappy = max(baseUnitCost * 5, 2500);
	int costLoyaltyOrEmbassy = max(baseUnitCost * 6, 3000);
	int costSpiceStorage = max(baseUnitCost * 8, 4000);
	if (baseArch == Archetypes::kArchetypeTrader) {
		costSpiceStorage = static_cast<int>(round(static_cast<float>(costSpiceStorage) * 0.80f));
	}
	int costUberTurret = max(baseUnitCost * 15, 8000);

	// 1. Дипломатическая миссия: открытие Посольства ИИ на планете ИГРОКА!
	cEmpire* playerEmp = GetPlayerEmpire();
	bool likesDiplomacy = (baseArch == Archetypes::kArchetypeDiplomat || baseArch == Archetypes::kArchetypeTrader ||
	                       baseArch == Archetypes::kArchetypeBard || baseArch == Archetypes::kArchetypeShaman ||
	                       baseArch == Archetypes::kArchetypeEcologist);

	if (playerEmp && playerEmp != empire && likesDiplomacy &&
	    !RelationshipManager.IsAtWar(empire, playerEmp) &&
	    empire->mEmpireMoney >= (investThreshold + costLoyaltyOrEmbassy)) {

		int affinity = (root->empireRelationsAnalyzer != nullptr)
			? root->empireRelationsAnalyzer->EmpiresAffinity(empire, playerEmp)
			: 0;
		bool isAllied = RelationshipManager.IsAllied(empire, playerEmp);

		if (isAllied || affinity >= 1 || (baseArch == Archetypes::kArchetypeDiplomat && affinity >= 0)) {
			if (Math::randf() < 0.25f) {
				for (const auto& pStar : playerEmp->mStars) {
					if (!pStar) continue;
					for (const auto& pPlanet : pStar->GetPlanetRecords()) {
						if (!pPlanet || pPlanet->GetTechLevel() != TechLevel::Empire) continue;
						if (!PlanetUtils::Homeworld(pPlanet.get()) && pPlanet->mType != PlanetType::T3) continue;

						SdoPlanetGadgetInfo curInfo = root->spaceCombatMetrics->GetPlanetGadgetInfo(pPlanet.get());
						if (curInfo.aiEmbassyEmpireID == 0) {
							SdoPlanetGadgetInfo embassyPurchase;
							embassyPurchase.hasEmbassy = true;
							embassyPurchase.aiEmbassyEmpireID = empire->GetEmpireID();
							root->spaceCombatMetrics->RegisterAiGadgetPurchase(pPlanet.get(), embassyPurchase);
							empire->mEmpireMoney -= costLoyaltyOrEmbassy;
							return; // Не более 1 крупного госпроекта за цикл
						}
					}
				}
			}
		}
	}

	cStarRecord* homeStar = EmpireUtils::GetHomeStar(empire);

	// 2. Убербашня (+ Орбитальный Дредноут) при высоком богатстве казны (> 75% MaxCap)
	int uberThreshold = static_cast<int>(round(static_cast<float>(dynamicMaxTreasury) * 0.75f));
	if (empire->mEmpireMoney >= max(uberThreshold, costUberTurret * 2)) {
		bool militaristOrTech = (baseArch == Archetypes::kArchetypeWarrior || baseArch == Archetypes::kArchetypeScientist ||
		                         baseArch == Archetypes::kArchetypeZealot || baseArch == Archetypes::kArchetypeGrob ||
		                         baseArch == Archetypes::kArchetypeTrader);
		if (militaristOrTech || !empire->mEnemies.empty()) {
			for (const auto& star : empire->mStars) {
				if (!star) continue;
				for (const auto& pl : star->GetPlanetRecords()) {
					if (!pl || pl->GetTechLevel() != TechLevel::Empire) continue;
					if (!PlanetUtils::Homeworld(pl.get()) && pl->mType != PlanetType::T3) continue;

					SdoPlanetGadgetInfo gInfo = root->spaceCombatMetrics->GetPlanetGadgetInfo(pl.get());
					if (!gInfo.hasUberTurret) {
						SdoPlanetGadgetInfo buyUber;
						buyUber.hasUberTurret = true;
						root->spaceCombatMetrics->RegisterAiGadgetPurchase(pl.get(), buyUber);
						empire->mEmpireMoney -= costUberTurret;
						return;
					}
				}
			}
		}
	}

	// 3. Инфраструктурные модули по философии (Хранилище пряности, Усилитель преданности на окраинах, Биозащита/Счастье)
	for (const auto& star : empire->mStars) {
		if (!star) continue;
		float distFromHome = (homeStar != nullptr && homeStar != star.get())
			? StarUtils::GetDistanceBetweenStars(homeStar, star.get())
			: 0.0f;

		for (const auto& pl : star->GetPlanetRecords()) {
			if (!pl || pl->GetTechLevel() != TechLevel::Empire) continue;
			SdoPlanetGadgetInfo gInfo = root->spaceCombatMetrics->GetPlanetGadgetInfo(pl.get());
			float spiceVal = GetAiPlanetSpiceValue(pl.get());

			// А) Хранилище пряности на богатых сырьевых мирах или Т3 узлах
			if (!gInfo.hasSpiceStorage && empire->mEmpireMoney >= (investThreshold + costSpiceStorage)) {
				if (spiceVal >= 1200.0f || pl->mType == PlanetType::T3 || baseArch == Archetypes::kArchetypeTrader) {
					SdoPlanetGadgetInfo buyStorage;
					buyStorage.hasSpiceStorage = true;
					root->spaceCombatMetrics->RegisterAiGadgetPurchase(pl.get(), buyStorage);
					empire->mEmpireMoney -= costSpiceStorage;
					return;
				}
			}

			// Б) Усилитель преданности у Воинов и Торговцев на далеких мятежных колониях окраин
			if (!gInfo.hasLoyaltyBooster &&
			    (baseArch == Archetypes::kArchetypeWarrior || baseArch == Archetypes::kArchetypeTrader) &&
			    distFromHome > max(logisticsRange, 10.0f) &&
			    (pl->mType == PlanetType::T0 || pl->mType == PlanetType::T1) &&
			    empire->mEmpireMoney >= (investThreshold + costLoyaltyOrEmbassy)) {

				SdoPlanetGadgetInfo buyLoyalty;
				buyLoyalty.hasLoyaltyBooster = true;
				root->spaceCombatMetrics->RegisterAiGadgetPurchase(pl.get(), buyLoyalty);
				empire->mEmpireMoney -= costLoyaltyOrEmbassy;
				return;
			}

			// В) Биозащита и Усилитель счастья у гармоничных и научных империй
			if (!gInfo.hasBioProtector && !gInfo.hasHappinessBooster &&
			    (baseArch == Archetypes::kArchetypeEcologist || baseArch == Archetypes::kArchetypeScientist ||
			     baseArch == Archetypes::kArchetypeShaman || baseArch == Archetypes::kArchetypeBard ||
			     baseArch == Archetypes::kArchetypeDiplomat) &&
			    empire->mEmpireMoney >= (investThreshold + costBioOrHappy)) {

				SdoPlanetGadgetInfo buyEco;
				buyEco.hasBioProtector = true;
				buyEco.hasHappinessBooster = (pl->mType == PlanetType::T2 || pl->mType == PlanetType::T3);
				root->spaceCombatMetrics->RegisterAiGadgetPurchase(pl.get(), buyEco);
				empire->mEmpireMoney -= costBioOrHappy;
				return;
			}
		}
	}
}

cEmpireWarfare::cEmpireWarfare(Simulator::cEmpire* empire,
	cSimulationValidator* simulationValidator,
	cWarfareConfig* warfareConfig, 
	cWarfareStrengthAnalyzer* warfareStrengthAnalyzer, 
	cWarfareEventDispatcher* warfareEventDispatcher)
{
	this->empire = empire;
	this->range = 0;
	this->attackedPlanets = 0;
	this->simulationValidator = simulationValidator;
	this->warfareConfig = warfareConfig;
	this->warfareStrengthAnalyzer = warfareStrengthAnalyzer;
	this->warfareEventDispatcher = warfareEventDispatcher;
}

cEmpireWarfare::~cEmpireWarfare()
{
}

int cEmpireWarfare::AddRef()
{
	return DefaultRefCounted::AddRef();
}

int cEmpireWarfare::Release()
{
	return DefaultRefCounted::Release();
}

void* cEmpireWarfare::Cast(uint32_t type) const
{
	CLASS_CAST(Object);
	CLASS_CAST(cEmpireWarfare);
	return nullptr;
}

bool cEmpireWarfare::AtackableStar(Simulator::cStarRecord* star) {
	for (cPlanetRecordPtr planet : star->GetPlanetRecords()) {
		if (MissionManager.ThereIsEventInPlanet(planet.get())) {
			return false;
		}
	}
	return true;
}

eastl::vector<cStarRecord*> cEmpireWarfare::GetEnemyStarsInRangeOfStar(Simulator::cStarRecord* star) {
	eastl::vector<cStarRecordPtr> closeStars;

	StarRequestFilter filter;
	filter.RemoveStarType(Simulator::StarType::None);
	filter.RemoveStarType(Simulator::StarType::GalacticCore);
	filter.RemoveStarType(Simulator::StarType::ProtoPlanetary);
	filter.RemoveStarType(Simulator::StarType::BlackHole);
	filter.techLevels = 0;
	filter.AddTechLevel(Simulator::TechLevel::Empire);
	filter.minDistance = 0;
	filter.maxDistance = range;
	StarManager.FindStars(star->mPosition, filter, closeStars);

	eastl::vector<eastl::pair<float, Simulator::cStarRecord*>> enemyStars;

	for (cStarRecordPtr closeStar : closeStars) {
		cEmpire* starEmpire = StarManager.GetEmpire(closeStar->mEmpireID);
		if (simulationValidator->ValidEmpire(starEmpire, true)
			&& DiplomacyUtils::War(empire.get(), starEmpire)
			&& AtackableStar(closeStar.get()))
		{
			float distance = StarUtils::GetDistanceBetweenStars(star, closeStar.get());
			enemyStars.push_back({ distance, closeStar.get() });
		}
	}

	eastl::sort(enemyStars.begin(), enemyStars.end(),
		[](const auto& a, const auto& b) {
			return a.first < b.first;
		});

	eastl::vector<Simulator::cStarRecord*> orderedStars;
	orderedStars.reserve(enemyStars.size());
	for (auto& pair : enemyStars) {
		orderedStars.push_back(pair.second);
	}
	return orderedStars;
}

void cEmpireWarfare::CalculateAttackPriorities() {
	cStarRecord* homeworld = EmpireUtils::GetHomeStar(empire.get());
	for (cStarRecordPtr star : empire->mStars) {
		if (star != nullptr) {
			eastl::vector<cStarRecord*> enemyStarsInRangeOfStar = GetEnemyStarsInRangeOfStar(star.get());
			if (!enemyStarsInRangeOfStar.empty()) {
				cStarRecord* closestStar = enemyStarsInRangeOfStar.front();
				float distToHome = (homeworld != nullptr) ? max(StarUtils::GetDistanceBetweenStars(homeworld, closestStar), 0.1f) : 1.0f;
				attackPriorityMap[closestStar] += 100.0f / distToHome;
				for (auto it = enemyStarsInRangeOfStar.begin() + 1; it != enemyStarsInRangeOfStar.end(); ++it) {
					cStarRecord* closeStar = *it;
					float subDist = (homeworld != nullptr) ? max(StarUtils::GetDistanceBetweenStars(homeworld, closeStar), 0.1f) : 1.0f;
					attackPriorityMap[closeStar] += 1.0f / subDist;
				}
			}
		}
	}
}

int cEmpireWarfare::GetEmpireBomberForceForPlanet(Simulator::cPlanetRecord* planet) {
	int requiredBombers = warfareStrengthAnalyzer->GetBomberForceForPlanet(this->empire.get(), planet);
	float multiAttackPenalty = warfareConfig->GetBombersMultiPlanetPenaltyFactor() * log(static_cast<float>(attackedPlanets) + 1.0f);
	requiredBombers += static_cast<int>(round(static_cast<float>(requiredBombers) * multiAttackPenalty));

	cStarRecord* homeworld = EmpireUtils::GetHomeStar(empire.get());
	if (homeworld != nullptr && planet->GetStarRecord() != nullptr) {
		float distanceToTarget = StarUtils::GetDistanceBetweenStars(homeworld, planet->GetStarRecord());
		float baseLogisticsRange = warfareConfig->GetWarfareRange(empire->GetWeaponryLevel());
		if (baseLogisticsRange <= 0.0f) {
			baseLogisticsRange = 10.0f;
		}
		if (distanceToTarget > baseLogisticsRange) {
			float logisticsStep = warfareConfig->GetBombersMultiPlanetPenaltyFactor() * 0.1f;
			float logisticsPenaltyRatio = (distanceToTarget - baseLogisticsRange) * logisticsStep;
			requiredBombers += static_cast<int>(round(static_cast<float>(requiredBombers) * logisticsPenaltyRatio));
		}
	}
	return max(requiredBombers, 1);
}

int cEmpireWarfare::GetEmpireBomberForceForSystem(Simulator::cStarRecord* star) {
	int requiredBombers = 0;
	for (cPlanetRecordPtr planet : star->GetPlanetRecords()) {
		if (planet->GetTechLevel() == TechLevel::Empire) {
			requiredBombers += GetEmpireBomberForceForPlanet(planet.get());
			attackedPlanets++;
		}
	}
	return requiredBombers;
}

void cEmpireWarfare::AttackStar(Simulator::cStarRecord* star, int bombers) {
	eastl::vector<eastl::pair<cPlanetRecordPtr, int>> bombersPerPlanet;
	int totalRequiredBombers = 0;
	for (cPlanetRecordPtr planet : star->GetPlanetRecords()) {
		if (simulationValidator->ValidPlanet(planet.get()) && planet->GetTechLevel() == TechLevel::Empire && !MissionManager.ThereIsEventInPlanet(planet.get())) {
			int planetRequiredBombers = warfareStrengthAnalyzer->GetBomberForceForPlanet(empire.get(), planet.get());
			totalRequiredBombers += planetRequiredBombers;
			bombersPerPlanet.push_back(eastl::make_pair(planet, planetRequiredBombers));
		}
	}

	if (bombersPerPlanet.size() > 0) {
		int extraBombersPerPlanet = max(0, bombers - totalRequiredBombers) / static_cast<int>(bombersPerPlanet.size());
		int surplusBombers = max(0, bombers - totalRequiredBombers) % static_cast<int>(bombersPerPlanet.size());
		for (auto& bomberForPlanet : bombersPerPlanet) {
			bomberForPlanet.second += extraBombersPerPlanet;
			if (surplusBombers > 0) {
				bomberForPlanet.second++;
				surplusBombers--;
			}
			warfareEventDispatcher->DispatchPlanetAttackedEvent(empire.get(), bomberForPlanet.first.get(), max(bomberForPlanet.second, 1));
		}
	}
}

void cEmpireWarfare::SelectAndAttackTargets() {
	if (!empire) return;

	float bombersPerCycle = warfareStrengthAnalyzer->GetBombersProducedByEmpire(empire.get());
	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);

	int baseUnitCost = max(static_cast<int>(round(1000.0f / max(warfareConfig->GetBombersPerRedSpiceFactory(), 0.1f))), 250);
	int bomberUnitCost = baseUnitCost;
	float taxMultiplier = 1.0f;
	if (baseArch == Archetypes::kArchetypeTrader) {
		taxMultiplier = 1.75f;
		bomberUnitCost = max(static_cast<int>(round(static_cast<float>(baseUnitCost) * 0.80f)), 200);
	}

	int numSystems = max(static_cast<int>(empire->mStars.size()), 1);
	int cycleTaxIncome = static_cast<int>(round(max(bombersPerCycle, 1.0f) * static_cast<float>(baseUnitCost) * 0.65f * taxMultiplier));

	int storageBonus = 0;
	cCompositionRoot* root = cCompositionRoot::Get();
	if (root && root->spaceCombatMetrics) {
		float gadgetMult = 1.0f;
		PropertyListPtr gProp;
		if (PropManager.GetPropertyList(id(u"GadgetConfig"), 0x02ae0c7e, gProp) && gProp != nullptr) {
			App::Property::GetFloat(gProp.get(), 0x07A1BC06, gadgetMult);
		}
		for (const auto& st : empire->mStars) {
			if (!st) continue;
			for (const auto& pl : st->GetPlanetRecords()) {
				if (pl && root->spaceCombatMetrics->GetPlanetGadgetInfo(pl.get()).hasSpiceStorage) {
					storageBonus += static_cast<int>(round(40000.0f * gadgetMult));
				}
			}
		}
	}

	int dynamicMaxTreasury = static_cast<int>(round(
		(50000.0f + static_cast<float>(numSystems) * 25000.0f + static_cast<float>(storageBonus) + static_cast<float>(cycleTaxIncome) * 10.0f) * taxMultiplier
	));
	dynamicMaxTreasury = min(max(dynamicMaxTreasury, 80000), 15000000);

	if (empire->mEmpireMoney < dynamicMaxTreasury) {
		empire->mEmpireMoney = min(empire->mEmpireMoney + cycleTaxIncome, dynamicMaxTreasury);
	}

	range = warfareConfig->GetWarfareRange(empire->GetWeaponryLevel());

	// Инвестиции богатых НПС в инфраструктуру своих планет и Посольства у игрока!
	InvestAiSurplusInColonyGadgets(empire.get(), baseArch, dynamicMaxTreasury, baseUnitCost, range);

	if (empire->mEnemies.empty()) return;

	int bankruptcyThreshold = max(static_cast<int>(round(static_cast<float>(dynamicMaxTreasury) * 0.15f)), baseUnitCost * 3);
	int minReserveMoney = bankruptcyThreshold;
	if (baseArch == Archetypes::kArchetypeWarrior || baseArch == Archetypes::kArchetypeZealot || baseArch == Archetypes::kArchetypeGrob) {
		minReserveMoney = static_cast<int>(round(static_cast<float>(bankruptcyThreshold) * 0.35f));
	} else if (baseArch == Archetypes::kArchetypeTrader || baseArch == Archetypes::kArchetypeScientist) {
		minReserveMoney = static_cast<int>(round(static_cast<float>(bankruptcyThreshold) * 1.25f));
	} else {
		minReserveMoney = static_cast<int>(round(static_cast<float>(bankruptcyThreshold) * 0.85f));
	}

	attackedPlanets = 0;
	attackPriorityMap.clear();
	CalculateAttackPriorities();

	float currentBombers = bombersPerCycle;

	while (currentBombers > 0.0f && !attackPriorityMap.empty()) {
		auto bestIt = attackPriorityMap.begin();
		float maxPriority = -1.0f;
		for (auto it = attackPriorityMap.begin(); it != attackPriorityMap.end(); ++it) {
			if (it->second > maxPriority) {
				maxPriority = it->second;
				bestIt = it;
			}
		}

		if (!simulationValidator->ValidStar(bestIt->first.get())) {
			attackPriorityMap.erase(bestIt);
			continue;
		}

		cStarRecordPtr bestTarget = bestIt->first;
		attackPriorityMap.erase(bestIt);

		int combatBombersInt = warfareStrengthAnalyzer->GetBomberForceForSystem(empire.get(), bestTarget.get());
		if (combatBombersInt <= 0) {
			continue;
		}

		float combatBombersForSystem = static_cast<float>(combatBombersInt);
		float logisticsCostForSystem = static_cast<float>(GetEmpireBomberForceForSystem(bestTarget.get()));

		if (logisticsCostForSystem > currentBombers && empire->mEmpireMoney > (minReserveMoney + bomberUnitCost)) {
			int missingBombers = static_cast<int>(ceil(logisticsCostForSystem - currentBombers));
			int spendableMoney = empire->mEmpireMoney - minReserveMoney;
			int affordableBombers = spendableMoney / bomberUnitCost;
			int purchasedBombers = min(missingBombers, affordableBombers);
			if (purchasedBombers > 0) {
				currentBombers += static_cast<float>(purchasedBombers);
				empire->mEmpireMoney -= purchasedBombers * bomberUnitCost;
			}
		}

		if (logisticsCostForSystem > currentBombers) {
			float pOfAttack = currentBombers / max(logisticsCostForSystem, 1.0f);
			if (pOfAttack > Math::randf()) {
				int scaledWave = max(static_cast<int>(round(combatBombersForSystem * pOfAttack)), 1);
				AttackStar(bestTarget.get(), scaledWave);
			}
			currentBombers = 0.0f;
		}
		else {
			currentBombers -= logisticsCostForSystem;
			if (attackPriorityMap.empty() && currentBombers > 0.0f) {
				int maxPlanetCap = warfareConfig->GetMaxBombersInRaid();
				int planetCount = 0;
				for (cPlanetRecordPtr p : bestTarget->GetPlanetRecords()) {
					if (p && p->GetTechLevel() == TechLevel::Empire) {
						planetCount++;
					}
				}
				int systemSoftCap = max(combatBombersInt, maxPlanetCap * max(planetCount, 1));
				int bonusWave = min(static_cast<int>(round(sqrt(currentBombers) * 1.5f)), max(0, systemSoftCap - combatBombersInt));
				AttackStar(bestTarget.get(), combatBombersInt + bonusWave);
				currentBombers = 0.0f;
			}
			else {
				AttackStar(bestTarget.get(), combatBombersInt);
			}
		}
	}
}
