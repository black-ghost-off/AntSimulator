#pragma once
#include <SFML/System.hpp>
#include "simulation/ant/ant.hpp"


// A larva living inside the colony. It is fed by the colony's food stock
// and matures into a worker, a soldier or even a new queen.
struct Grub
{
	sf::Vector2f position;
	float        age             = 0.0f;
	float        maturation_time = 20.0f;
	uint8_t      hunger          = 0;
	bool         dead            = false;
	Ant::Type    type            = Ant::Type::Worker;

	// Number of failed feedings before starving to death
	static constexpr uint8_t max_hunger = 3;

	Grub() = default;

	Grub(sf::Vector2f pos, Ant::Type type_, float maturation)
		: position(pos)
		, maturation_time(maturation)
		, type(type_)
	{}

	[[nodiscard]]
	bool isMature() const
	{
		return age >= maturation_time;
	}

	void starve()
	{
		if (++hunger >= max_hunger) {
			dead = true;
		}
	}
};
