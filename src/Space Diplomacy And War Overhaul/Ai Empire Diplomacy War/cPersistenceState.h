#pragma once

#include <Spore\BasicIncludes.h>
#include <EASTL\vector.h>
#include "cPersistedObject.h"
#include "ISpaceTimeProvider.h"

#define cPersistenceStatePtr intrusive_ptr<cPersistenceState>

class cPersistenceState
	: public cPersistedObject
	, public ISpaceTimeProvider
{
public:
	static const uint32_t TYPE = id("SpaceDiplomacyWarfareOverhaul::cPersistenceState");
	static const uint32_t NOUN_ID = TYPE;

	int AddRef() override;
	int Release() override;
	void* Cast(uint32_t type) const override;
	uint32_t GetNounID() const override;
	bool Write(Simulator::ISerializerStream* stream) override;
	bool Read(Simulator::ISerializerStream* stream) override;

	static Simulator::Attribute ATTRIBUTES[];

	eastl::string16 GetModVersion() const;
	void SetModVersion(eastl::string16 modVersion);
	uint32_t GetNextInstanceID();
	uint32_t GetCurrentSpaceTime() override;
	void IncrementSpaceTime(uint32_t deltaGameTime);

	// Покупки ИИ (по PlanetID.internalValue)
	eastl::vector<uint32_t> aiGadgetPlanetIDs;
	eastl::vector<uint32_t> aiGadgetFlags;
	eastl::vector<uint32_t> aiGadgetEmbassyEmpires;

	// Кэш посещенных планет Unlimited-Colony (города и турели)
	eastl::vector<uint32_t> cachedDefensePlanetIDs;
	eastl::vector<int> cachedDefenseCities;
	eastl::vector<int> cachedDefenseTurrets;

	// Галактическая торговая сеть (пары StarID.internalValue)
	eastl::vector<uint32_t> tradeRouteStarA;
	eastl::vector<uint32_t> tradeRouteStarB;

private:
	eastl::string16 modVersion;
	uint32_t nextInstanceID = 1;
	uint32_t spaceStageTime = 0;
};

class cPersistenceStateFactory
	: public App::ISPClassFactory
{
public:
	size_t GetTypes(uint32_t* pDstTypes, size_t count) const override;
	Object* Create(uint32_t type, ICoreAllocator* pAllocator) override;
	const char* GetName(uint32_t type) const override;
};
