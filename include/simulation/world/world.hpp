#pragma once
#include <SFML/System.hpp>
#include <vector>
#include <algorithm>

#include "world_grid.hpp"
#include "common/utils.hpp"
#include "wall.hpp"
#include "common/grid.hpp"
#include "common/cooldown.hpp"
#include "common/number_generator.hpp"
#include "simulation/ant/ant_mode.hpp"
#include "render/world_renderer.hpp"


// A plant is a living food generator: it periodically grows food
// around itself and dies after some time
struct Plant
{
	sf::Vector2i cell;
	Cooldown     production;
	float        life = 300.0f;

	Plant() = default;

	Plant(sf::Vector2i cell_, float production_period, float life_time)
		: cell(cell_)
		, production(production_period, RNGf::getUnder(production_period))
		, life(life_time)
	{}

	[[nodiscard]]
	bool isDead() const
	{
		return life <= 0.0f;
	}
};


struct World
{
	sf::Vector2f size;
	WorldGrid map;
	DoubleObject<sf::VertexArray> va_map;
	WorldRenderer renderer;
	// Tunnels dug by ants since last distance field update
	bool walls_changed = false;
	// Food growth (grass like regrowth)
	Cooldown food_growth_cooldown = Cooldown(0.25f);
	static constexpr uint32_t food_growth_attempts  = 8;
	static constexpr uint32_t max_food_per_cell     = 3;
	static constexpr float    food_spread_proba     = 0.05f;
	// Plants (food generators)
	std::vector<Plant> plants;
	Cooldown plant_spawn_cooldown = Cooldown(0.5f);
	static constexpr uint32_t max_plants_count         = 16;
	static constexpr int32_t  plant_radius             = 3;
	static constexpr float    plant_production_period  = 3.0f;
	// Chance for a new plant to randomly sprout somewhere, checked on each spawn cooldown
	static constexpr float    plant_sprout_proba       = 0.35f;

	World(uint32_t width, uint32_t height)
		: map(width, height, 4)
		, size(to<float>(width), to<float>(height))
		, renderer(map, va_map)
	{
        // Create walls around the map
		for (int32_t x(0); x < map.width; x++) {
			for (int32_t y(0); y < map.height; y++) {
				if (x == 0 || x == map.width - 1 || y == 0 || y == map.height - 1) {
					WorldCell& cell     = map.get(sf::Vector2i(x, y));
					cell.wall           = 1;
					cell.indestructible = true;
				}
			}
		}
	}

	void update(float dt)
	{
		map.update(dt);
		updateFoodGrowth(dt);
		updatePlants(dt);
	}

	// Called by ants digging tunnels, returns true if the wall got removed
	bool digWall(WorldCell& cell)
	{
		if (cell.dig()) {
			walls_changed = true;
			return true;
		}
		return false;
	}

	// Food slowly spreads to neighboring cells, like growing grass
	void updateFoodGrowth(float dt)
	{
		if (!food_growth_cooldown.updateAutoReset(dt)) {
			return;
		}
		for (uint32_t i(food_growth_attempts); i--;) {
			const sf::Vector2i cell_coord{
				1 + to<int32_t>(RNGf::getUnder(to<float>(map.width  - 2))),
				1 + to<int32_t>(RNGf::getUnder(to<float>(map.height - 2)))
			};
			WorldCell& cell = map.get(cell_coord);
			if (cell.wall || !cell.food) {
				continue;
			}
			// Spread to a random neighbor
			if (RNGf::proba(food_spread_proba)) {
				const int32_t offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
				const auto& off = offsets[RNGf::getUintUnder(3)];
				const sf::Vector2i neighbor{cell_coord.x + off[0], cell_coord.y + off[1]};
				const WorldCell& neighbor_cell = map.get(neighbor);
				if (!neighbor_cell.wall && neighbor_cell.food < max_food_per_cell) {
					addFoodAt(neighbor, 1);
				}
			}
		}
	}

	// Try to find a random wall free cell (not on the border)
	bool findOpenCell(sf::Vector2i& out, uint32_t attempts = 32) const
	{
		for (uint32_t i(attempts); i--;) {
			const sf::Vector2i cell_coord{
				1 + to<int32_t>(RNGf::getUnder(to<float>(map.width  - 2))),
				1 + to<int32_t>(RNGf::getUnder(to<float>(map.height - 2)))
			};
			if (!map.getCst(cell_coord).wall) {
				out = cell_coord;
				return true;
			}
		}
		return false;
	}

