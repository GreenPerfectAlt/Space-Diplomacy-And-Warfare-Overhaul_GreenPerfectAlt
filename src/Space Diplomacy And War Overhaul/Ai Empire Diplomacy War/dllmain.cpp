// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "Diplomacy\DebugDiplomacy.h"
#include <Spore/Simulator/SubSystem/CommManager.h>
#include <Spore/App/cMessageManager.h>
#include <Spore/App/IMessageManager.h>
#include "Diplomacy\cDiplomacySystem.h"
#include <Spore/Graphics/IModelWorld.h>
#include <Spore/App/cCameraManager.h>
#include <Spore/Swarm/cEffectsManager.h>
#include <Spore/Audio/AudioSystem.h>
#include <Spore/Simulator/cCity.h>
#include <Spore/Simulator/cBuilding.h>
#include <Spore/Simulator/cSpaceToolData.h>
#include <Spore/Simulator/SubSystem/ToolManager.h>
#include <Spore/CommonIDs.h>
#include <Spore-Mod-Utils/Include/SporeModUtils.h>
#include "Warfare/DebugWarfare.h"
#include "Warfare/cWarfareSystem.h"
#include "Warfare/cToolInvasionStrategy.h"
#include "Diplomacy/AffinityTextProc.h"
#include "cCompositionRoot.h"
#include "cPersistedEvent.h"
#include "Diplomacy/PersistedEvent/cPersistedDiplomacyEvent.h"
#include "Diplomacy/PersistedEvent/cNeighborsInPeaceEvent.h"
#include "Diplomacy/PersistedEvent/cMadePeaceEvent.h"
#include "Diplomacy/PersistedEvent/cUpliftedByMonolithEvent.h"
#include "Diplomacy/PersistedEvent/cFormedAllianceEvent.h"
#include "Diplomacy/PersistedEvent/cDefeatedEnemyTogetherEvent.h"
#include "Diplomacy/DiplomacyDetours.h"
#include "cPersistedObject.h"
#include "cPersistenceState.h"
#include "cDatabaseManager.h"
#include "Diplomacy/PersistedEvent/cPreparingToDeclareWarEvent.h"
#include <EASTL/map.h>
#include <EASTL/set.h>
#include <cmath>

using namespace SporeModUtils;
using namespace Simulator;

struct SpecialOrbitalShipVisual {
	float targetScale = 1.35f;
	IVisualEffectPtr auraFx = nullptr;
};

static bool sIsGeneratingInitialPlanetGarrison = false;
static eastl::map<cPlanetRecordPtr, uint32_t> sNpcPlanetUprisingCooldowns;
static eastl::map<uint32_t, uint32_t> sZealotEmpireSchismEndTime;
static int sGalaxyRevolutionTickAccumMs = 0;
static eastl::map<uint32_t, uint32_t> sZealotEmpireSchismEndTime;
static eastl::map<cPlanetRecordPtr, uint32_t> sNpcHangarJamCooldowns;
static eastl::set<cGameDataUFO*> sActiveTradeFreighters;
static eastl::map<cGameDataUFO*, SpecialOrbitalShipVisual> sScaledOrbitalShips;
static bool sRaiderRallyPanicActive = false;

static void ClearScaledOrbitalShips()
{
	for (auto& kv : sScaledOrbitalShips) {
		if (kv.second.auraFx != nullptr) {
			kv.second.auraFx->Stop(0);
		}
	}
	sScaledOrbitalShips.clear();
}

static void StartLoopingShipAura(cGameDataUFO* ufo, float scaleMult, IVisualEffectPtr& outFx, bool isBossDreadnought)
{
	outFx = nullptr;
	if (!ufo) return;

	float auraScale = scaleMult * 0.42f;
	if (EffectsManager.CreateVisualEffect(id("spc_rare_glow"), 0, outFx) && outFx != nullptr) {
		outFx->SetSourceTransform(Transform().SetOffset(ufo->GetPosition()).SetScale(auraScale));
		outFx->Start();
	}
}

static void RegisterScaledOrbitalShip(cGameDataUFO* ufo, float scaleMult, uint32_t primaryFxId, uint32_t fallbackFxId = 0)
{
	if (!ufo) return;
	SpecialOrbitalShipVisual vis;
	vis.targetScale = scaleMult;
	ufo->SetScale(scaleMult);

	bool isBoss = (scaleMult >= 1.85f);
	StartLoopingShipAura(ufo, scaleMult, vis.auraFx, isBoss);
	sScaledOrbitalShips[ufo] = vis;
}

static void ProcessBackgroundGalacticRevolutions(int deltaGameTime)
{
	if (!IsSpaceGame() || deltaGameTime <= 0) return;

	float rebMult = ReadExternalConfigFloat(u"RebellionConfig", 0x07A1BC02, 1.0f);
	float cdSec   = ReadExternalConfigFloat(u"RebellionConfig", 0x07A1BC10, 180.0f);
	int minStarsForSecession = max(static_cast<int>(round(ReadExternalConfigFloat(u"RebellionConfig", 0x07A1BC11, 6.0f))), 4);

	if (rebMult <= 0.01f || cdSec <= 1.0f) return;

	sGalaxyRevolutionTickAccumMs += deltaGameTime;
	int intervalMs = static_cast<int>(round(cdSec * 1000.0f));
	if (sGalaxyRevolutionTickAccumMs < intervalMs) return;
	sGalaxyRevolutionTickAccumMs = 0;

	cCompositionRoot* root = cCompositionRoot::Get();
	cEmpire* playerEmp = GetPlayerEmpire();
	if (!root || !root->spaceCombatMetrics || !playerEmp || playerEmp->mStars.empty()) return;

	// Ищем кризисные империи в окрестностях сектора игрока (до 45 парсеков — зона видимости и уведомлений!)
	StarRequestFilter filter;
	filter.RemoveStarType(Simulator::StarType::None);
	filter.RemoveStarType(Simulator::StarType::GalacticCore);
	filter.RemoveStarType(Simulator::StarType::ProtoPlanetary);
	filter.RemoveStarType(Simulator::StarType::BlackHole);
	filter.techLevels = 0;
	filter.AddTechLevel(Simulator::TechLevel::Empire);
	filter.minDistance = 0.5f;
	filter.maxDistance = 45.0f;

	eastl::vector<cStarRecordPtr> sectorStars;
	StarManager.FindStars(playerEmp->mStars.front()->mPosition, filter, sectorStars);

	eastl::vector<cEmpire*> candidates;
	for (const auto& st : sectorStars) {
		if (!st || st->mEmpireID == 0) continue;
		cEmpire* emp = StarManager.GetEmpire(st->mEmpireID);
		if (!emp || emp == playerEmp) continue;
		if (static_cast<int>(emp->mStars.size()) < minStarsForSecession) continue;
		if (root->simulationValidator && !root->simulationValidator->ValidEmpire(emp, true)) continue;

		Simulator::Archetypes bArch = ArchetypeUtils::GetBaseArchetype(emp->mArchetype);
		if (bArch == Archetypes::kArchetypeGrob) continue;

		bool exists = false;
		for (cEmpire* c : candidates) { if (c == emp) { exists = true; break; } }
		if (!exists) candidates.push_back(emp);
	}

	if (candidates.empty()) return;

	uint32_t nowTime = (root->persistenceState != nullptr) ? root->persistenceState->GetCurrentSpaceTime() : 0;

	for (cEmpire* targetEmp : candidates) {
		Simulator::Archetypes arch = ArchetypeUtils::GetBaseArchetype(targetEmp->mArchetype);
		int maxCap = GetDynamicMaxTreasuryForEmpire(targetEmp, root);
		int crisisThreshold = static_cast<int>(round(static_cast<float>(maxCap) * 0.15f));
		bool bankrupt = (targetEmp->mEmpireMoney > 0 && targetEmp->mEmpireMoney < crisisThreshold);

		bool inZealotSchism = false;
		if (arch == Archetypes::kArchetypeZealot && nowTime > 0) {
			auto itS = sZealotEmpireSchismEndTime.find(targetEmp->GetEmpireID());
			if (itS != sZealotEmpireSchismEndTime.end() && nowTime < itS->second) {
				inZealotSchism = true;
			}
		}

		if (arch == Archetypes::kArchetypeZealot && !inZealotSchism) continue;
		if (!bankrupt && !inZealotSchism && targetEmp->mEnemies.empty()) continue;

		float revChance = 0.18f * rebMult;
		if (inZealotSchism) revChance = 0.50f * rebMult;
		else if (arch == Archetypes::kArchetypeWarrior) revChance = 0.35f * rebMult;
		else if (arch == Archetypes::kArchetypeTrader && bankrupt) revChance = 0.30f * rebMult;

		if (Math::randf() >= min(revChance, 0.75f)) continue;

		// Выбираем самую далекую от столицы незащищенную колонию без Усилителя преданности
		cStarRecord* homeStar = EmpireUtils::GetHomeStar(targetEmp);
		cPlanetRecord* rebelPlanet = nullptr;
		float maxDist = -1.0f;

		for (const auto& st : targetEmp->mStars) {
			if (!st || st.get() == homeStar || root->spaceCombatMetrics->IsStarBlockaded(st.get())) continue;
			float d = homeStar ? StarUtils::GetDistanceBetweenStars(homeStar, st.get()) : 10.0f;
			for (const auto& pl : st->GetPlanetRecords()) {
				if (!pl || pl->GetTechLevel() != TechLevel::Empire || PlanetUtils::Homeworld(pl.get())) continue;
				if (root->spaceCombatMetrics->GetPlanetGadgetInfo(pl.get()).hasLoyaltyBooster) continue;
				if (d > maxDist) {
					maxDist = d;
					rebelPlanet = pl.get();
				}
			}
		}

		if (!rebelPlanet || !rebelPlanet->GetStarRecord()) continue;

		// 1. Запускаем настоящее ванильное событие налета/мятежа (шлет игроку мигающее SOS-уведомление от союзников и соседей!)
		cMission* raidMission = MissionManager.CreateMission(id("RaidPlunderEvent"), rebelPlanet, targetEmp);
		if (raidMission != nullptr) {
			raidMission->AcceptMission();
		}

		// 2. Если империя в глубоком банкротстве или Расколе — шанс реального откола окраинной системы на карте галактики!
		float secessionChance = (bankrupt || inZealotSchism) ? (0.30f * rebMult) : 0.0f;
		if (static_cast<int>(targetEmp->mStars.size()) >= minStarsForSecession && Math::randf() < secessionChance) {
			cStarRecord* rebelStar = rebelPlanet->GetStarRecord();
			// Ищем ближайшую молодую/соседнюю империю, к которой переметнутся сепаратисты (либо врага метрополии)
			eastl::vector<cStarRecordPtr> nearRebel;
			StarRequestFilter secFilter = filter;
			secFilter.maxDistance = 18.0f;
			StarManager.FindStars(rebelStar->mPosition, secFilter, nearRebel);

			cEmpire* newMaster = nullptr;
			for (const auto& ns : nearRebel) {
				if (!ns || ns->mEmpireID == 0 || ns->mEmpireID == targetEmp->GetEmpireID()) continue;
				cEmpire* cand = StarManager.GetEmpire(ns->mEmpireID);
				if (cand && cand != playerEmp && ArchetypeUtils::GetBaseArchetype(cand->mArchetype) != Archetypes::kArchetypeGrob) {
					newMaster = cand;
					if (RelationshipManager.IsAtWar(targetEmp, cand)) break;
				}
			}

			if (newMaster != nullptr && root->warfareSystem != nullptr) {
				StarManager.RequireEmpireForStar(rebelStar, newMaster);
				RelationshipManager.DeclareWar(targetEmp, newMaster);
			}
		}

		break; // Не более 1 крупной галактической революции за цикл, чтобы не спамить экран уведомлениями!
	}
}

