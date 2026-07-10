#pragma once

#include <list>
#include "simulation/world/world.hpp"
#include "simulation/config.hpp"
#include "common/direction.hpp"
#include "common/number_generator.hpp"
#include "ant_mode.hpp"
#include "common/cooldown.hpp"
#include "simulation/colony/colony_base.hpp"
#include "common/index_vector.hpp"


struct SamplingResult
{
	float max_intensity         = 0.0f;
	// To objective stuff
	sf::Vector2f max_direction  = {0.0f, 0.0f};
	WorldCell* max_cell         = nullptr;
	bool found_permanent        = false;
	bool found_fight            = false;
	// Repellent stuff
	float max_repellent         = 0.0f;
	WorldCell* repellent_cell   = nullptr;
	// Food scent stuff (sensed even through walls)
	float max_scent             = 0.0f;
	sf::Vector2f scent_direction = {0.0f, 0.0f};
};


struct Ant
{
	enum class Type
	{
		Worker,
		Soldier,
		Queen
	};

	// Parameters
	static constexpr float move_speed                = 40.0f;
	static constexpr float marker_detection_max_dist = 40.0f;
	static constexpr float direction_update_period   = 0.25f;
	static constexpr float marker_period             = 0.25f;
	static constexpr float direction_noise_range     = PI * 0.02f;
	static constexpr float repellent_period          = 128.0f;
	// Probability for a worker to dig a wall cell when bumping into it
	static constexpr float tunnel_dig_proba          = 0.2f;
	// Tunnel digging AI: once an ant decides to dig it commits to a
	// direction and carves a straight tunnel several cells long
	static constexpr float   dig_hit_period      = 0.15f;
	static constexpr float   scent_dig_threshold = 0.03f;
	static constexpr uint8_t tunnel_min_cells    = 3;
	static constexpr uint8_t tunnel_max_cells    = 8;

    float width           = 3.0f;
    float length          = 4.7f;
	Mode phase            = Mode::ToFood;
    uint16_t hits         = 0;
	sf::Vector2f position;
	Direction direction;

	// Fight info
	FightMode fight_mode    = FightMode::NoFight;
	float damage            = 10.0f;
    float fight_dist        = length * 0.25f;
    civ::Ref<Ant> target;
    sf::Vector2f fight_pos;
    sf::Vector2f fight_vec;
    bool enemy_found        = false;
	float enemy_intensity   = 0.0f;
    float to_fight_timeout  = 1.0f;
    float to_fight_time     = 0.0f;
	AntRef fight_request;

    Cooldown attack_cooldown;
    Cooldown direction_update;
	Cooldown marker_add;
	Cooldown search_markers;
	float internal_clock         = 0.0f;
	float to_enemy_markers_count = 0.0f;
    float max_autonomy           = 300.0f;
    float liberty_coef           = 0.0f;
	float autonomy               = 0.0f;
	// Life time (ants die of old age)
	float age                    = 0.0f;
	float max_lifetime           = 700.0f;

	int16_t id     = 0;
	uint8_t col_id = 0;
	Type type      = Type::Worker;

	// War / raid info
	sf::Vector2f home_position;
	sf::Vector2f raid_target;
	bool has_grub        = false;
	bool raid_returning  = false;
	// Last enemy colony this ant fought against
	uint8_t last_enemy_col = 255;

	// Tunnel digging state (committed dig direction and remaining cells)
	sf::Vector2f dig_direction    = {0.0f, 0.0f};
	uint8_t      dig_cells_left   = 0;
	float        dig_free_time    = 0.0f;
	Cooldown     dig_hit_cooldown = Cooldown(dig_hit_period);

	Ant() = default;
	Ant(float x, float y, float angle, uint8_t colony_id)
		: position(x, y)
		, home_position(x, y)
		, direction(angle)
		, direction_update(direction_update_period, RNGf::getUnder(1.0f) * direction_update_period)
		, marker_add(marker_period, RNGf::getUnder(1.0f) * marker_period)
		, search_markers(5.0f, 5.0f)
		, phase(Mode::ToFood)
		, liberty_coef(RNGf::getRange(0.001f, 0.01f))
        , fight_mode(FightMode::NoFight)
		, col_id(colony_id)
        , attack_cooldown(1.5f, 0.0f)
		, type(Type::Worker)
	{
		max_lifetime = RNGf::getRange(600.0f, 900.0f);
	}
    
