#pragma once
#include <vector>

#include "simulation/ant/ant_mode.hpp"
#include "simulation/config.hpp"
#include "common/utils.hpp"
#include "common/grid.hpp"


constexpr float min_intensity = 0.1f;


struct AntRef
{
	bool     active = false;
	uint8_t  col_id = 0;
	uint16_t ant_id = 0;
};


struct ColonyCell
{
	// Stores the intensity of ToHome and ToFood markers
	float intensity[3];
	// Is the marker permanent ?
	bool permanent = false;
	// Repellent intensity
	float repellent;
	// Current ant
	int16_t current_ant = -1;
    bool fighting;

	ColonyCell()
		: intensity{0.0f, 0.0f, 0.0f}
		, repellent(0.0f)
        , fighting(false)
	{}

	void update(float dt)
	{
		// Reset current ant
		current_ant = -1;
        fighting    = false;
		// Update toFood and toHome
		intensity[0] -= permanent ? 0.0f : dt;
		intensity[1] -= dt;
		intensity[2] -= dt;
		// Update repellents
		repellent -= dt;
		repellent = std::max(0.0f, repellent);
	}
};


struct WorldCell
{
	// Wall materials
	static constexpr uint32_t wall_none = 0;
	static constexpr uint32_t wall_rock = 1;
	static constexpr uint32_t wall_dirt = 2;

	ColonyCell markers[Conf::MAX_COLONIES_COUNT];
	// Food quantity in the cell
	uint32_t food;
	// Wall material (none / rock / dirt)
	uint32_t wall;
	// Digging state (tunnels dug by ants)
	uint8_t dig_hits       = 0;
	bool    indestructible = false;
	// Scent emitted by food, it diffuses to nearby cells and is
	// strongly (but not fully) attenuated by walls
	float food_scent       = 0.0f;
	// Density of ants at a certain point
	float density;
	// Dist to wall
	float wall_dist;
	float discovered;

	WorldCell()
		: food(0)
		, wall(0)
		, density(0.0f)
		, wall_dist(0.0f)
		, discovered(1.0f)
	{
		for (ColonyCell& c : markers) {
			c = ColonyCell();
		}
	}

	void update(float dt)
	{
		// Update intensities
		for (uint8_t i(Conf::MAX_COLONIES_COUNT); i--;) {
			markers[i].update(dt);
		}
		// Update density
		density *= 0.99f;
		//discovered += dt * float(bool(discovered));
	}

	bool pick()
	{
		const bool last = (food <= 1.0f);
		food = (food - bool(food)) * (!last);
		return last;
	}

	// One dig attempt, returns true if the wall got removed.
	// Dirt is much softer than rock.
	bool dig()
	{
		static constexpr uint8_t rock_resistance = 10;
		static constexpr uint8_t dirt_resistance = 3;
		if (!wall || indestructible) {
			return false;
		}
		const uint8_t resistance = (wall == wall_dirt) ? dirt_resistance : rock_resistance;
		if (++dig_hits >= resistance) {
			wall     = wall_none;
			dig_hits = 0;
			return true;
		}
		return false;
	}
    
    AntRef getEnemy(uint8_t team)
    {
		AntRef res;
        // Update intensities
        for (uint8_t i(Conf::MAX_COLONIES_COUNT); i--;) {
            if (i != team && markers[i].current_ant > -1) {
				res.active = true;
				res.ant_id = markers[i].current_ant;
				res.col_id = i;
                return res;
            }
        }
        return res;
    }

	bool checkEnemyPresence(uint8_t team)
	{
		// Update intensities
		for (uint8_t i(0); i<Conf::MAX_COLONIES_COUNT; ++i) {
			if (i != team && markers[i].current_ant > -1) {
				return true;
			}
		}
		return false;
	}

	bool checkFight(uint8_t colony_id)
	{
		return markers[colony_id].fighting;
	}

	float& getRepellent(uint8_t colony_id)
	{
		return markers[colony_id].repellent;
	}

	[[nodiscard]]
	float getIntensity(Mode mode, uint8_t colony_id) const
	{
		return std::max(min_intensity, markers[colony_id].intensity[to<uint32_t>(mode)]);
	}

	[[nodiscard]]
	bool isPermanent(uint8_t colony_id) const
	{
		return markers[colony_id].permanent;
	}

	void degrade(uint8_t colony_id, Mode mode, float ratio)
	{
		markers[colony_id].intensity[to<uint32_t>(mode)] *= ratio;
	}

	void addPresence()
	{
		density += 1.0f;
	}
};


struct HitPoint
{
	WorldCell* cell;
	sf::Vector2f normal;
	float distance;

	HitPoint()
		: cell(nullptr)
		, distance(-1.0f)
	{}

	HitPoint(WorldCell& c, sf::Vector2f n, float dist)
		: cell(&c)
		, normal(n)
		, distance(dist)
	{}

	bool isDistanceUnder(float d) const
	{
		return (distance != -1.0f) && (distance < d);
	}
};