class cOrbitalVisualsStrategy : public Simulator::cStrategy
{
public:
	static const uint32_t NOUN_ID = id("SpaceWarfareOverhaul::cOrbitalVisualsStrategy");

	int AddRef() override { return cStrategy::AddRef(); }
	int Release() override { return cStrategy::Release(); }
	void Initialize() override {}
	void Dispose() override { ClearScaledOrbitalShips(); }
	const char* GetName() const override { return "cOrbitalVisualsStrategy"; }
	bool Write(Simulator::ISerializerStream*) override { return true; }
	bool Read(Simulator::ISerializerStream*) override { return true; }
	bool WriteToXML(Simulator::XmlSerializer*) override { return true; }

	void Update(int deltaTime, int deltaGameTime) override
	{
		ProcessBackgroundGalacticRevolutions(deltaGameTime);
		if (!IsSpaceGame() || GetCurrentContext() != SpaceContext::Planet) {
			if (!sScaledOrbitalShips.empty()) {
				ClearScaledOrbitalShips();
			}
			return;
		}

		if (sScaledOrbitalShips.empty()) return;

		eastl::set<cGameDataUFO*> liveUfos;
		for (const auto& ufo : GetData<cGameDataUFO>()) {
			if (ufo != nullptr && ufo->mHealthPoints > 0.0f) {
				liveUfos.insert(ufo.get());
			}
		}

		for (auto it = sScaledOrbitalShips.begin(); it != sScaledOrbitalShips.end(); ) {
			cGameDataUFO* ship = it->first;
			if (liveUfos.find(ship) == liveUfos.end()) {
				if (it->second.auraFx != nullptr) {
					it->second.auraFx->Stop(0);
				}
				it = sScaledOrbitalShips.erase(it);
				continue;
			}

			float visualScale = it->second.targetScale * 1.45f;
			ship->SetScale(visualScale);

			if (ship->mpModel != nullptr) {
				Transform modelTr = ship->mpModel->GetTransform();
				modelTr.SetScale(visualScale);
				ship->mpModel->SetTransform(modelTr);
			}

			if (it->second.auraFx != nullptr) {
				float auraSc = it->second.targetScale * 0.42f;
				it->second.auraFx->SetSourceTransform(
					Transform().SetOffset(ship->GetPosition()).SetScale(auraSc));
			}
			++it;
		}
	}
};


static float ReadExternalConfigFloat(const char16_t* propName, uint32_t propID, float defaultVal)
{
	float val = defaultVal;
	PropertyListPtr propList;
	if (PropManager.GetPropertyList(id(propName), 0x02ae0c7e, propList) && propList != nullptr) {
		App::Property::GetFloat(propList.get(), propID, val);
	}
	return val;
}

static float ReadWarfareConfigFloat(uint32_t propID, float defaultVal)
{
	PropertyListPtr configKeys;
	if (PropManager.GetPropertyList(id("ConfigurationKeys"), id("SdoConfig"), configKeys) && configKeys != nullptr) {
		ResourceKey warfareKey;
		if (App::Property::GetKey(configKeys.get(), 0x0FCF340F, warfareKey)) {
			PropertyListPtr warfareProp;
			if (PropManager.GetPropertyList(warfareKey.instanceID, warfareKey.groupID, warfareProp) && warfareProp != nullptr) {
				float val = defaultVal;
				if (App::Property::GetFloat(warfareProp.get(), propID, val)) {
					return val;
				}
			}
		}
	}
	return defaultVal;
}

// Масштабирование 3D-меша сразу после тика камеры (перед рендером, с обновлением GPU через UpdateModel!)
member_detour(CameraUpdatePreRender__detour, App::cCameraManager, void(int))
{
	void detoured(int deltaTime)
	{
		original_function(this, deltaTime);

		if (sScaledOrbitalShips.empty() || !IsSpaceGame() || GetCurrentContext() != SpaceContext::Planet) {
			return;
		}

		for (auto& kv : sScaledOrbitalShips) {
			cGameDataUFO* ship = kv.first;
			if (!ship || ship->mHealthPoints <= 0.0f || ship->mpModel == nullptr) continue;

			float visualScale = kv.second.targetScale * 1.35f;
			Transform tr = ship->mpModel->GetTransform();
			tr.SetScale(visualScale);
			ship->mpModel->SetTransform(tr);
			
			if (kv.second.auraFx != nullptr) {
				float auraSc = (kv.second.targetScale >= 1.85f) ? 0.28f : 0.14f;
				kv.second.auraFx->SetSourceTransform(
					Transform().SetOffset(ship->GetPosition()).SetScale(auraSc));
			}
		}
	}
};