	void addPlant(sf::Vector2i cell)
	{
		plants.emplace_back(cell, plant_production_period, RNGf::getRange(240.0f, 480.0f));
		// The plant itself: if ants eat this food entirely, the plant dies
		addFoodAt(cell, 4);
	}

	// Plants generate food around them and die of old age, or when ants have
	// eaten them entirely. New ones have a small chance to sprout somewhere.
	void updatePlants(float dt)
	{
		// Small chance for a new plant to sprout at a random open spot
		if (plant_spawn_cooldown.updateAutoReset(dt) &&
		    plants.size() < max_plants_count && RNGf::proba(plant_sprout_proba)) {
			sf::Vector2i cell;
			if (findOpenCell(cell)) {
				addPlant(cell);
			}
		}
		// Update existing plants
		for (Plant& plant : plants) {
			plant.life -= dt;
			// The plant dies if its own cell got eaten bare (or walled over)
			const WorldCell& stem = map.getCst(plant.cell);
			if (!stem.food || stem.wall) {
				plant.life = 0.0f;
				continue;
			}
			if (plant.production.updateAutoReset(dt)) {
				// Grow food at a random cell around the plant (never its own
				// cell: once eaten, the plant cannot regrow itself)
				const sf::Vector2i target{
					plant.cell.x + to<int32_t>(RNGf::getFullRange(to<float>(plant_radius))),
					plant.cell.y + to<int32_t>(RNGf::getFullRange(to<float>(plant_radius)))
				};
				if (target != plant.cell && map.checkCoords(target)) {
					const WorldCell& cell = map.getCst(target);
					if (!cell.wall && cell.food < max_food_per_cell) {
						addFoodAt(target, 1);
					}
				}
			}
		}
		// Remove dead plants
		plants.erase(std::remove_if(plants.begin(), plants.end(),
		                            [](const Plant& p) { return p.isDead(); }),
		             plants.end());
	}

	void addMarker(sf::Vector2f pos, Mode type, float intensity, uint8_t colony_id, bool permanent = false)
	{
		map.addMarker(pos, type, intensity, colony_id, permanent);
	}

	void addMarkerRepellent(sf::Vector2f pos, uint64_t colony_id, float amount)
	{
		map.get(pos).markers[colony_id].repellent += amount;
	}

	void addWall(const sf::Vector2f& position)
	{
		addWall(sf::Vector2i{to<int32_t>(position.x) / map.cell_size, to<int32_t>(position.y) / map.cell_size});
	}

    void addWall(const sf::Vector2i& position)
    {
        if (map.checkCoords(position)) {
			WorldCell& cell = map.get(position);
            cell.food = 0;
            cell.wall = 1;
			for (auto& markers : cell.markers) {
				clearMarkersOfCell(markers);
			}
        }
    }

	void removeWall(const sf::Vector2f& position)
	{
		if (map.checkCoords(position)) {
			map.get(position).wall = 0;
		}
	}

	void render(sf::RenderTarget& target, sf::RenderStates states)
	{
		states.texture = &(*Conf::MARKER_TEXTURE);
		renderer.mutex.lock();
		target.draw(va_map.getCurrent(), states);
		renderer.mutex.unlock();
	}

	void addFoodAt(float x, float y, uint32_t quantity)
	{
        addFoodAt(sf::Vector2i{to<int32_t>(x) / map.cell_size, to<int32_t>(y) / map.cell_size}, quantity);
	}

    void addFoodAt(sf::Vector2i pos, uint32_t quantity)
    {
        if (map.checkCoords(pos)) {
            map.addMarker(pos, Mode::ToFood, 1.0f, true);
            map.addFood(pos, quantity);
        }
    }

    void clearMarkers(uint8_t colony_id)
    {
        for (auto& cell : map.cells) {
			clearMarkersOfCell(cell.markers[colony_id]);
            cell.density = 0.0f;
        }
    }

	static void clearMarkersOfCell(ColonyCell& cell)
	{
		cell.intensity[0] = 0.0f;
		cell.intensity[1] = 0.0f;
		cell.intensity[2] = 0.0f;
		cell.repellent    = 0.0f;
		cell.permanent    = false;
	}
};
