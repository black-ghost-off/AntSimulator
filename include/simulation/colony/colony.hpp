#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <list>
#include <algorithm>
#include "common/utils.hpp"
#include "colony_base.hpp"
#include "grub.hpp"
#include "common/graph.hpp"
#include "common/racc.hpp"
#include "common/index_vector.hpp"
#include "common/thread_pool.hpp"
#include "simulation/ant/ant_updater.hpp"


struct Colony
{
	ColonyBase       base;
	uint32_t         max_ants_count;
	civ::Vector<Ant> ants;
	std::vector<Grub> grubs;
	bool             queen_alive = false;
	sf::Vector2f     queen_position;
	Cooldown         ants_creation_cooldown;
	Cooldown         grub_feed_cooldown;
	Cooldown         pop_diff_update;
	RDiff<int64_t>   pop_diff;
	uint8_t          id;
	sf::Color        ants_color       = sf::Color::White;
	uint64_t         ant_creation_id  = 0;
    bool             color_changed    = false;
    bool             position_changed = false;

	// Queen / grubs parameters
	static constexpr float    egg_cost              = 0.25f;
	static constexpr float    new_queen_cost        = 150.0f;
	static constexpr float    grub_meal_cost        = 0.1f;
	static constexpr float    grub_maturation_time  = 20.0f;
	static constexpr float    queen_maturation_time = 30.0f;
	static constexpr float    queen_move_speed      = 6.0f;
	static constexpr uint32_t max_grubs_count       = 256;


    Colony() = default;

	Colony(float x, float y, uint32_t n)
		: base(sf::Vector2f(x, y), 20.0f)
		, max_ants_count(n)
		, ants_creation_cooldown(0.125f)
		, grub_feed_cooldown(1.0f)
		, pop_diff_update(1.0f)
		, pop_diff(60)
		, id(0)
	{

	}

    void initialize(uint8_t colony_id)
    {
        id = colony_id;
        // Some starting food so the queen can lay eggs right away
        base.food = 100.0f;
        // Start small: the colony has to grow through the queen and its grubs
        const uint32_t ants_count = std::min(150u, max_ants_count > 1 ? max_ants_count - 1 : 0u);
        for (uint32_t i(ants_count); i--;) {
            createWorker();
        }
        // Every colony starts with its queen
        makeQueen(createWorker());
    }

    void setPosition(sf::Vector2f new_position)
    {
        position_changed = true;
        base.position = new_position;
        for (Ant& a : ants) {
            a.position = new_position;
            a.home_position = new_position;
        }
        for (Grub& g : grubs) {
            g.position = getGrubPosition();
        }
    }

	Ant& createWorker()
	{
		++ant_creation_id;
		const uint64_t ant_id = ants.emplace_back(base.position.x, base.position.y, RNGf::getUnder(2.0f * PI), id);
		Ant& ant = ants[ant_id];
		ant.id = to<uint16_t>(ant_id);
		ant.type = Ant::Type::Worker;
		return ant;
	}

	void specializeSoldier(Ant& ant)
	{
		if (base.enemies_found_count) {
			--base.enemies_found_count;
		}
		const float soldier_scale = 2.0f;
		ant.type                  = Ant::Type::Soldier;
		ant.length                *= soldier_scale;
		ant.width                 *= soldier_scale;
		ant.damage                *= soldier_scale * 2.0f;
		ant.max_autonomy          *= soldier_scale;
		ant.max_lifetime          *= 1.5f;
	}

	void makeQueen(Ant& ant)
	{
		const float queen_scale = 3.0f;
		ant.type          = Ant::Type::Queen;
		ant.length        *= queen_scale;
		ant.width         *= queen_scale;
		ant.damage        *= queen_scale;
		ant.max_autonomy  *= queen_scale;
		ant.max_lifetime  = RNGf::getRange(2000.0f, 3000.0f);
		ant.position      = base.position;
		queen_alive       = true;
		queen_position    = ant.position;
	}

    [[nodiscard]]
	sf::Vector2f getGrubPosition() const
	{
		const float angle = RNGf::getUnder(2.0f * PI);
		const float dist  = RNGf::getUnder(0.75f * base.radius);
		return base.position + dist * sf::Vector2f(cos(angle), sin(angle));
	}

	void genericAntsUpdate(float dt, World& world, tp::ThreadPool& thread_pool)
	{
		thread_pool.dispatch(to<uint32_t>(ants.size()), [&](uint32_t start, uint32_t end) {
			for (uint32_t i{start}; i < end; ++i) {
				AntUpdater::initialUpdate(ants.getDataAt(i), world, dt);
			}
		});
	}

    [[nodiscard]]
	bool mustCreateSoldier() const
	{
		const uint32_t soldiers_creation_discard = 5;
		// A colony at war turns most of its brood into soldiers
		if (base.atWar()) {
			return ant_creation_id % 2 == 0;
		}
		return base.enemies_found_count && (ant_creation_id % soldiers_creation_discard == 0);
	}

    [[nodiscard]]
	bool isNotFull() const
	{
		return ants.size() < max_ants_count;
	}

