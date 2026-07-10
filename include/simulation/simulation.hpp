#pragma once
#include <iostream>
#include <iomanip>
#include "world/world.hpp"
#include "colony/colony.hpp"
#include "config.hpp"
#include "common/viewport_handler.hpp"
#include "common/event_manager.hpp"
#include "event_state.hpp"
#include "render/renderer.hpp"
#include "simulation/world/map_loader.hpp"
#include "simulation/world/world_generator.hpp"
#include "simulation/ant/fight_system.hpp"
#include "simulation/war_system.hpp"
#include "world/async_distance_field_builder.hpp"
#include "common/thread_pool.hpp"


struct Simulation
{
	civ::Vector<Colony> colonies;
	World world;
	// Render
	Renderer renderer;
	EventSate ev_state;
	FightSystem fight_system;
	WarSystem war_system;
	sf::Clock clock;
    AsyncDistanceFieldBuilder distance_field_builder;
	// Throttles distance field updates triggered by ants digging tunnels
	Cooldown tunnel_update_cooldown = Cooldown(2.0f);
	// Workers used to parallelize per ant updates
	tp::ThreadPool thread_pool;
	// Periodic console stats logging (always active, independent of UI)
	float    sim_time           = 0.0f;
	Cooldown stats_log_cooldown = Cooldown(5.0f);

    explicit
	Simulation(sf::Window& window)
		: world(Conf::WORLD_WIDTH, Conf::WORLD_HEIGHT)
		, renderer()
        , distance_field_builder(world.map)
	{
        distance_field_builder.requestUpdate();
	}

	void loadMap(const std::string& map_filename)
	{
		MapLoader::loadMap(world, map_filename);
        distance_field_builder.requestUpdate();
	}

	// Procedurally generate a random world (rocks to dig through + plants)
	void generateWorld()
	{
		WorldGenerator::generate(world);
		distance_field_builder.requestUpdate();
	}

	civ::Ref<Colony> createColony(float colony_x, float colony_y)
	{
		// Create the colony object
		const civ::ID colony_id = colonies.emplace_back(colony_x, colony_y, Conf::ANTS_COUNT);
		auto colony_ref = colonies.getRef(colony_id);
        Colony& colony = *colony_ref;
        colony.initialize(to<uint8_t>(colony_id));
		// Create colony markers
        createColonyMarkers(colony);
		// Register it for the renderer
		renderer.addColony(colony_ref);
        world.renderer.colonies_color.emplace_back();
        return colony_ref;
	}

	void update(float dt)
	{
		if (!ev_state.pause) {
            // Mark ants with no more time left as dead
            removeDeadAnts();
			// Update world cells (markers, density, walls)
			world.update(dt);
			// First perform position update and grid registration
            for (Colony& colony : colonies) {
                if (colony.color_changed) {
                    world.renderer.colonies_color[colony.id] = colony.ants_color;
                }
                if (colony.position_changed) {
                    updateColonyPosition(colony);
                }
            }
			for (Colony& colony : colonies) {
				colony.genericAntsUpdate(dt, world, thread_pool);
			}
			// Then update objectives and world sampling
			for (Colony& colony : colonies) {
				colony.update(dt, world, thread_pool);
			}
			// Search for fights
			fight_system.checkForFights(colonies, world);
			// Update wars between colonies (declarations, raids, larva theft)
			war_system.update(colonies, dt);
			// Update distance field if ants dug new tunnels
			tunnel_update_cooldown.update(dt);
			if (world.walls_changed && tunnel_update_cooldown.ready()) {
				tunnel_update_cooldown.reset();
				world.walls_changed = false;
				distance_field_builder.requestUpdate();
			}
			// Update stats
			renderer.updateColoniesStats(dt);
			// Always log stats to the console
			sim_time += dt;
			if (stats_log_cooldown.updateAutoReset(dt)) {
				logStats();
			}
		}
	}

	// Prints a full simulation report: world food, plants, and for each
	// colony its population breakdown, food stock/income and war status
	void logStats() const
	{
		// World food: exposed (reachable) vs still buried in walls
		uint64_t food_exposed = 0;
		uint64_t food_buried  = 0;
		for (const WorldCell& cell : world.map.cells) {
			if (cell.wall) {
				food_buried += cell.food;
			} else {
				food_exposed += cell.food;
			}
		}
		std::cout << "===== [t=" << std::fixed << std::setprecision(0) << sim_time << "s] "
		          << "colonies: " << colonies.size()
		          << " | world food: " << food_exposed << " exposed, " << food_buried << " buried"
		          << " | plants: " << world.plants.size() << " =====" << std::endl;
		for (const Colony& colony : colonies) {
			const uint32_t total    = to<uint32_t>(colony.ants.size());
			const uint32_t soldiers = colony.soldiersCount();
			const uint32_t queens   = colony.queen_alive ? 1 : 0;
			const uint32_t workers  = total - soldiers - queens;
			std::cout << "  Colony " << to<uint32_t>(colony.id)
			          << " | ants: " << total
			          << " (workers: " << workers
			          << ", soldiers: " << soldiers
			          << ", queen: " << (colony.queen_alive ? "alive" : "DEAD") << ")"
			          << " | grubs: " << colony.grubs.size()
			          << " | food: " << std::setprecision(1) << colony.base.food
			          << "/" << std::setprecision(0) << colony.base.max_food
			          << " (income: " << std::setprecision(2) << colony.base.food_acc_mean.get() << ")";
			if (colony.base.atWar()) {
				std::cout << " | AT WAR with colony " << to<int32_t>(colony.base.war_target)
				          << " (" << std::setprecision(0) << colony.base.war_time_left << "s left)";
			}
			if (colony.base.stolen_grubs) {
				std::cout << " | stolen grubs: " << colony.base.stolen_grubs;
			}
			std::cout << std::endl;
		}
	}
    
    void removeDeadAnts()
    {
        // Mark old ants as dead
        for (Colony& colony : colonies) {
            const uint32_t killed = colony.killWeakAnts(world);
            if (killed) {
                const uint32_t initial_size = to<int32_t>(colony.ants.size());
                renderer.colonies[colony.id].cleanVAs(initial_size - killed, initial_size);
            }
        }
        // Remove them
        for (Colony& colony : colonies) {
            colony.removeDeadAnts();
        }
    }

    void updateColonyPosition(Colony& colony)
    {
        colony.position_changed = false;
        world.clearMarkers(colony.id);
        // Create colony markers
        createColonyMarkers(colony);
    }

    void createColonyMarkers(Colony& colony)
    {
        for (uint32_t i(0); i < 64; ++i) {
            float angle = float(i) / 64.0f * (2.0f * PI);
            world.addMarker(colony.base.position + 0.9f * colony.base.radius * sf::Vector2f(cos(angle), sin(angle)), Mode::ToHome, 10.0f, true);
        }
    }

	void render(sf::RenderTarget& target)
	{
		renderer.render(world, target);
	}

    void removeColony(uint8_t colony_id)
    {
        for (Colony& c : colonies) {
            c.stopFightsWith(colony_id);
            // End wars against the removed colony
            if (c.base.war_target == to<int8_t>(colony_id)) {
                WarSystem::endWar(c);
            }
            c.base.enemy_contacts[colony_id] = 0;
        }
        colonies.erase(colony_id);
        renderer.colonies.erase(colony_id);
        world.renderer.colonies_color.erase(colony_id);
        world.clearMarkers(colony_id);
    }
};
