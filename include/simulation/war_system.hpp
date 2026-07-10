#pragma once
#include "simulation/colony/colony.hpp"
#include "common/index_vector.hpp"


// Handles wars between colonies: repeated fights against a given colony
// eventually trigger a war. Soldiers are then sent raiding the enemy nest,
// killing its defenders and stealing its larvae to raise them at home.
struct WarSystem
{
	// Number of reported fights against one colony needed to declare war
	static constexpr uint32_t contacts_threshold = 24;
	// Minimum army size to consider going to war
	static constexpr uint32_t min_soldiers       = 8;
	// Duration of a war campaign
	static constexpr float    war_duration       = 180.0f;
	// Raiders steal larvae when this close to the enemy nest center
	static constexpr float    steal_dist_coef    = 1.2f;

	// Old grudges slowly fade away
	Cooldown contacts_decay = Cooldown(30.0f);

	void update(civ::Vector<Colony>& colonies, float dt)
	{
		const bool decay = contacts_decay.updateAutoReset(dt);
		for (Colony& colony : colonies) {
			if (decay) {
				for (uint32_t& c : colony.base.enemy_contacts) {
					c /= 2;
				}
			}
			if (colony.base.atWar()) {
				updateWar(colony, colonies, dt);
			}
			else {
				checkWarDeclaration(colony, colonies);
			}
		}
	}

	static Colony* getColonyById(civ::Vector<Colony>& colonies, uint8_t id)
	{
		for (Colony& c : colonies) {
			if (c.id == id) {
				return &c;
			}
		}
		return nullptr;
	}

	static void updateWar(Colony& colony, civ::Vector<Colony>& colonies, float dt)
	{
		ColonyBase& base = colony.base;
		base.war_time_left -= dt;
		Colony* enemy = getColonyById(colonies, to<uint8_t>(base.war_target));
		// The campaign ends when time runs out or the enemy is gone
		if (!enemy || base.war_time_left <= 0.0f) {
			endWar(colony);
			return;
		}
		base.war_target_pos = enemy->base.position;
		// Raiders that reached the enemy nest try to steal a larva
		const float steal_dist = steal_dist_coef * enemy->base.radius;
		for (Ant& a : colony.ants) {
			if (a.phase == Mode::Raid && !a.raid_returning && !a.isFighting()) {
				if (getLength(a.position - enemy->base.position) < steal_dist) {
					if (!enemy->grubs.empty()) {
						a.has_grub = true;
						enemy->grubs.pop_back();
					}
					// Loaded or not, head back home
					a.raid_returning = true;
					a.detectEnemy();
				}
			}
		}
	}

	static void checkWarDeclaration(Colony& colony, civ::Vector<Colony>& colonies)
	{
		ColonyBase& base = colony.base;
		// Find the most fought against colony
		uint8_t  worst_enemy  = 255;
		uint32_t max_contacts = 0;
		for (uint8_t i(0); i < Conf::MAX_COLONIES_COUNT; ++i) {
			if (i != colony.id && base.enemy_contacts[i] > max_contacts) {
				max_contacts = base.enemy_contacts[i];
				worst_enemy  = i;
			}
		}
		if (max_contacts < contacts_threshold || colony.soldiersCount() < min_soldiers) {
			return;
		}
		Colony* enemy = getColonyById(colonies, worst_enemy);
		if (!enemy) {
			base.enemy_contacts[worst_enemy] = 0;
			return;
		}
		// War is declared: mobilize the army
		base.war_target     = to<int8_t>(worst_enemy);
		base.war_target_pos = enemy->base.position;
		base.war_time_left  = war_duration;
		for (uint32_t& c : base.enemy_contacts) {
			c = 0;
		}
		for (Ant& a : colony.ants) {
			if (a.type == Ant::Type::Soldier && !a.isFighting()) {
				a.phase          = Mode::Raid;
				a.raid_returning = false;
				a.raid_target    = base.war_target_pos;
			}
		}
	}

	static void endWar(Colony& colony)
	{
		colony.base.war_target    = -1;
		colony.base.war_time_left = 0.0f;
		// Call the troops back home
		for (Ant& a : colony.ants) {
			if (a.phase == Mode::Raid) {
				a.raid_returning = true;
			}
		}
	}
};