    [[nodiscard]]
	bool hasQueenGrub() const
	{
		return std::any_of(grubs.begin(), grubs.end(), [](const Grub& g) { return g.type == Ant::Type::Queen; });
	}

	// The queen lays eggs (grubs), if she is dead the colony tries to raise a new one
	void layEggs(float dt)
	{
		if (!ants_creation_cooldown.updateAutoReset(dt)) {
			return;
		}
		if (queen_alive) {
			if (grubs.size() < max_grubs_count && isNotFull()) {
				++ant_creation_id;
				const bool soldier = mustCreateSoldier();
				const float cost   = soldier ? 3.0f * egg_cost : egg_cost;
				if (base.useFood(cost)) {
					grubs.emplace_back(getGrubPosition(),
					                   soldier ? Ant::Type::Soldier : Ant::Type::Worker,
					                   grub_maturation_time);
				}
			}
		}
		else if (!hasQueenGrub() && base.useFood(new_queen_cost)) {
			// No queen anymore, raise a new one from a royal grub
			grubs.emplace_back(getGrubPosition(), Ant::Type::Queen, queen_maturation_time);
		}
	}

	// Grubs eat from the colony stock, grow, and finally turn into ants
	void updateGrubs(float dt)
	{
		// Larvae stolen from enemy nests are raised as this colony's own workers
		while (base.stolen_grubs && grubs.size() < max_grubs_count) {
			--base.stolen_grubs;
			grubs.emplace_back(getGrubPosition(), Ant::Type::Worker, grub_maturation_time);
		}
		const bool feed_time = grub_feed_cooldown.updateAutoReset(dt);
		for (Grub& g : grubs) {
			g.age += dt;
			if (feed_time && !base.useFood(grub_meal_cost)) {
				g.starve();
			}
			if (!g.dead && g.isMature() && isNotFull()) {
				Ant& ant = createWorker();
				ant.position = g.position;
				if (g.type == Ant::Type::Soldier) {
					specializeSoldier(ant);
				}
				else if (g.type == Ant::Type::Queen) {
					makeQueen(ant);
				}
				g.dead = true;
			}
		}
		grubs.erase(std::remove_if(grubs.begin(), grubs.end(), [](const Grub& g) { return g.dead; }), grubs.end());
	}

	// The queen stays inside the nest, slowly wandering around and defending herself if attacked
	void updateQueen(Ant& ant, float dt)
	{
		if (ant.isFighting()) {
			ant.attack(dt);
			return;
		}
		if (ant.direction_update.updateAutoReset(dt)) {
			ant.direction += RNGf::getFullRange(1.0f);
		}
		const sf::Vector2f to_center = base.position - ant.position;
		const float dist = getLength(to_center);
		if (dist > 0.5f * base.radius) {
			ant.direction.setDirectionNow(to_center / dist);
		}
		ant.position += (dt * queen_move_speed) * ant.direction.getVec();
	}

	void update(float dt, World& world, tp::ThreadPool& thread_pool)
	{
		// Update stats
		if (pop_diff_update.updateAutoReset(dt)) {
 			pop_diff.addValue(to<int64_t>(ants.size()));
		}
		layEggs(dt);
		updateGrubs(dt);
		// Update ants in parallel: each ant only writes to its own state and
		// to world cells, writes to shared colony state happen in the serial
		// pass below
		thread_pool.dispatch(to<uint32_t>(ants.size()), [&](uint32_t start, uint32_t end) {
			for (uint32_t i{start}; i < end; ++i) {
				Ant& ant = ants.getDataAt(i);
				if (ant.type == Ant::Type::Queen) {
					updateQueen(ant, dt);
				}
				else {
					AntUpdater::update(ant, world, dt);
				}
			}
		});
		// Serial pass: colony interactions (food deposit, war reports, ...)
		bool queen_found = false;
		for (Ant& ant : ants) {
			if (ant.type == Ant::Type::Queen) {
				queen_found    = true;
				queen_position = ant.position;
			}
			ant.checkColony(base);
		}
		queen_alive = queen_found;
	}

	void removeDeadAnts()
	{
		std::list<int16_t> to_remove;
		for (Ant& a : ants) {
			if (a.isDead()) {
				to_remove.push_back(a.id);
			}
		}
		for (uint64_t ant_id : to_remove) {
			ants.erase(ant_id);
		}
	}
    
    uint32_t killWeakAnts(World& world)
    {
        uint32_t count = 0;
        for (Ant& a : ants) {
            if (a.isDone()) {
                a.kill(world);
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]]
	uint32_t soldiersCount() const
	{
		return to<uint32_t>(std::count_if(ants.begin(), ants.end(), [](const Ant& a) { return a.type == Ant::Type::Soldier; }));
	}

    void setColor(sf::Color color)
    {
        ants_color = color;
        color_changed = true;
    }

    void stopFightsWith(uint8_t colony_id)
    {
        for (Ant& a : ants) {
            if (a.target) {
                a.fight_mode = FightMode::NoFight;
                if (a.target->col_id == colony_id) {
                    a.target = {};
                }
            }
        }
    }
};