// Слушатель грабежа пряности при сбитии каравана
class cTradeConvoyLootListener : public App::IMessageListener, public DefaultRefCounted
{
public:
	int AddRef() override { return DefaultRefCounted::AddRef(); }
	int Release() override { return DefaultRefCounted::Release(); }

	bool HandleMessage(uint32_t messageID, void* msg) override
	{
		if (!IsSpaceGame() || messageID != SimulatorMessages::kMsgCombatantKilled || !msg) return false;

		CombatantKilledMessage* killMsg = static_cast<CombatantKilledMessage*>(msg);
		cGameDataUFO* deadUfo = object_cast<cGameDataUFO>(killMsg->GetCombatant());
		if (!deadUfo) return false;

		auto itVis = sScaledOrbitalShips.find(deadUfo);
		if (itVis != sScaledOrbitalShips.end()) {
			if (itVis->second.auraFx != nullptr) {
				itVis->second.auraFx->Stop(0);
			}
			sScaledOrbitalShips.erase(itVis);
		}

		auto it = sActiveTradeFreighters.find(deadUfo);
		if (it != sActiveTradeFreighters.end()) {
			sActiveTradeFreighters.erase(it);

			cCompositionRoot* root = cCompositionRoot::Get();
			cPlanetRecord* activePlanet = GetActivePlanetRecord();
			cEmpire* playerEmp = GetPlayerEmpire();

			if (root && root->spaceCombatMetrics && activePlanet && activePlanet->GetStarRecord() && playerEmp) {
				float redRate = max(ReadWarfareConfigFloat(0xD07B1F69, 1.0f), 0.1f);
				int unitCost = max(static_cast<int>(round(1000.0f / redRate)), 250);
				float spiceMult = root->spaceCombatMetrics->GetNormalizedStarSpiceRatio(activePlanet->GetStarRecord());
				float tradeMult = root->spaceCombatMetrics->GetTradeConfig().incomeMultiplier;

				int lootAmount = max(static_cast<int>(round(static_cast<float>(unitCost * 6) * spiceMult * tradeMult)), 1500);
				cEmpire* cargoOwner = StarManager.GetEmpire(deadUfo->mPoliticalID);

				if (cargoOwner && cargoOwner != playerEmp && RelationshipManager.IsAtWar(cargoOwner, playerEmp)) {
					playerEmp->mEmpireMoney += lootAmount;
					cargoOwner->mEmpireMoney = max(cargoOwner->mEmpireMoney - lootAmount, 0);

					Audio::PlayAudio(id("ui_space_money_gain"));
					Audio::PlayAudio(id("spc_trade_sell"));

					IVisualEffectPtr lootFx;
					if (EffectsManager.CreateVisualEffect(id("spc_collect_rare"), 0, lootFx) && lootFx != nullptr) {
						lootFx->SetSourceTransform(Transform().SetOffset(deadUfo->GetPosition()));
						lootFx->Start();
					}
				} else if (cargoOwner) {
					cargoOwner->mEmpireMoney = max(cargoOwner->mEmpireMoney - lootAmount, 0);
				}
			}
		}
		return false;
	}
};

static intrusive_ptr<cTradeConvoyLootListener> sConvoyLootListener;


void Initialize()
{
	cSimulatorSystem::Get()->AddStrategy(new cCompositionRoot(), cCompositionRoot::NOUN_ID);
	cSimulatorSystem::Get()->AddStrategy(new cDiplomacySystem(), cDiplomacySystem::NOUN_ID);
	cSimulatorSystem::Get()->AddStrategy(new cWarfareSystem(), cWarfareSystem::NOUN_ID);
	cSimulatorSystem::Get()->AddStrategy(new cDatabaseManager(), cDatabaseManager::NOUN_ID);
	ToolManager.AddStrategy(new cToolInvasionStrategy(), cToolInvasionStrategy::TYPE);

	ClassManager.AddFactory(new cPersistedObjectFactory());
	ClassManager.AddFactory(new cPersistenceStateFactory());
	ClassManager.AddFactory(new cPersistedEventFactory());
	ClassManager.AddFactory(new cPersistedDiplomacyEventFactory());
	ClassManager.AddFactory(new cNeighborsInPeaceEventFactory());
	ClassManager.AddFactory(new cMadePeaceEventFactory());
	ClassManager.AddFactory(new cUpliftedByMonolithEventFactory());
	ClassManager.AddFactory(new cFormedAllianceEventFactory());
	ClassManager.AddFactory(new cDefeatedEnemyTogetherEventFactory());
	ClassManager.AddFactory(new cPreparingForWarEventFactory());

	cSimulatorSystem::Get()->AddStrategy(new cOrbitalVisualsStrategy(), cOrbitalVisualsStrategy::NOUN_ID);

	sConvoyLootListener = new cTradeConvoyLootListener();
	MessageManager.AddListener(sConvoyLootListener.get(), SimulatorMessages::kMsgCombatantKilled);
}

void Dispose()
{
	ClearScaledOrbitalShips();
	if (sConvoyLootListener != nullptr) {
		MessageManager.RemoveListener(sConvoyLootListener.get(), SimulatorMessages::kMsgCombatantKilled);
		sConvoyLootListener = nullptr;
	}
}


member_detour(ShouldSpawnRaiderUFO__detour, cRaidEvent, bool())
{
	bool detoured()
	{
		bool ret = original_function(this);
		return false;
	}
};

// Разрыв контрактов и финансовый шок обеим сторонам при объявлении войны
member_detour(TradeWarBreak__detour, cRelationshipManager, void(cEmpire*, cEmpire*))
{
	void detoured(cEmpire* empire1, cEmpire* empire2)
	{
		cCompositionRoot* root = cCompositionRoot::Get();
		if (IsSpaceGame() && root && root->spaceCombatMetrics && empire1 && empire2) {
			float redRate = max(ReadWarfareConfigFloat(0xD07B1F69, 1.0f), 0.1f);
			int unitCost = max(static_cast<int>(round(1000.0f / redRate)), 250);
			root->spaceCombatMetrics->BreakTradeRoutesOnWar(empire1, empire2, unitCost);
		}
		original_function(this, empire1, empire2);
	}
};

static bool MatchNpcToolKey(uint32_t idVal, const char* n1, const char* n2, const char* n3 = nullptr, uint32_t raw1 = 0, uint32_t raw2 = 0)
{
	if (idVal == 0) return false;
	if (n1 && idVal == id(n1)) return true;
	if (n2 && idVal == id(n2)) return true;
	if (n3 && idVal == id(n3)) return true;
	if (raw1 != 0 && idVal == raw1) return true;
	if (raw2 != 0 && idVal == raw2) return true;
	return false;
}

static float ReadWarfareRangeForEmpire(cEmpire* empire, float defaultRange)
{
	if (!empire) return defaultRange;
	PropertyListPtr configKeys;
	if (PropManager.GetPropertyList(id("ConfigurationKeys"), id("SdoConfig"), configKeys) && configKeys != nullptr) {
		ResourceKey warfareKey;
		if (App::Property::GetKey(configKeys.get(), 0x0FCF340F, warfareKey)) {
			PropertyListPtr warfareProp;
			if (PropManager.GetPropertyList(warfareKey.instanceID, warfareKey.groupID, warfareProp) && warfareProp != nullptr) {
				eastl::vector<float> ranges;
				if (App::Property::GetArrayFloat(warfareProp.get(), 0x4BC711F0, ranges) && !ranges.empty()) {
					int lvl = empire->GetWeaponryLevel();
					if (lvl < 0) return ranges.front();
					if (lvl >= static_cast<int>(ranges.size())) return ranges.back();
					return ranges[lvl];
				}
			}
		}
	}
	return defaultRange;
}

