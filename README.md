# ⚔️ Space Diplomacy & Warfare Overhaul — Living 4X Galaxy, Trade & Revolutions

**Platform:** Spore: Galactic Adventures (`v1.1.3+`)  
**Requires:** Spore ModAPI Launcher Kit + UPE  
**Required Companion & Config Hub:** [Defensive Ships in Player Colonies](https://github.com/GreenPerfectAlt/Defensive-Ships-In-Player-Colonies_GreenPerfectAlt)

**Space Diplomacy & Warfare Overhaul** transforms the Space Stage into a deep 4X grand strategy simulation. Building upon Sadicto's Diplomacy & Warfare foundation, this expanded fork introduces real **AI Empire Treasuries & Upkeep**, **Multi-Species 3D Trade Convoys & Plundering**, **10 Asymmetrical Philosophy Doctrines**, **Orbital Uber-Turret Dreadnoughts**, **The Great Zealot Schism**, and **Background Galactic Revolutions** that fracture bankrupt superpowers into brand-new breakaway republics!

---

## ⚠️ IMPORTANT: Two-Mod Ecosystem (Install Both!)

This mod is **Part 2** of a unified two-mod Space Stage overhaul. **Install BOTH mods together** so all XML balance presets and cross-mod mechanics work seamlessly:

1. 🛡️ **[Defensive Ships in Player Colonies (`v1.0.9+`)](https://github.com/GreenPerfectAlt/Defensive-Ships-In-Player-Colonies_GreenPerfectAlt)** *(Part 1 — Config Hub & Player Garrisons)*
   * Powers **your** automated colony defense fleets, orbital shipyards, fleet upkeep, citizen strikes, and rebellions.
   * Houses the **6-Module Interactive ModAPI Installer** (`FleetConfig`, `RebellionConfig`, `UpkeepConfig`, `UnrestConfig`, `GadgetConfig`, `TradeConfig`) that dynamically configures **this mod** with zero hardcoded multipliers or timers!
2. ⚔️ **[Space Diplomacy & Warfare Overhaul (`v1.1.3+`)](https://github.com/GreenPerfectAlt/Space-Diplomacy-And-Warfare-Overhaul_GreenPerfectAlt)** *(Part 2 — This Mod)*
   * Drives autonomous AI alliances, frontlines, treasuries, 3D trade spaceports, AI gadget construction, philosophy warfare, and galaxy-map secession wars.

---

## 🚀 New 4X Overhaul Systems (`v1.0.1` – `v1.1.3`)

### 1. Galactic Trade Network, Multi-Species Spaceports & 3D Plundering
* **Vanilla Blue-Line & AI Cluster Sync:** Hooks directly into Spore's native `mTradeRouteManager` alongside autonomous AI-to-AI trade routes. On save load, neighboring empires immediately establish regional trade routes based on spice rarity arbitrage and Affinity.
* **Multi-Empire Spaceports & Two-Way Convoys:** Major hubs (**Homeworlds** and **T3** worlds) host guest cargo fleets from **all** active trade partners simultaneously! Your own Sporepedia ships visibly patrol foreign trade partners' cities, while allied freighters help defend your colonies against pirates and rebels.
* **Dynamic Tanker Scaling:** Convoy ship counts and hull armor scale dynamically with planet T-Score, cached **Unlimited-Colony** city counts, **Spice Storage** depots (`1.35x`), and logarithmic spice value (`log2(spiceRatio)`), highlighted by glowing orbital auras (`spc_rare_glow`).
* **1v1 Convoy Plundering & Blockades:**
  * Active sieges **blockade** star systems—freezing trade income and raising city strike unrest (`+25%`).
  * Declaring war shatters trade contracts and inflicts immediate **Financial Shock** on both treasuries.
  * Destroying enemy domestic or partner cargo freighters in 3D combat **plunders Sporebucks** straight from the enemy's treasury into your wallet with native cash-register audio and rare-artifact visual bursts!

### 2. Real AI Empire Treasury, Upkeep & Autonomous Gadget Purchasing
* **No Infinite AI Money:** AI empires earn taxes from spice production and trade routes (`mEmpireMoney`), pay periodic **Fleet Upkeep**, and fund bomber squadrons from real reserves.
* **Boom vs. Bankruptcy:** Wealthy empires (`>=80%` treasury cap) field elite reinforced ships (**`1.20x` HP**) and purchase bonus invasion wings. Bankrupt empires (`<15%` cap) suffer supply collapse (**`0.75x` HP**), halted invasions, and high rebellion risk.
* **Persistent AI Infrastructure (`cPersistenceState`):** Prosperous AI empires invest surplus funds into permanent **Spice Storage** depots, **Loyalty/Happiness Boosters**, **Bio-Protectors**, **Uber Turrets**, and foreign **Embassies**—all saved persistently across game sessions!

### 3. Background Galactic Revolutions, SOS Alerts & Breakaway Republics
* **Live Galaxy-Map Uprisings:** Overextended superpowers (`6+` stars by default, configurable via XML) suffering bankruptcy or ideological collapse face real frontier revolutions in the background without requiring player visits.
* **Native SOS Distress Signals:** Triggers genuine Spore emergency alerts (`RaidPlunderEvent`) in the top-left HUD when allied or neighboring colonies erupt into revolt—intervene for reputation or let them burn!
* **Secession & Breakaway Republics:** If a bankrupt giant fails to contain a revolt on an unfortified frontier star (no **Loyalty Booster**), that star **secedes into a brand-new independent empire**! The breakaway republic retains its parent species and ship model (`mUFOKey`), adopts an opposing revolutionary philosophy, receives starting capital, and immediately fights a **War of Independence** against its former master! *(Small empires are 100% protected from collapse).*

### 4. "The Great Schism" — Zealot Heresy Cascade
* Normally, **Zealot** garrisons have **`0%` rebellion chance** thanks to blind fanaticism.
* **Shattering the Dogma:** Planting a foreign **Embassy** on a Zealot world, starving their cities into **Factory Strikes** via blockade, or triggering **Raider Rally** panic shatters their religious immunity—igniting **The Great Schism** (`55%` rebellion rate) across **every star system** in that Zealot empire simultaneously!

### 5. Orbital Dreadnoughts, Embassies & Ship Tools
* **Uber-Turret Orbital Dreadnoughts:** AI worlds fortified with an **Uber Turret** deploy a massive **Boss Flagship (`3.5x` Bomber HP)** alongside `+14` planetary turrets and `+25%` garrison armor (`+40%` for Scientists).
* **Diplomatic Cruisers & Espionage:** Allied AI empires open **Embassies** on your worlds, stationing an allied **Diplomatic Cruiser** in your orbit (`-50%` unrest) that turns hostile if war breaks out. Your own Embassies on NPC worlds boost enemy garrison defection by **`+25%`**.
* **Active 3D Siege Tools:** **Static Cling / Soothing Song** jams NPC planetary hangars for `45s` (blocking defender respawns), while **Raider Rally** triggers `1.35x` orbital mutiny panic.

---

## 🏛️ Complete Philosophy Asymmetry (All 10 Archetypes)

| Philosophy | Trade Network & Convoys | Combat & Garrison Doctrine | Signature Strength & Achilles' Heel |
| :--- | :--- | :--- | :--- |
| **Trader** | **`+3` Route Slots**, **`+45%` Trade Profit**, **`+35%` Convoy Ships**, trades with neutrals (`Affinity >= 0`). | `1.6x` Garrison size, `1.75x` Treasury Cap, `2.0x` War Deterrence. | **Strength:** Richest banks & Spice Depots.<br>**Weakness:** Mercenary fleets mutiny fast (`40%`) if bankrupted! |
| **Diplomat** | **`+2` Route Slots**, `2.0x` War Deterrence, builds **Embassies** on player worlds. | Light hulls (`-20%` HP), small native garrison (`0.8x`). | **Federation Shield:** Guest partner convoys on Diplomat worlds gain **`+1` ship & `+35%` HP**!<br>**Weakness:** Suffers **`2.0x` Financial Shock** when war breaks trade. |
| **Scientist** | **`1.8x` Hyper-Relay Trade Range**, `+1` Route Slot, **`+20%` Trade Profit**. | `+10%` Hull HP, `1.6x` Garrison, **`+35%` AI Turret HP**, **`+40%` Uber-Turret Shields**. | **Strength:** Long-range control relays (`1.8x` anti-rebel radius).<br>**Weakness:** **`2.0x` Factory Strike unrest** during wars/blockades! |
| **Ecologist** | Prioritizes `T2/T3` terraformed hubs (`1.35x` weight), **`+25%` Convoy Ships** on `T3`/Capitals. | **`2.0x` Fortress Garrisons** on `T3`/Homeworlds (`1.1x` elsewhere), **`+30%` Bio-Protector HP**. | **Strength:** Living `T3` worlds are natural bastions.<br>**Weakness:** Despises wartime pollution (**`2.5x` Factory Strike unrest**)! |
| **Bard** | **`+2` Route Slots**, **`+15%` Trade Profit**, **`+25%` Convoy Ships**, open borders (`Affinity >= 0`). | Lightest hulls (`-20%` HP), small garrison (`0.8x`), loyal frontiers (`0.65x` unrest). | **Strength:** Bustling multi-species spaceport capitals.<br>**Weakness:** Fragile, high-value convoys—prime targets for piracy & plundering! |
| **Shaman** | **`+25%` Rare-Spice Profit** and **`1.25x` Crystal Armor scaling** on rare spice routes. | Standard hulls + **`+15%` Bio-Protector HP**, peaceful frontiers (`0.65x` unrest). | **Strength:** Turns purple/pink spice systems into rich, armored sanctuaries.<br>**Weakness:** Modest `0.8x` garrisons on common red/yellow worlds. |
| **Warrior** | Prioritizes internal military supply lines (**`0.85x` internal route yield** vs `0.55x`). | **Elite Heavy Armor (`+30%` HP)** & massive **`2.5x` Garrisons**, ignores war deterrence (`0.35x`). | **Strength:** Flying tanks in 1v1 wars.<br>**Weakness:** Constant war spending drains banks, causing **`50%` frontier general mutinies**! |
| **Zealot** | Closed sect (`50%` route slots), trades only with coreligionists & allies. | **Massive `2.5x` Kamikaze Swarms** with lighter **`+10%` Hulls**, ignores trade losses (`0.35x`). | **Strength:** **`0%` Mutiny Immunity** under intact dogma.<br>**Weakness:** Vulnerable to **The Great Schism (`55%` empire-wide heresy cascade)**! |
| **Grox** | **Total Galactic Embargo** (`0` foreign routes), internal cyber-supply lines only. | **Maximum `+30%` Cyber-Armor**, `2.5x` Armadas, **`0%` Rebellions**. | Eternal machine war against all organic life; internal transports yield pure plunder. |
| **Knight / Wanderer** *(Player)* | **Player Knight:** Deploys **Honor Escorts** on allied worlds (**`+20%` Convoy Ships & `+15%` Shield HP**). | **Knight:** `1.35x` Heavy Garrison Armor.<br>**Wanderer:** Balanced adaptable baseline. | Gives player-exclusive archetypes distinct presence across allied spaceports. |

---

## 🤝 Core Diplomacy, Affinity & Cluster Warfare

* **Affinity System:** Diplomacy revolves around philosophical compatibility, shared allies (`+1`), common enemies (`+2`), monolith uplifting (`+2`), active **Trade Networks (`+1` to `+2`)**, and long-standing alliances (`+2`).
* **Coherent Frontlines & Invasion Call:** AI empires attack contiguous border stars rather than jumping randomly across space, sizing bomber fleets against real planetary defenses (including cached **Unlimited-Colony** cities and **Uber Turrets**!). Players can summon allied strike fleets via the dynamically priced **Invasion Call** tool.
* **40-Minute Truces & War Warnings:** Empires preparing war notify the player in advance (avoidable by raising relations above `+30`), and peace treaties enforce 40-minute bloc truces.

![Affinity Interface](images/AffinityTooltip.png)
![Diplomacy Notifications](images/DiplomacyNotifications.png)
![Allies and Enemies View](images/AiAlliesEnemies.png)
![Invasion Call Summon](images/InvasionCallPlanet.png)

---

## 📥 Installation & Customization

1. Requires **Spore: Galactic Adventures**, **Spore ModAPI Launcher Kit**, and **UPE**.
2. Download and install **[Defensive Ships in Player Colonies (`v1.0.9+`)](https://github.com)** first—select your preferred presets for Fleet Scale, Rebellions, Upkeep, Unrest, Gadgets, and Galactic Trade.
3. Download the latest **`Space-Diplomacy-And-Warfare-Overhaul.sporemod`** from **[Releases](../../releases)** and install it via **Spore ModAPI Easy Installer** (configure Active Radius, Bomber Rates, Notifications, etc.).
4. Launch the game via **Spore ModAPI Launcher**! Fully save-game compatible (starting a new game is optional for initial Affinity modifiers).

---

## 📜 Credits

* Original *Space Diplomacy & Warfare Overhaul* architecture and UI assets by **Sadicto**.
* 4X Treasury Economy, Galactic Trade Network, 3D Convoys, Philosophy Asymmetry, Gadget AI, Save Persistence, and Galactic Secession Overhaul by **GreenPerfectAlt**.