    void addToWorldGrid(World& world)
    {
		WorldCell* cell = world.map.getSafe(position);
		if (!cell) {
			// Set as dead if outside the world
			terminate();
			return;
		}

		ColonyCell& colony_cell = cell->markers[col_id];
        if (!colony_cell.fighting) {
			colony_cell.current_ant = id;
			colony_cell.fighting = isFighting();
        }
    }

	void removeFromWorldGrid(World& world) const
	{
		ColonyCell& cell = world.map.get(position).markers[col_id];
		if (cell.current_ant == id) {
			cell.current_ant = -1;
		}
	}

    [[nodiscard]]
    bool isFighting() const
    {
        return fight_mode == FightMode::Fighting;
    }

	void attack(float dt)
	{
		if (target) {
            Ant& opponent = *target;
            position = fight_pos - fight_vec * (0.5f * length + (attack_cooldown.getRatio()) * fight_dist);
            attack_cooldown.update(dt);
            if (attack_cooldown.ready()) {
                attack_cooldown.reset();
                opponent.autonomy += damage;
            }
		} else {
			fight_mode = FightMode::NoFight;
			if (type == Type::Soldier) {
				// Restore some energy
				autonomy = std::max(0.0f, autonomy - 3.0f);
			}
		}
	}

	void updatePosition(World& world, float dt)
	{
		sf::Vector2f v = direction.getVec();
		const HitPoint hit = world.map.getFirstHit(position, v, dt * move_speed);
		if (hit.cell) {
			if (canDig() && updateDigging(world, *hit.cell, v, dt)) {
				// The ant is busy carving its tunnel
				return;
			}
			const uint32_t hits_threshold = 4;
			if (hits > hits_threshold) {
				terminate();
			}
			else {
				v.x *= hit.normal.x != 0.0f ? -1.0f : 1.0f;
				v.y *= hit.normal.y != 0.0f ? -1.0f : 1.0f;
			}
			++hits;
			direction.setDirectionNow(v);
		}
		else {
			hits = 0;
			// Committed diggers keep their tunnel straight
			if (dig_cells_left) {
				direction.setDirectionNow(dig_direction);
				v = dig_direction;
				dig_free_time += dt;
				// Broke into open space: the tunnel is done
				if (dig_free_time > 0.4f) {
					dig_cells_left = 0;
				}
			}
			position += (dt * move_speed) * v;
			// Ants outside the map go back to home
			if (position.x < 0.0f || position.x > to<float>(Conf::WORLD_WIDTH) ||
			    position.y < 0.0f || position.y > to<float>(Conf::WORLD_HEIGHT)) {
				terminate();
			}
		}
	}

	[[nodiscard]]
	bool canDig() const
	{
		return type == Type::Worker || phase == Mode::Raid;
	}

	void cancelDigging()
	{
		dig_cells_left = 0;
		dig_free_time  = 0.0f;
	}

	// Where would this ant like to dig, and how badly? Returns a motivation
	// factor, 0 meaning "no reason to dig here"
	float getDigMotivation(World& world, sf::Vector2f move_dir, sf::Vector2f& dig_dir) const
	{
		// Raiders tunnel straight toward their objective
		if (phase == Mode::Raid) {
			const sf::Vector2f to_target = (raid_returning ? home_position : raid_target) - position;
			const float dist = getLength(to_target);
			if (dist < 1.0f) {
				return 0.0f;
			}
			dig_dir = to_target / dist;
			return 2.0f;
		}
		// Loaded or tired ants dig shortcuts straight toward the nest,
		// but only if the wall actually blocks the way home
		if (phase == Mode::ToHome || phase == Mode::ToHomeNoFood || phase == Mode::Refill) {
			const sf::Vector2f to_home = home_position - position;
			const float dist = getLength(to_home);
			if (dist < 1.0f) {
				return 0.0f;
			}
			dig_dir = to_home / dist;
			return std::max(0.0f, dot(dig_dir, move_dir));
		}
		// Food searching ants dig toward a growing food smell
		const float probe_dist   = 3.0f * to<float>(world.map.cell_size);
		const WorldCell* here    = world.map.getSafe(position);
		const WorldCell* probe   = world.map.getSafe(position + probe_dist * move_dir);
		if (here && probe && probe->food_scent > scent_dig_threshold &&
		    probe->food_scent > 1.05f * here->food_scent) {
			dig_dir = move_dir;
			return 0.5f + 4.0f * probe->food_scent;
		}
		// No goal behind this wall: rarely dig an exploration tunnel
		dig_dir = move_dir;
		return 0.1f;
	}