static int GetDynamicMaxTreasuryForEmpire(cEmpire* empire, cCompositionRoot* root)
{
	if (!empire) return 150000;
	float produced = 2.0f;
	if (root && root->warfareStrengthAnalyzer) {
		produced = max(root->warfareStrengthAnalyzer->GetBombersProducedByEmpire(empire), 1.0f);
	}
	float redSpiceRate = max(ReadWarfareConfigFloat(0xD07B1F69, 1.0f), 0.1f);
	int unitCost = max(static_cast<int>(round(1000.0f / redSpiceRate)), 250);

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(empire->mArchetype);
	float tradeMult = (baseArch == Archetypes::kArchetypeTrader) ? 1.75f : 1.0f;

	int storageBonus = 0;
	int routeBonus = 0;
	if (root && root->spaceCombatMetrics) {
		float gadgetMult = ReadExternalConfigFloat(u"GadgetConfig", 0x07A1BC06, 1.0f);
		for (const auto& star : empire->mStars) {
			if (!star) continue;
			for (const auto& pl : star->GetPlanetRecords()) {
				if (pl && root->spaceCombatMetrics->GetPlanetGadgetInfo(pl.get()).hasSpiceStorage) {
					storageBonus += static_cast<int>(round(40000.0f * gadgetMult));
				}
			}
		}
		routeBonus = root->spaceCombatMetrics->GetEmpireTotalTradeRoutes(empire) * unitCost * 8;
	}

	int cycleIncome = max(static_cast<int>(round(produced * static_cast<float>(unitCost) * 0.45f * tradeMult)), 2500);
	int starBaseCap = 50000 + static_cast<int>(empire->mStars.size()) * 25000 + storageBonus + routeBonus;
	int maxCap = static_cast<int>(round(static_cast<float>(starBaseCap + cycleIncome * 10) * tradeMult));
	return max(maxCap, 80000);
}

static float GetPlanetNormalizedSpiceRatio(cPlanetRecord* planet)
{
	if (!planet || planet->mSpiceGen.instanceID == 0) return 1.0f;
	float refRed = 1.0f;
	PropertyListPtr redProp;
	if (PropManager.GetPropertyList(0x0289B83D, 0x034D97FA, redProp) && redProp != nullptr) {
		App::Property::GetFloat(redProp.get(), 0x00B2CCCB, refRed);
	}
	refRed = max(refRed, 1.0f);

	PropertyListPtr spiceProp;
	if (PropManager.GetPropertyList(planet->mSpiceGen.instanceID, 0x034D97FA, spiceProp) && spiceProp != nullptr) {
		float baseCost = 0.0f;
		if (App::Property::GetFloat(spiceProp.get(), 0x00B2CCCB, baseCost) && baseCost > 0.0f) {
			return max(baseCost / refRed, 0.5f);
		}
	}
	return 1.0f;
}

static float GetNpcPlanetaryCityUnrestFactor(cPlanetRecord* planet)
{
	if (!planet) return 0.0f;
	float unrestMult = ReadExternalConfigFloat(u"UnrestConfig", 0x07A1BC04, 1.0f);
	if (unrestMult <= 0.01f) return 0.0f;
	if (GetCurrentContext() != SpaceContext::Planet || planet != GetActivePlanetRecord()) {
		return 0.0f;
	}

	cCompositionRoot* root = cCompositionRoot::Get();
	SdoPlanetGadgetInfo gInfo;
	if (root && root->spaceCombatMetrics) {
		gInfo = root->spaceCombatMetrics->GetPlanetGadgetInfo(planet);
		if (gInfo.hasHappinessBooster) {
			return 0.0f;
		}
	}

	const auto& cities = GetData<cCity>();
	int totalCities = 0;
	int unhappyCitizens = 0;
	int totalFactories = 0;
	int totalEntertainment = 0;

	for (const auto& city : cities) {
		if (!city) continue;
		totalCities++;
		unhappyCitizens += max(static_cast<int>(city->mUnhappyCount), 0);
		for (const auto& bld : city->mBuildings) {
			if (!bld) continue;
			uint32_t noun = bld->GetNounID();
			if (noun == Simulator::GameNounIDs::kBuildingIndustry) {
				totalFactories++;
			} else if (noun == Simulator::GameNounIDs::kBuildingEntertainment) {
				totalEntertainment++;
			}
		}
	}

	if (totalCities <= 0) return 0.0f;

	float avgUnhappy = static_cast<float>(unhappyCitizens) / static_cast<float>(totalCities);
	float unhappyScore = min(avgUnhappy * 0.22f, 0.85f);

	cEmpire* planetOwner = (planet->GetStarRecord() != nullptr) ? StarManager.GetEmpire(planet->GetStarRecord()->mEmpireID) : nullptr;
	Simulator::Archetypes ownerArch = planetOwner ? ArchetypeUtils::GetBaseArchetype(planetOwner->mArchetype) : Archetypes::kArchetypeWanderer;

	bool underWarOrBlockade = false;
	if (planetOwner) {
		if (!planetOwner->mEnemies.empty()) underWarOrBlockade = true;
		if (root && root->spaceCombatMetrics && planet->GetStarRecord() && root->spaceCombatMetrics->IsStarBlockaded(planet->GetStarRecord())) {
			underWarOrBlockade = true;
		}
	}

	float factoryStrain = 0.0f;
	int allowedFactories = (ownerArch == Archetypes::kArchetypeEcologist && underWarOrBlockade)
		? (totalEntertainment + totalCities)
		: (totalEntertainment * 2 + totalCities);

	if (totalFactories > allowedFactories) {
		int excessFactories = totalFactories - allowedFactories;
		float archFactorySensitivity = 1.0f;
		// В мирное время не душим ИИ за автозастройку Maxis, а при войне/блокаде Экологи (2.5x) и Ученые (2.0x) бастуют жестко!
		if (underWarOrBlockade) {
			if (ownerArch == Archetypes::kArchetypeEcologist) archFactorySensitivity = 2.5f;
			else if (ownerArch == Archetypes::kArchetypeScientist) archFactorySensitivity = 2.0f;
		}
		factoryStrain = min(static_cast<float>(excessFactories) * 0.06f * archFactorySensitivity, 0.75f);
	}

	float rawUnrest = min(unhappyScore + factoryStrain, 1.0f) * unrestMult;

	if (root && root->spaceCombatMetrics && planet->GetStarRecord()) {
		bool isBlockaded = false;
		cEmpire* tradePartner = root->spaceCombatMetrics->GetStarActiveTradePartner(planet->GetStarRecord(), isBlockaded);
		if (tradePartner != nullptr) {
			if (isBlockaded) {
				rawUnrest += 0.25f * unrestMult;
			} else {
				rawUnrest *= 0.75f;
			}
		}
	}

	if (gInfo.hasBioProtector && rawUnrest > 0.0f) {
		float gadgetMult = ReadExternalConfigFloat(u"GadgetConfig", 0x07A1BC06, 1.0f);
		rawUnrest *= max(0.20f, 1.0f - 0.30f * gadgetMult);
	}
	return min(rawUnrest, 1.5f);
}

static bool IsNpcPlanetInCombat(cPlanetRecord* planet, cEmpire* ownerEmpire)
{
	if (!planet || !ownerEmpire) return false;
	cEmpire* playerEmp = GetPlayerEmpire();
	if (playerEmp && RelationshipManager.IsAtWar(ownerEmpire, playerEmp)) {
		return true;
	}
	const auto& ufos = GetData<cGameDataUFO>();
	for (const auto& ufo : ufos) {
		if (ufo && ufo->mHealthPoints > 0 && ufo->mPoliticalID != 0 && ufo->mPoliticalID != ownerEmpire->GetEmpireID()) {
			cEmpire* otherEmp = StarManager.GetEmpire(ufo->mPoliticalID);
			if (!otherEmp) {
				return true;
			}
			if (RelationshipManager.IsAtWar(ownerEmpire, otherEmp)) {
				return true;
			}
		}
	}
	return false;
}

