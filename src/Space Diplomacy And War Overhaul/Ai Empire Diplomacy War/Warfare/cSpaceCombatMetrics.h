#pragma once

#include <Spore\BasicIncludes.h>
#include <EASTL\map.h>
#include <EASTL\vector.h>
#include <EASTL\utility.h>

class cPersistenceState;

#define cSpaceCombatMetricsPtr intrusive_ptr<cSpaceCombatMetrics>

struct SdoPlanetGadgetInfo {
	bool hasLoyaltyBooster = false;
	bool hasHappinessBooster = false;
	bool hasUberTurret = false;
	bool hasSpiceStorage = false;
	bool hasBioProtector = false;
	bool hasEmbassy = false;
	uint32_t aiEmbassyEmpireID = 0;
};

struct SdoTradeRouteEntry {
	uint32_t starIdA = 0;
	uint32_t starIdB = 0;
};

struct SdoTradeConfigValues {
	float incomeMultiplier = 1.0f;       // 0x07A1BC07
	float spiceArbitrageWeight = 1.0f;   // 0x07A1BC08
	float warDeterrenceStrength = 1.0f;  // 0x07A1BC09
	int maxRoutesPerEmpire = 5;          // 0x07A1BC0A
	int routesPerAffinityPoint = 2;      // 0x07A1BC0B
	float convoyDensity = 1.0f;          // 0x07A1BC0C
	float convoyBaseScale = 1.25f;       // 0x07A1BC0D
	float convoyScalePerSpice = 0.08f;   // 0x07A1BC0E
};

class cSpaceCombatMetrics 
	: public Object
	, public DefaultRefCounted
{
public:
	static const uint32_t TYPE = id("SpaceWarfareOverhaul::cSpaceCombatMetrics");
	
	cSpaceCombatMetrics(ResourceKey spaceCombatKey);
	~cSpaceCombatMetrics();

	int AddRef() override;
	int Release() override;
	void* Cast(uint32_t type) const override;

	float GetBomberHealthByLevel(int empireLevel);
	float GetFighterHealthByLevel(int empireLevel);
	float GetDefenderHealthByLevel(int empireLevel);
	float GetTurretHealthByLevel(int empireLevel);

	int GetDefendersInOutpost();
	int GetDefendersInColony();
	int GetFightersPerBomber();

	int GetTurretsInT1();
	int GetTurretsInT2();
	int GetTurretsInT3();

	float GetBomberHealthByEmpire(Simulator::cEmpire* empire);
	float GetFighterHealthByEmpire(Simulator::cEmpire* empire);
	float GetDefenderHealthByEmpire(Simulator::cEmpire* empire);
	float GetTurretHealthByEmpire(Simulator::cEmpire* empire);

	int GetDefendersInPlanet(Simulator::cPlanetRecord* planet);
	int GetTurretsInPlanet(Simulator::cPlanetRecord* planet);

	// Кэш посещенных планет для Unlimited-Colony и постройки ИИ
	void UpdatePlanetDefenseCache(Simulator::cPlanetRecord* planet);
	int GetCachedCityCount(Simulator::cPlanetRecord* planet);
	int GetCachedTurretCount(Simulator::cPlanetRecord* planet);
	SdoPlanetGadgetInfo GetPlanetGadgetInfo(Simulator::cPlanetRecord* planet);
	void RegisterAiGadgetPurchase(Simulator::cPlanetRecord* planet, const SdoPlanetGadgetInfo& purchasedInfo);

	// Галактическая торговая сеть (без хардкода, через TradeConfig.prop)
	SdoTradeConfigValues GetTradeConfig() const;
	float GetNormalizedStarSpiceRatio(Simulator::cStarRecord* star) const;
	bool IsStarBlockaded(Simulator::cStarRecord* star) const;
	bool HasTradeRouteBetweenStars(uint32_t starA, uint32_t starB) const;
	int GetEmpireTotalTradeRoutes(Simulator::cEmpire* empire);
	int GetActiveTradeRoutesBetweenEmpires(Simulator::cEmpire* emp1, Simulator::cEmpire* emp2, bool includeBlockaded = false);
	// Индекс промышленного загрязнения империи и бонус/штраф симпатии Экологов к игроку
	float GetEmpirePollutionIndex(Simulator::cEmpire* targetEmpire);
	int GetEcologistEcoAffinityModifier(Simulator::cEmpire* empire1, Simulator::cEmpire* empire2);
	int GetTradeAffinityBonus(Simulator::cEmpire* emp1, Simulator::cEmpire* emp2);
	float GetTradeWarDeterrenceMultiplier(Simulator::cEmpire* attacker, Simulator::cEmpire* target);
	int ManageAndCalculateEmpireTradeIncome(Simulator::cEmpire* empire, int baseUnitCost, float tradeRange);
	int BreakTradeRoutesOnWar(Simulator::cEmpire* emp1, Simulator::cEmpire* emp2, int baseUnitCost);
	Simulator::cEmpire* GetStarActiveTradePartner(Simulator::cStarRecord* star, bool& outBlockaded);
	void GetStarActiveTradePartners(Simulator::cStarRecord* star, eastl::vector<Simulator::cEmpire*>& outPartners, bool& outBlockaded);
	bool StarHasAnyActiveTradeRoute(Simulator::cStarRecord* star, bool& outBlockaded);
	void CalculateDynamicConvoySpecs(Simulator::cPlanetRecord* planet, Simulator::cEmpire* convoyEmpire, int& outShipCount, float& outScaleMult, float& outHpMult);

	void ExportStateToPersistence(cPersistenceState* state);
	void ImportStateFromPersistence(const cPersistenceState* state);
	void EnsurePersistenceLoaded();

	eastl::vector<float> healthBomber;
	eastl::vector<float> healthFighter;
	eastl::vector<float> healthDefender;
	eastl::vector<float> healthTurret;

	int numDefendersInOutpost;
	int numDefendersInColony;
	int numFightersPerBomber;

	int numTurretsInT1;
	int numTurretsInT2;
	int numTurretsInT3;

private:
	void PruneInvalidTradeRoutes();
	void BootstrapInitialTradeNetwork();

	bool persistenceImported = false;
	eastl::map<cPlanetRecordPtr, eastl::pair<int, int>> planetDefenseCache;
	eastl::map<cPlanetRecordPtr, SdoPlanetGadgetInfo> planetGadgetCache;
	eastl::map<cPlanetRecordPtr, SdoPlanetGadgetInfo> aiPurchasedGadgets;
	eastl::vector<SdoTradeRouteEntry> galacticTradeRoutes;
};
