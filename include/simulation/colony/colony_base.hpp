#pragma once
#include <SFML/System.hpp>
#include "common/racc.hpp"
#include "simulation/config.hpp"


struct ColonyBase
{
	sf::Vector2f position;
	float radius;
	float food;
	float max_food = 1000.0f;
	// Profiling
	float food_acc;
	RMean<float> food_acc_mean;
    uint32_t enemies_found_count = 0;
	// War state: id of the colony we are at war with (-1 if at peace)
	int8_t       war_target    = -1;
	sf::Vector2f war_target_pos;
	float        war_time_left = 0.0f;
	// Larvae stolen from enemies, delivered by raiders and waiting to hatch
	uint32_t     stolen_grubs  = 0;
	// Reported fights against each colony, used to decide war declarations
	uint32_t     enemy_contacts[Conf::MAX_COLONIES_COUNT] = {};

	[[nodiscard]]
	bool atWar() const
	{
		return war_target >= 0;
	}

	ColonyBase()
		: radius(0.0f)
		, food(0.0f)
		, food_acc(0.0f)
		, food_acc_mean(20)
	{}

	ColonyBase(sf::Vector2f position_, float r)
		: position(position_)
		, radius(r)
		, food(0.0f)
		, food_acc(0.0f)
		, food_acc_mean(200)
	{}

	void addFood(float quantity)
	{
		food += quantity;
		food = std::min(max_food, food);
		food_acc += quantity;
	}

	bool useFood(float quantity)
	{
		if (food >= quantity) {
			food -= quantity;
			return true;
		}
		return false;
	}
};