static float ComputeNpcPlanetRebellionChance(cPlanetRecord* activePlanet, cEmpire* ownerEmpire, cCompositionRoot* root, bool peacetimeCheck, cGameDataUFO* newlySpawnedUfo)
{
	if (!activePlanet || !ownerEmpire || !root || !root->spaceCombatMetrics) {
		return 0.0f;
	}
	float rebellionMultiplier = ReadExternalConfigFloat(u"RebellionConfig", 0x07A1BC02, 1.0f);
	if (rebellionMultiplier <= 0.01f) {
		return 0.0f;
	}

	Simulator::Archetypes baseArch = ArchetypeUtils::GetBaseArchetype(ownerEmpire->mArchetype);
	if (baseArch == Archetypes::kArchetypeGrob) {
		return 0.0f;
	}

	float upkeepMult = ReadExternalConfigFloat(u"UpkeepConfig", 0x07A1BC03, 1.0f);
	int maxCap = GetDynamicMaxTreasuryForEmpire(ownerEmpire, root);
	int crisisThreshold = static_cast<int>(round(static_cast<float>(maxCap) * 0.15f));
	bool isBankrupt = (upkeepMult > 0.01f) && (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold);

	float cityUnrest = GetNpcPlanetaryCityUnrestFactor(activePlanet);
	bool severeCityStrike = (cityUnrest >= 0.45f);
	SdoPlanetGadgetInfo earlyGadgets = root->spaceCombatMetrics->GetPlanetGadgetInfo(activePlanet);

	// Логика Великого Раскола Фанатиков: 0% в норме, но если подорвана догма (Посольство, стачки или паника) — каскад бунтов по всей империи!
	bool zealotSchismActive = false;
	if (baseArch == Archetypes::kArchetypeZealot) {
		uint32_t nowSpaceTime = (root->persistenceState != nullptr) ? root->persistenceState->GetCurrentSpaceTime() : 0;
		uint32_t empID = ownerEmpire->GetEmpireID();

		bool dogmaShatteredHere = severeCityStrike || sRaiderRallyPanicActive ||
			(!peacetimeCheck && earlyGadgets.hasEmbassy) ||
			(isBankrupt && cityUnrest >= 0.25f);

		if (dogmaShatteredHere && nowSpaceTime > 0) {
			float xmlSchismSec = ReadExternalConfigFloat(u"RebellionConfig", 0x07A1BC0F, 240.0f);
			float sizeScale = 1.0f + min(static_cast<float>(ownerEmpire->mStars.size()) * 0.04f, 0.80f);
			uint32_t schismDuration = static_cast<uint32_t>(round(xmlSchismSec * 1000.0f * sizeScale));
			sZealotEmpireSchismEndTime[empID] = nowSpaceTime + schismDuration;
			zealotSchismActive = true;
		} else if (nowSpaceTime > 0) {
			auto itSchism = sZealotEmpireSchismEndTime.find(empID);
			if (itSchism != sZealotEmpireSchismEndTime.end() && nowSpaceTime < itSchism->second) {
				zealotSchismActive = true;
			}
		} else if (dogmaShatteredHere) {
			zealotSchismActive = true;
		}

		if (!zealotSchismActive) {
			return 0.0f; // Монолитная вера держится — бунтов 0%!
		}
	}

	bool heavyLosses = false;
	if (!peacetimeCheck) {
		int targetDefenders = min(root->spaceCombatMetrics->GetDefendersInPlanet(activePlanet), 24);
		int aliveDefenders = 0;
		const auto& ufos = GetData<cGameDataUFO>();
		for (const auto& ufo : ufos) {
			if (ufo && ufo.get() != newlySpawnedUfo && ufo->mHealthPoints > 0 &&
				ufo->mPoliticalID == ownerEmpire->GetEmpireID() &&
				ufo->mUFOType == static_cast<int>(UfoType::Defender)) {
				aliveDefenders++;
			}
		}
		heavyLosses = (targetDefenders <= 2) ? (aliveDefenders == 0) : (aliveDefenders < (targetDefenders / 2));
	}

	if (zealotSchismActive) rebellionChance = 0.55f;
	else if (baseArch == Archetypes::kArchetypeWarrior) rebellionChance = 0.50f;
	else if (baseArch == Archetypes::kArchetypeTrader) rebellionChance = 0.40f;
	else if ((baseArch == Archetypes::kArchetypeEcologist || baseArch == Archetypes::kArchetypeScientist) && severeCityStrike) {
		rebellionChance = 0.42f;
	}
	
	if (!zealotSchismActive) {
		if (peacetimeCheck) {
			if (!isBankrupt && !severeCityStrike) return 0.0f;
			if (PlanetUtils::Homeworld(activePlanet) && !severeCityStrike) return 0.0f;
		} else {
			if (!isBankrupt && !heavyLosses && !severeCityStrike) return 0.0f;
		}
	}

	float rebellionChance = 0.25f;
	if (zealotSchismActive) rebellionChance = 0.55f; // Цепной каскад религиозного раскола по всем планетам секты!
	else if (baseArch == Archetypes::kArchetypeWarrior) rebellionChance = 0.50f;
	else if (baseArch == Archetypes::kArchetypeTrader) rebellionChance = 0.40f;
	else if (baseArch == Archetypes::kArchetypeScientist || baseArch == Archetypes::kArchetypeEcologist) rebellionChance = 0.15f;
	else if (baseArch == Archetypes::kArchetypeDiplomat || baseArch == Archetypes::kArchetypeBard || baseArch == Archetypes::kArchetypeShaman) rebellionChance = 0.20f;

	if (severeCityStrike) {
		rebellionChance += cityUnrest * 0.20f;
	}

	float planetTierFactor = 1.0f;
	if (PlanetUtils::Homeworld(activePlanet)) planetTierFactor = 0.20f;
	else if (activePlanet->mType == PlanetType::T0) planetTierFactor = 1.55f;
	else if (activePlanet->mType == PlanetType::T1) planetTierFactor = 1.25f;
	else if (activePlanet->mType == PlanetType::T3) planetTierFactor = 0.60f;

	float spiceFactor = 1.0f;
	float spiceRatio = GetPlanetNormalizedSpiceRatio(activePlanet);
	if (spiceRatio >= 5.0f) spiceFactor = 0.65f;
	else if (spiceRatio >= 2.5f) spiceFactor = 0.75f;
	else if (spiceRatio <= 1.2f) spiceFactor = 1.15f;

	int cityCount = 1;
	int turretCount = 0;
	const auto& cities = GetData<cCity>();
	int loadedCities = 0;
	for (const auto& city : cities) {
		if (city) {
			loadedCities++;
			turretCount += static_cast<int>(city->mTurrets.size());
		}
	}
	if (loadedCities > 0) cityCount = loadedCities;

	float infrastructureFactor = (cityCount <= 1) ? 1.15f : max(1.0f - static_cast<float>(cityCount - 1) * 0.08f, 0.50f);
	if (turretCount >= 12) infrastructureFactor *= 0.85f;

	float distanceFactor = 1.0f;
	cStarRecord* homeStar = EmpireUtils::GetHomeStar(ownerEmpire);
	cStarRecord* currentStar = activePlanet->GetStarRecord();
	if (homeStar && currentStar && homeStar != currentStar) {
		float dist = StarUtils::GetDistanceBetweenStars(homeStar, currentStar);
		float coreRadius = ReadWarfareRangeForEmpire(ownerEmpire, 12.0f);
		if (coreRadius <= 0.0f) coreRadius = 12.0f;

		if (baseArch == Archetypes::kArchetypeWarrior || baseArch == Archetypes::kArchetypeTrader) {
			if (dist > coreRadius) distanceFactor = min(1.0f + (dist - coreRadius) * 0.03f, 1.60f);
		} else if (baseArch == Archetypes::kArchetypeDiplomat || baseArch == Archetypes::kArchetypeEcologist ||
		           baseArch == Archetypes::kArchetypeBard || baseArch == Archetypes::kArchetypeShaman) {
			if (dist > coreRadius * 0.8f) distanceFactor = max(1.0f - (dist - coreRadius * 0.8f) * 0.02f, 0.65f);
		} else if (baseArch == Archetypes::kArchetypeScientist) {
			float relayRange = max(coreRadius * 1.8f, 25.0f);
			if (dist > relayRange) distanceFactor = min(1.0f + (dist - relayRange) * 0.015f, 1.20f);
		}
	}

	SdoPlanetGadgetInfo gadgets = root->spaceCombatMetrics->GetPlanetGadgetInfo(activePlanet);
	float gadgetMult = ReadExternalConfigFloat(u"GadgetConfig", 0x07A1BC06, 1.0f);

	if (gadgets.hasUberTurret) {
		infrastructureFactor *= 0.80f;
	}

	float subversionFactor = 1.0f;
	if (!peacetimeCheck && root->empireRelationsAnalyzer && GetPlayerEmpire() && ownerEmpire != GetPlayerEmpire()) {
		int affinityWithPlayer = root->empireRelationsAnalyzer->EmpiresAffinity(ownerEmpire, GetPlayerEmpire())
		                       + root->spaceCombatMetrics->GetTradeAffinityBonus(ownerEmpire, GetPlayerEmpire());
		if (affinityWithPlayer >= 2) subversionFactor = 1.25f;
		else if (affinityWithPlayer == 1) subversionFactor = 1.10f;
		else if (affinityWithPlayer <= -2) subversionFactor = 0.85f;

		if (gadgets.hasEmbassy) {
			subversionFactor *= (1.0f + 0.25f * gadgetMult);
		}
	} else if (peacetimeCheck && gadgets.hasEmbassy) {
		subversionFactor *= 0.50f;
	}

	if (!peacetimeCheck && sRaiderRallyPanicActive) {
		subversionFactor *= 1.35f;
	}

	float finalChance = rebellionChance * planetTierFactor * spiceFactor * infrastructureFactor * distanceFactor * subversionFactor * rebellionMultiplier;
	if (gadgets.hasLoyaltyBooster) {
		float suppression = min(0.90f * gadgetMult, 0.98f);
		finalChance *= (1.0f - suppression);
	}
	if (peacetimeCheck) {
		finalChance *= 0.70f;
	}
	return min(finalChance, 0.90f);
}