	// Digging logic when facing a wall. Returns true if the ant handled the
	// hit by digging (or by waiting in front of its tunnel face)
	bool updateDigging(World& world, WorldCell& cell, sf::Vector2f move_dir, float dt)
	{
		// Never dig the indestructible map border
		if (cell.indestructible) {
			cancelDigging();
			return false;
		}
		// Continue an ongoing tunnel
		if (dig_cells_left) {
			direction.setDirectionNow(dig_direction);
			dig_free_time = 0.0f;
			if (dig_hit_cooldown.updateAutoReset(dt) && world.digWall(cell)) {
				--dig_cells_left;
			}
			hits = 0;
			return true;
		}
		// Decide whether to start a new tunnel
		sf::Vector2f dig_dir = move_dir;
		float motivation = getDigMotivation(world, move_dir, dig_dir);
		// Soft dirt is much more inviting than rock
		if (cell.wall == WorldCell::wall_dirt) {
			motivation *= 2.5f;
		}
		if (motivation > 0.0f && RNGf::proba(std::min(1.0f, tunnel_dig_proba * motivation))) {
			dig_direction  = dig_dir;
			dig_cells_left = to<uint8_t>(tunnel_min_cells + RNGf::getUintUnder(tunnel_max_cells - tunnel_min_cells));
			dig_free_time  = 0.0f;
			dig_hit_cooldown.reset();
			direction.setDirectionNow(dig_direction);
			hits = 0;
			return true;
		}
		return false;
	}

	void checkFood(World& world)
	{
		if (world.map.isOnFood(position)) {
			phase = Mode::ToHome;
			cancelDigging();
			direction.addNow(PI);
			autonomy = 0.0f;
			internal_clock = 0.0f;
			if (world.map.pickFood(position)) {
				phase = Mode::ToHomeNoFood;
				marker_add.target = repellent_period;
				marker_add.value = RNGf::getUnder(marker_add.target);
				// Add a repellent for 300s
				world.addMarkerRepellent(position, col_id, 300.0f);
			}
		}
	}

	void checkColony(ColonyBase& base)
	{
		if (getLength(position - base.position) < base.radius) {
			cancelDigging();
			marker_add.target = marker_period;
			if (phase == Mode::ToHome || phase == Mode::ToHomeNoFood) {
				base.addFood(1.0f);
				direction.addNow(PI);
                base.enemies_found_count += enemy_found;
			}
			// Deliver a stolen larva, the colony will raise it as its own
			if (has_grub) {
				has_grub = false;
				++base.stolen_grubs;
				direction.addNow(PI);
			}
			// Report who we fought, feeding the colony's war planning
			if (enemy_found && last_enemy_col < Conf::MAX_COLONIES_COUNT) {
				++base.enemy_contacts[last_enemy_col];
			}
			// Refill
			if (!isFighting()) {
				autonomy = 0.0f;
			}
			enemy_intensity = 0.0f;
			resetMarkers();
			enemy_found = false;
			raid_returning = false;
			if (type == Type::Soldier) {
				if (base.atWar()) {
					// The colony is at war: join the raid on the enemy nest
					phase = Mode::Raid;
					raid_target = base.war_target_pos;
				}
				else {
					phase = Mode::ToEnemy;
				}
			}
			else if (type == Type::Worker) {
				phase = Mode::ToFood;
			}
		}
	}

	void updateClocks(float dt)
	{
		autonomy += dt;
		age += dt;
		internal_clock += dt;
		to_enemy_markers_count += dt;
	}

    [[nodiscard]]
	Mode getMarkersSamplingType() const
	{
		if (phase == Mode::ToHome || phase == Mode::Refill || phase == Mode::ToHomeNoFood) {
			return Mode::ToHome;
		}
		if (phase == Mode::Raid) {
			return Mode::ToEnemy;
		}
		return phase;
	}