struct WorldGrid : public Grid<WorldCell>
{
	WorldGrid(uint32_t width_, uint32_t height_, uint32_t cell_size_)
		: Grid(width_, height_, cell_size_)
	{
	}

	void addMarker(sf::Vector2f pos, Mode type, float intensity, uint8_t colony_id, bool permanent = false)
	{
        addMarker(sf::Vector2i{to<int32_t>(pos.x) / cell_size, to<int32_t>(pos.y) / cell_size}, type, intensity, colony_id, permanent);
	}

    void addMarker(sf::Vector2i pos, Mode type, float intensity, uint8_t colony_id, bool permanent = false)
    {
        ColonyCell& cell = get(pos).markers[colony_id];
        const auto mode_index = to<uint32_t>(type);
        // Overwrite intensity if different colony
        cell.intensity[mode_index] = std::max(cell.intensity[mode_index], intensity);
        cell.permanent |= permanent;
    }

	void addFood(sf::Vector2f pos, uint32_t quantity)
	{
        addFood(sf::Vector2i(to<int32_t>(pos.x), to<int32_t>(pos.y)), quantity);
	}

    void addFood(sf::Vector2i pos, uint32_t quantity)
    {
        WorldCell& cell = get(pos);
        if (!cell.wall) {
            cell.food += quantity;
        }
    }

	void remove(sf::Vector2f pos, Mode type, uint8_t colony_id)
	{
		WorldCell& cell = get(pos);
		const auto mode_index = to<uint32_t>(type);
		cell.markers[colony_id].intensity[mode_index] = 0.0f;
	}

	void update(float dt)
	{
		for (WorldCell& c : cells) {
			c.update(dt);
		}
		updateFoodScent();
	}

	// Propagate food scent by max-relaxation. Each frame every cell takes the
	// strongest neighboring scent, faded by the medium: air lets it travel far,
	// dirt muffles it a little, rock muffles it strongly (but a faint trace
	// still gets through, giving ants a direction to dig in). Without a food
	// source the field self-decays.
	void updateFoodScent()
	{
		static constexpr float air_scent_fade  = 0.95f;
		static constexpr float dirt_scent_fade = 0.85f;
		static constexpr float rock_scent_fade = 0.60f;
		for (int32_t y(1); y < height - 1; ++y) {
			for (int32_t x(1); x < width - 1; ++x) {
				WorldCell& cell = cells[x + y * width];
				if (cell.food && !cell.wall) {
					cell.food_scent = 1.0f;
					continue;
				}
				const float best = std::max(
					std::max(cells[x - 1 + y * width].food_scent, cells[x + 1 + y * width].food_scent),
					std::max(cells[x + (y - 1) * width].food_scent, cells[x + (y + 1) * width].food_scent));
				float fade = air_scent_fade;
				if (cell.wall) {
					fade = (cell.wall == WorldCell::wall_dirt) ? dirt_scent_fade : rock_scent_fade;
				}
				cell.food_scent = best * fade;
			}
		}
	}

    [[nodiscard]]
	bool isOnFood(sf::Vector2f pos) const
	{
		return getCst(pos).food;
	}

	bool pickFood(sf::Vector2f pos)
	{
		return get(pos).pick();
	}

    void clearCell(sf::Vector2i cell_coord)
    {
        auto& cell = get(cell_coord);
        cell.wall = 0;
        cell.food = 0;
    }

	HitPoint getFirstHit(sf::Vector2f p, sf::Vector2f d, float max_dist)
	{
		HitPoint intersection;
		sf::Vector2i cell_p = getCellCoords(p);
		const sf::Vector2i step(d.x < 0.0f ? -1 : 1, d.y < 0.0f ? -1 : 1);
		const sf::Vector2f inv_d(1.0f / d.x, 1.0f / d.y);
		const float t_dx = std::abs(float(cell_size) * inv_d.x);
		const float t_dy = std::abs(float(cell_size) * inv_d.y);
		float t_max_x = ((cell_p.x + (step.x > 0)) * cell_size - p.x) * inv_d.x;
		float t_max_y = ((cell_p.y + (step.y > 0)) * cell_size - p.y) * inv_d.y;
		float dist = 0.0f;
		while (dist < max_dist) {
			const uint32_t b = t_max_x < t_max_y;
			// Advance in grid
			dist = (b * t_max_x + (!b) * t_max_y);
			t_max_x += t_dx * b;
			t_max_y += t_dy * (!b);
			cell_p.x += step.x * b;
			cell_p.y += step.y * (!b);
			if (!checkCoords(cell_p)) {
				return intersection;
			}
			else {
				WorldCell& cell = get(cell_p);
				if (cell.wall) {
					intersection.cell = &cell;
					intersection.normal = sf::Vector2f(to<float>(b), to<float>(!b));
					intersection.distance = dist;
					return intersection;
				}
			}
		}
		return intersection;
	}
};