static bool ShouldTriggerNpcRebellion(cEmpire* ownerEmpire, cCompositionRoot* root, cGameDataUFO* newlySpawnedUfo)
{
	if (GetCurrentContext() != SpaceContext::Planet) return false;
	cPlanetRecord* activePlanet = GetActivePlanetRecord();
	if (!activePlanet || !activePlanet->GetStarRecord() ||
	    activePlanet->GetStarRecord()->mEmpireID != ownerEmpire->GetEmpireID()) {
		return false;
	}
	if (!IsNpcPlanetInCombat(activePlanet, ownerEmpire)) {
		return false;
	}
	float chance = ComputeNpcPlanetRebellionChance(activePlanet, ownerEmpire, root, false, newlySpawnedUfo);
	if (chance <= 0.001f) return false;
	return (Math::randf() < chance);
}

static void TryTriggerNpcPeacetimeColonialUprising(cPlanetRecord* planet, cEmpire* planetEmpire, cCompositionRoot* root)
{
	if (!planet || !planetEmpire || !root || !root->spaceCombatMetrics) return;
	if (IsNpcPlanetInCombat(planet, planetEmpire)) return;
	if (root->spaceCombatMetrics->GetPlanetGadgetInfo(planet).hasLoyaltyBooster) return;

	uint32_t currentSpaceTime = (root->persistenceState != nullptr) ? root->persistenceState->GetCurrentSpaceTime() : 0;
	auto itCd = sNpcPlanetUprisingCooldowns.find(planet);
	if (itCd != sNpcPlanetUprisingCooldowns.end() && currentSpaceTime > 0 && currentSpaceTime < itCd->second) {
		return;
	}

	float chance = ComputeNpcPlanetRebellionChance(planet, planetEmpire, root, true, nullptr);
	if (chance <= 0.001f) return;

	if (Math::randf() >= chance) {
		if (currentSpaceTime > 0) {
			uint32_t baseCdMs = static_cast<uint32_t>(round(max(ReadExternalConfigFloat(u"RebellionConfig", 0x07A1BC10, 180.0f), 30.0f) * 500.0f));
			sNpcPlanetUprisingCooldowns[cPlanetRecordPtr(planet)] = currentSpaceTime + baseCdMs;	
		}
		return;
	}

	eastl::vector<cGameDataUFO*> loyalDefenders;
	const auto& ufos = GetData<cGameDataUFO>();
	for (const auto& ufo : ufos) {
		if (ufo && ufo->mHealthPoints > 0 &&
		    ufo->mPoliticalID == planetEmpire->GetEmpireID() &&
		    ufo->mUFOType == static_cast<int>(UfoType::Defender) &&
		    sActiveTradeFreighters.find(ufo.get()) == sActiveTradeFreighters.end()) {
			loyalDefenders.push_back(ufo.get());
		}
	}

	if (loyalDefenders.size() < 2) return;

	float rebelFraction = min(0.28f + GetNpcPlanetaryCityUnrestFactor(planet) * 0.20f, 0.45f);
	int rebelCount = max(static_cast<int>(round(static_cast<float>(loyalDefenders.size()) * rebelFraction)), 1);
	rebelCount = min(rebelCount, static_cast<int>(loyalDefenders.size()) - 1);

	for (int i = 0; i < rebelCount; ++i) {
		cGameDataUFO* traitor = loyalDefenders[i];
		traitor->mPoliticalID = 0xFFFFFFFF;
		traitor->mUFOType = static_cast<int>(UfoType::Bomber);
	}

	if (currentSpaceTime > 0) {
		uint32_t fullCdMs = static_cast<uint32_t>(round(max(ReadExternalConfigFloat(u"RebellionConfig", 0x07A1BC10, 180.0f), 30.0f) * 1300.0f));
		sNpcPlanetUprisingCooldowns[cPlanetRecordPtr(planet)] = currentSpaceTime + fullCdMs;
	}
}

static_detour(SpawnUFO__detour, cGameDataUFO*(UfoType, int*, ResourceKey*))
{
	cGameDataUFO* detoured(UfoType ufoType, int* pPoliticalID, ResourceKey* pUfoKey)
	{
		cGameDataUFO* ufo = original_function(ufoType, pPoliticalID, pUfoKey);
		if (!ufo || !IsSpaceGame() || !pPoliticalID || *pPoliticalID == 0) {
			return ufo;
		}
		cCompositionRoot* root = cCompositionRoot::Get();
		if (!root || !root->spaceCombatMetrics) {
			return ufo;
		}

		float gadgetMult = ReadExternalConfigFloat(u"GadgetConfig", 0x07A1BC06, 1.0f);
		uint32_t politicalID = static_cast<uint32_t>(*pPoliticalID);
		cEmpire* ownerEmpire = StarManager.GetEmpire(politicalID);

		if (!ownerEmpire && gadgetMult > 0.01f && GetPlayerEmpire() != nullptr) {
			cEmpire* playerEmp = GetPlayerEmpire();
			float raiderHP = root->spaceCombatMetrics->GetDefenderHealthByEmpire(playerEmp) * (0.90f + 0.25f * gadgetMult);
			if (raiderHP > ufo->mMaxHealthPoints) {
				ufo->mHealthPoints = raiderHP;
				ufo->mMaxHealthPoints = raiderHP;
			}
			return ufo;
		}
		if (!ownerEmpire) {
			return ufo;
		}

		if (ownerEmpire == GetPlayerEmpire()) {
			if (ufoType == UfoType::Defender || static_cast<int>(ufoType) == 3) {
				return ufo;
			}
			if (gadgetMult > 0.01f && ufoType != UfoType::Bomber) {
				float miniCloneHP = root->spaceCombatMetrics->GetBomberHealthByEmpire(ownerEmpire) * (1.15f * gadgetMult);
				if (miniCloneHP > ufo->mMaxHealthPoints) {
					ufo->mHealthPoints = miniCloneHP;
					ufo->mMaxHealthPoints = miniCloneHP;
				}
				return ufo;
			}
		}

		if (ufoType == UfoType::Defender && !sIsGeneratingInitialPlanetGarrison && GetCurrentContext() == SpaceContext::Planet) {
			cPlanetRecord* activePlanet = GetActivePlanetRecord();
			uint32_t nowTime = (root->persistenceState != nullptr) ? root->persistenceState->GetCurrentSpaceTime() : 0;
			if (activePlanet && nowTime > 0) {
				auto itJam = sNpcHangarJamCooldowns.find(activePlanet);
				if (itJam != sNpcHangarJamCooldowns.end() && nowTime < itJam->second) {
					ufo->mHealthPoints = 0.0f;
					return ufo;
				}
			}
		}

		float customHP = 0.0f;
		int typeInt = static_cast<int>(ufoType);

		if (ufoType == UfoType::Bomber) {
			customHP = root->spaceCombatMetrics->GetBomberHealthByEmpire(ownerEmpire);
		}
		else if (ufoType == UfoType::Defender) {
			customHP = root->spaceCombatMetrics->GetDefenderHealthByEmpire(ownerEmpire);
			if (GetCurrentContext() == SpaceContext::Planet && GetActivePlanetRecord() != nullptr) {
				SdoPlanetGadgetInfo gInfo = root->spaceCombatMetrics->GetPlanetGadgetInfo(GetActivePlanetRecord());
				Simulator::Archetypes bArch = ArchetypeUtils::GetBaseArchetype(ownerEmpire->mArchetype);
				if (gInfo.hasUberTurret) {
					float uberShieldBonus = (bArch == Archetypes::kArchetypeScientist) ? 0.40f : 0.25f;
					customHP *= (1.0f + uberShieldBonus * gadgetMult);
				}
				if (gInfo.hasBioProtector) {
					if (bArch == Archetypes::kArchetypeEcologist) {
						customHP *= (1.0f + 0.30f * gadgetMult);
					} else if (bArch == Archetypes::kArchetypeShaman) {
						customHP *= (1.0f + 0.15f * gadgetMult);
					}
				}
			}
		}
		else if (typeInt == 4 || typeInt == 3) {
			customHP = root->spaceCombatMetrics->GetFighterHealthByEmpire(ownerEmpire);
		}

		if (customHP > 1.0f) {
			int maxCap = GetDynamicMaxTreasuryForEmpire(ownerEmpire, root);
			int richThreshold = static_cast<int>(round(static_cast<float>(maxCap) * 0.80f));
			int crisisThreshold = static_cast<int>(round(static_cast<float>(maxCap) * 0.15f));

			if (ownerEmpire->mStars.size() >= 3 && ownerEmpire->mEmpireMoney >= richThreshold) {
				customHP *= 1.20f;
			} else if (ownerEmpire->mEmpireMoney > 0 && ownerEmpire->mEmpireMoney < crisisThreshold) {
				customHP *= 0.75f;
			}
			ufo->mHealthPoints = customHP;
			ufo->mMaxHealthPoints = customHP;
		}

		if (ufoType == UfoType::Defender && !sIsGeneratingInitialPlanetGarrison) {
			if (ShouldTriggerNpcRebellion(ownerEmpire, root, ufo)) {
				ufo->mPoliticalID = 0xFFFFFFFF;
				ufo->mUFOType = static_cast<int>(UfoType::Bomber);
			}
		}
		return ufo;
	}
};