	void resetMarkers()
	{
		internal_clock = 0.0f;
		to_enemy_markers_count = 0.0f;
	}

	void addMarker(World& world) const
	{
		if (phase == Mode::ToHome || phase == Mode::ToFood) {
			const float intensity = getMarkerIntensity(0.05f, internal_clock);
			world.addMarker(position, phase == Mode::ToFood ? Mode::ToHome : Mode::ToFood, intensity, col_id);
		}
		else if (phase == Mode::ToHomeNoFood) {
			const auto intensity = to<float>(getMarkerIntensity(0.1f, internal_clock));
			world.addMarkerRepellent(position, col_id, intensity);
		}
		if (enemy_found) {
			// If enemy found add ToEnemy markers
			const float intensity = std::min(0.1f, enemy_intensity) * getMarkerIntensity(0.05f, to_enemy_markers_count);
			world.addMarker(position, Mode::ToEnemy, intensity, col_id);
		}
	}

	void render_food(sf::VertexArray& va, const uint32_t index) const
	{
		constexpr float radius = 2.0f;
		sf::Vector2f food_pos(-10000.0f, -10000.0f);
		if (phase == Mode::ToHome || phase == Mode::ToHomeNoFood || has_grub) {
			food_pos = position + length * 0.65f * direction.getVec();
		}

		va[index + 0].position = sf::Vector2f(food_pos.x - radius, food_pos.y - radius);
		va[index + 1].position = sf::Vector2f(food_pos.x + radius, food_pos.y - radius);
		va[index + 2].position = sf::Vector2f(food_pos.x + radius, food_pos.y + radius);
		va[index + 3].position = sf::Vector2f(food_pos.x - radius, food_pos.y + radius);
	}

	void render_in(sf::VertexArray& va, const uint32_t index) const
	{
        const float size_ratio = width / length;
		const sf::Vector2f dir_vec(direction.getVec() * length);
		const sf::Vector2f nrm_vec(-dir_vec.y * size_ratio, dir_vec.x * size_ratio);

		va[index + 0].position = position - nrm_vec + dir_vec;
		va[index + 1].position = position + nrm_vec + dir_vec;
		va[index + 2].position = position + nrm_vec - dir_vec;
		va[index + 3].position = position - nrm_vec - dir_vec;
	}

    [[nodiscard]]
	static float getMarkerIntensity(float coef, float count)
	{
		return Conf::MARKER_INTENSITY * expf(-coef * count);
	}

	void setTarget(civ::Ref<Ant> new_target)
	{
		// Disable fight request
		fight_request.active = false;
		// Set fight parameters
		fight_mode = FightMode::Fighting;
		target = new_target;
		last_enemy_col = new_target->col_id;
        fight_pos = 0.5f * (target->position + position);
        fight_vec = getNormalized(target->position - position);
		direction = getAngle(fight_vec);
		enemy_found = true;
	}
    
    void kill(World& world)
    {
        if (phase == Mode::ToHome || phase == Mode::ToHomeNoFood) {
            world.addFoodAt(position.x, position.y, 1);
        }
        // A dropped stolen larva becomes food
        if (has_grub) {
            world.addFoodAt(position.x, position.y, 1);
        }
        // A dead queen is a big meal
        if (type == Type::Queen) {
            world.addFoodAt(position.x, position.y, 20);
        }
        phase = Mode::Dead;
		removeFromWorldGrid(world);
    }

	void detectEnemy()
	{
		enemy_found = true;
		to_enemy_markers_count = 0.0f;
		to_fight_time = 0.0f;
		enemy_intensity += 0.001f;
	}

	void request_fight(AntRef ref)
	{
		fight_mode = FightMode::ToFight;
		fight_request = ref;
		last_enemy_col = ref.col_id;
	}

	void terminate()
	{
		autonomy = max_autonomy + 1.0f;
	}

    [[nodiscard]]
	bool isDone() const
	{
		return autonomy >= max_autonomy || age >= max_lifetime;
	}

    [[nodiscard]]
	bool isDead() const
	{
		return phase == Mode::Dead;
	}
};