member_detour(GenerateNpcPlanetDefenders__detour, cSimPlanetHighLOD, void(cPlanet* planet))
{
	void detoured(cPlanet* planet)
	{
		if (sIsGeneratingInitialPlanetGarrison) {
			original_function(this, planet);
			return;
		}

		sIsGeneratingInitialPlanetGarrison = true;
		sActiveTradeFreighters.clear();
		ClearScaledOrbitalShips();
		original_function(this, planet);

		if (IsSpaceGame() && planet && planet->mpPlanetRecord) {
			cCompositionRoot* root = cCompositionRoot::Get();
			cPlanetRecord* pRec = planet->mpPlanetRecord.get();

			if (root && root->spaceCombatMetrics) {
				root->spaceCombatMetrics->UpdatePlanetDefenseCache(pRec);
			}

			cEmpire* planetEmpire = planet->GetEmpire();
			if (!planetEmpire && pRec->GetStarRecord() && pRec->GetStarRecord()->mEmpireID != 0) {
				planetEmpire = StarManager.GetEmpire(pRec->GetStarRecord()->mEmpireID);
			}

			float gadgetMult = ReadExternalConfigFloat(u"GadgetConfig", 0x07A1BC06, 1.0f);

			// 0. Динамические 3D-караваны с постоянным крупным размером и свечением щита
			if (planetEmpire && root && root->spaceCombatMetrics && pRec->GetStarRecord()) {
				bool anyBlockaded = false;
				bool hasAnyTrade = root->spaceCombatMetrics->StarHasAnyActiveTradeRoute(pRec->GetStarRecord(), anyBlockaded);

				// А) Собственная флотилия груженых транспортов хозяина планеты
				if (hasAnyTrade && !anyBlockaded && planetEmpire != GetPlayerEmpire()) {
					int localCount = 0;
					float localScale = 1.0f, localHpMult = 1.0f;
					root->spaceCombatMetrics->CalculateDynamicConvoySpecs(pRec, planetEmpire, localCount, localScale, localHpMult);

					int* pLocalPolID = &planetEmpire->mPoliticalID;
					ResourceKey* pLocalKey = &planetEmpire->mUFOKey;

					for (int c = 0; c < localCount; ++c) {
						cGameDataUFO* localFreighter = STATIC_CALL(Address(ModAPI::ChooseAddress(0x0102bbb0, 0x0102acc0)),
							cGameDataUFO*, Args(UfoType, int*, ResourceKey*),
							Args(UfoType::Defender, pLocalPolID, pLocalKey));
						if (localFreighter) {
							float cargoHP = root->spaceCombatMetrics->GetDefenderHealthByEmpire(planetEmpire) * localHpMult;
							localFreighter->mHealthPoints = cargoHP;
							localFreighter->mMaxHealthPoints = cargoHP;
							RegisterScaledOrbitalShip(localFreighter, localScale, id("spc_shield_hit"), id("spc_collect_rare"));
							sActiveTradeFreighters.insert(localFreighter);
						}
					}
				}

				// Б) Мульти-имперские гостевые караваны ВСЕХ торговых партнеров этой звезды (и твои корабли у НПС, и разные расы на хабе!)
				bool hubBlockaded = false;
				eastl::vector<cEmpire*> tradePartners;
				root->spaceCombatMetrics->GetStarActiveTradePartners(pRec->GetStarRecord(), tradePartners, hubBlockaded);

				if (!hubBlockaded && !tradePartners.empty()) {
					int numPartners = static_cast<int>(tradePartners.size());
					for (cEmpire* tradePartner : tradePartners) {
						if (!tradePartner || RelationshipManager.IsAtWar(tradePartner, planetEmpire)) continue;

						int partnerCount = 0;
						float partnerScale = 1.0f, partnerHpMult = 1.0f;
						root->spaceCombatMetrics->CalculateDynamicConvoySpecs(pRec, tradePartner, partnerCount, partnerScale, partnerHpMult);

						if (numPartners > 1) {
							partnerCount = max(1, (partnerCount + numPartners - 1) / numPartners);
						}

						int* pSpawnPolID = (tradePartner == GetPlayerEmpire()) ? &planetEmpire->mPoliticalID : &tradePartner->mPoliticalID;
						ResourceKey* pPartnerKey = &tradePartner->mUFOKey;

						for (int c = 0; c < partnerCount; ++c) {
							cGameDataUFO* freighter = STATIC_CALL(Address(ModAPI::ChooseAddress(0x0102bbb0, 0x0102acc0)),
								cGameDataUFO*, Args(UfoType, int*, ResourceKey*),
								Args(UfoType::Defender, pSpawnPolID, pPartnerKey));
							if (freighter) {
								freighter->mPoliticalID = tradePartner->GetEmpireID();
								float cargoHP = root->spaceCombatMetrics->GetDefenderHealthByEmpire(tradePartner) * partnerHpMult;
								freighter->mHealthPoints = cargoHP;
								freighter->mMaxHealthPoints = cargoHP;
								RegisterScaledOrbitalShip(freighter, partnerScale, id("spc_rare_glow"));
								sActiveTradeFreighters.insert(freighter);
							}
						}
					}
				}
			}

			// 1. Дипломатический Крейсер Посольства ИИ на планете ИГРОКА
			if (planetEmpire && planetEmpire == GetPlayerEmpire() && root && root->spaceCombatMetrics && gadgetMult > 0.01f) {
				SdoPlanetGadgetInfo playerPlanetInfo = root->spaceCombatMetrics->GetPlanetGadgetInfo(pRec);
				if (playerPlanetInfo.aiEmbassyEmpireID != 0) {
					cEmpire* ambassadorEmp = StarManager.GetEmpire(playerPlanetInfo.aiEmbassyEmpireID);
					if (ambassadorEmp && ambassadorEmp != GetPlayerEmpire()) {
						int* pAmbPolID = &ambassadorEmp->mPoliticalID;
						ResourceKey* pAmbKey = &ambassadorEmp->mUFOKey;
						bool atWarNow = RelationshipManager.IsAtWar(ambassadorEmp, GetPlayerEmpire());

						cGameDataUFO* envoyShip = STATIC_CALL(Address(ModAPI::ChooseAddress(0x0102bbb0, 0x0102acc0)),
							cGameDataUFO*, Args(UfoType, int*, ResourceKey*),
							Args(atWarNow ? UfoType::Bomber : UfoType::Defender, pAmbPolID, pAmbKey));
						if (envoyShip) {
							float envoyHP = root->spaceCombatMetrics->GetBomberHealthByEmpire(ambassadorEmp) * 1.50f;
							envoyShip->mHealthPoints = envoyHP;
							envoyShip->mMaxHealthPoints = envoyHP;
							RegisterScaledOrbitalShip(envoyShip, 1.40f, id("spc_shield_hit"));
						}
					}
				}
			}

			// 2. Гарнизон и Орбитальный Дредноут Убербашни на планете НПС
			if (planetEmpire && planetEmpire != GetPlayerEmpire() &&
			    root && root->spaceCombatMetrics && root->simulationValidator &&
			    root->simulationValidator->ValidEmpire(planetEmpire, false, true)) {

				SdoPlanetGadgetInfo npcGadgets = root->spaceCombatMetrics->GetPlanetGadgetInfo(pRec);
				int targetDefenders = root->spaceCombatMetrics->GetDefendersInPlanet(pRec);
				if (targetDefenders > 24) {
					targetDefenders = 24;
				}

				if (targetDefenders > 0) {
					int existingDefenders = 0;
					const auto& ufos = GetData<cGameDataUFO>();
					for (const auto& ufo : ufos) {
						if (ufo && ufo->mHealthPoints > 0 &&
						    ufo->mPoliticalID == planetEmpire->GetEmpireID() &&
						    ufo->mUFOType == static_cast<int>(UfoType::Defender) &&
						    sActiveTradeFreighters.find(ufo.get()) == sActiveTradeFreighters.end()) {
							existingDefenders++;
						}
					}

					int toSpawn = targetDefenders - existingDefenders;
					if (toSpawn > 0) {
						int* pPolID = &planetEmpire->mPoliticalID;
						ResourceKey* pKey = &planetEmpire->mUFOKey;
						for (int i = 0; i < toSpawn; ++i) {
							STATIC_CALL(Address(ModAPI::ChooseAddress(0x0102bbb0, 0x0102acc0)),
								cGameDataUFO*,
								Args(UfoType, int*, ResourceKey*),
								Args(UfoType::Defender, pPolID, pKey));
						}
					}
				}

				// Гигантский Орбитальный Флагман-Страж (Дредноут), если ИИ построил Убербашню
				if (npcGadgets.hasUberTurret && gadgetMult > 0.01f) {
					int* pPolID = &planetEmpire->mPoliticalID;
					ResourceKey* pKey = &planetEmpire->mUFOKey;
					cGameDataUFO* dreadnought = STATIC_CALL(Address(ModAPI::ChooseAddress(0x0102bbb0, 0x0102acc0)),
						cGameDataUFO*, Args(UfoType, int*, ResourceKey*),
						Args(UfoType::Defender, pPolID, pKey));
					if (dreadnought) {
						float bossHP = root->spaceCombatMetrics->GetBomberHealthByEmpire(planetEmpire) * (3.5f * gadgetMult);
						dreadnought->mHealthPoints = bossHP;
						dreadnought->mMaxHealthPoints = bossHP;
						RegisterScaledOrbitalShip(dreadnought, 1.95f, id("spc_uber_turret_idle"), id("spc_shield_hit"));
					}
				}

				TryTriggerNpcPeacetimeColonialUprising(pRec, planetEmpire, root);
			}
		}

		sRaiderRallyPanicActive = false;
		sIsGeneratingInitialPlanetGarrison = false;
	}
};

static void HandlePlayerToolOnNpcPlanet(cSpaceToolData* pTool)
{
	if (!pTool || !IsSpaceGame() || GetCurrentContext() != SpaceContext::Planet) return;
	float gadgetMult = ReadExternalConfigFloat(u"GadgetConfig", 0x07A1BC06, 1.0f);
	if (gadgetMult <= 0.01f) return;

	cPlanetRecord* activePlanet = GetActivePlanetRecord();
	if (!activePlanet || !activePlanet->GetStarRecord()) return;
	uint32_t ownerId = activePlanet->GetStarRecord()->mEmpireID;
	if (ownerId == 0 || ownerId == GetPlayerEmpireID()) return;
	cEmpire* npcEmpire = StarManager.GetEmpire(ownerId);
	if (!npcEmpire) return;

	uint32_t toolId = pTool->mItemID.instanceID;
	cCompositionRoot* root = cCompositionRoot::Get();
	uint32_t nowTime = (root && root->persistenceState) ? root->persistenceState->GetCurrentSpaceTime() : 0;

	if (MatchNpcToolKey(toolId, "soothingsong", "staticcling", "tool_staticcling", 0x04668B95, 0x04668B93)) {
		if (nowTime > 0) {
			uint32_t jamDuration = static_cast<uint32_t>(round(45000.0f * gadgetMult));
			sNpcHangarJamCooldowns[cPlanetRecordPtr(activePlanet)] = nowTime + jamDuration;
		}
	}
	else if (MatchNpcToolKey(toolId, "raiderrally", "pirateraid", "tool_raiderrally", 0x04668B92, 0x03F3E498)) {
		sRaiderRallyPanicActive = true;
	}
	else if (!RelationshipManager.IsAtWar(npcEmpire, GetPlayerEmpire()) &&
	         (MatchNpcToolKey(toolId, "minderase", "globalminderase", "happyray", 0x03F3E491, 0x03F3E481) ||
	          MatchNpcToolKey(toolId, "fireworks", "superhappyray", nullptr, 0x03F3E480, 0x04668B81))) {
		const auto& ufos = GetData<cGameDataUFO>();
		for (const auto& ufo : ufos) {
			if (ufo && ufo->mHealthPoints > 0 && ufo->mPoliticalID == 0xFFFFFFFF && ufo->mUFOType == static_cast<int>(UfoType::Bomber)) {
				ufo->mPoliticalID = npcEmpire->mPoliticalID;
				ufo->mUFOType = static_cast<int>(UfoType::Defender);
			}
		}
	}
}

member_detour(NpcToolOnHit__detour, cToolStrategy, bool(cSpaceToolData*, const Math::Vector3&, SpaceToolHit, int))
{
	bool detoured(cSpaceToolData* pTool, const Math::Vector3& position, SpaceToolHit hitType, int unk)
	{
		bool res = original_function(this, pTool, position, hitType, unk);
		HandlePlayerToolOnNpcPlanet(pTool);
		return res;
	}
};

void AttachDetours()
{
	ShowCommEvent__detour::attach(GetAddress(cCommManager, ShowCommEvent));
	ApplyRelationshipMonolith__detour::attach(GetAddress(cRelationshipManager, ApplyRelationship));
	DeclareWar__detour::attach(GetAddress(cRelationshipManager, DeclareWar));
	TradeWarBreak__detour::attach(GetAddress(cRelationshipManager, DeclareWar));
	DeclareAlliance__detour::attach(GetAddress(cRelationshipManager, DeclareAlliance));
	BreakAlliance__detour::attach(GetAddress(cRelationshipManager, BreakAlliance));
	HandleSpaceCommAction__detour::attach(GetAddress(cCommManager, HandleSpaceCommAction));
	ShowEvent__detour::attach(GetAddress(cUIEventLog, ShowEvent));
	EmpireDestroyed__detour::attach(GetAddress(cEmpire, Destroy));
	ShouldSpawnRaiderUFO__detour::attach(Address(ModAPI::ChooseAddress(0x00c5adc0, 0x00c5b860)));
	SpawnUFO__detour::attach(Address(ModAPI::ChooseAddress(0x0102bbb0, 0x0102acc0)));
	GenerateNpcPlanetDefenders__detour::attach(Address(ModAPI::ChooseAddress(0x00ffa340, 0x00ff96b0)));
	NpcToolOnHit__detour::attach(GetAddress(cToolStrategy, OnHit));
}

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD  ul_reason_for_call,
                      LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		ModAPI::AddPostInitFunction(Initialize);
		ModAPI::AddDisposeFunction(Dispose);

		PrepareDetours(hModule);
		AttachDetours();
		CommitDetours();
		break;

	case DLL_PROCESS_DETACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}
