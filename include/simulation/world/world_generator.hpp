#pragma once
#include <vector>
#include "world.hpp"
#include "common/number_generator.hpp"
#include "common/math.hpp"


// Procedurally generates a random world:
// - organic rock formations (no pre-dug tunnels, ants have to dig their own)
// - plants (food generators) scattered in open areas
// - open spots suited for colonies
struct WorldGenerator
{
	static constexpr uint32_t cells_per_rock_blob   = 15000; // controls rock formations density
	static constexpr uint32_t rock_walk_steps       = 20;
	static constexpr uint32_t cells_per_dirt_blob   = 3000; // standalone soft dirt veins
	static constexpr uint32_t dirt_walk_steps       = 7200;
	static constexpr int32_t  dirt_shell_thickness  = 5;    // dirt coating around rocks
	static constexpr uint32_t cells_per_food_vein   = 8000; // buried food density
	static constexpr uint32_t vein_walk_steps       = 80;   // vein length
	static constexpr int32_t  vein_radius           = 2;    // vein thickness
	static constexpr float    min_colony_spacing    = 600.0f;
	static constexpr float    colony_clearance      = 10.0f;

	static void generate(World& world)
	{
		clear(world);
		generateRocks(world);
		generateFoodVeins(world);
		generateInitialPlants(world);
	}

	// Remove every wall (except borders) and all food
	static void clear(World& world)
	{
		for (int32_t x(1); x < world.map.width - 1; ++x) {
			for (int32_t y(1); y < world.map.height - 1; ++y) {
				world.map.clearCell({x, y});
			}
		}
		world.plants.clear();
	}

	// Stamp a disc of a given wall material. Dirt never overwrites rock.
	static void stampDisc(World& world, sf::Vector2i center, int32_t radius, uint32_t material)
	{
		for (int32_t dx(-radius); dx <= radius; ++dx) {
			for (int32_t dy(-radius); dy <= radius; ++dy) {
				if (dx * dx + dy * dy > radius * radius) {
					continue;
				}
				const sf::Vector2i coord{center.x + dx, center.y + dy};
				if (coordsInside(world, coord)) {
					WorldCell& cell = world.map.get(coord);
					if (material == WorldCell::wall_dirt && cell.wall == WorldCell::wall_rock) {
						continue;
					}
					cell.wall = material;
					cell.food = 0;
				}
			}
		}
	}

	// Organic wall formations created by random walks stamping discs.
	// Rock formations get a soft dirt shell around them.
	static void stampBlob(World& world, uint32_t material, uint32_t steps)
	{
		sf::Vector2i position{
			1 + to<int32_t>(RNGf::getUnder(to<float>(world.map.width  - 2))),
			1 + to<int32_t>(RNGf::getUnder(to<float>(world.map.height - 2)))
		};
		float angle = RNGf::getUnder(2.0f * PI);
		for (uint32_t s(steps); s--;) {
			const int32_t radius = 2 + to<int32_t>(RNGf::getUnder(3.0f));
			if (material == WorldCell::wall_rock) {
				// Dirt shell first, then the rock core on top of it
				stampDisc(world, position, radius + dirt_shell_thickness, WorldCell::wall_dirt);
			}
			stampDisc(world, position, radius, material);
			// Meander
			angle += RNGf::getFullRange(0.8f);
			position.x += to<int32_t>(std::round(to<float>(radius) * cos(angle)));
			position.y += to<int32_t>(std::round(to<float>(radius) * sin(angle)));
			if (!coordsInside(world, position)) {
				break;
			}
		}
	}

	static void generateRocks(World& world)
	{
		const auto cells_count = to<uint32_t>(world.map.width * world.map.height);
		const auto blobs_count = std::max(4u, cells_count / cells_per_rock_blob);
		for (uint32_t i(blobs_count); i--;) {
			stampBlob(world, WorldCell::wall_rock, rock_walk_steps);
		}
		// Standalone soft dirt veins, easy digging shortcuts
		const auto dirt_blobs_count = std::max(4u, cells_count / cells_per_dirt_blob);
		for (uint32_t i(dirt_blobs_count); i--;) {
			stampBlob(world, WorldCell::wall_dirt, dirt_walk_steps);
		}
	}

	static void generateInitialPlants(World& world)
	{
		const uint32_t plants_count = World::max_plants_count / 2;
		for (uint32_t i(plants_count); i--;) {
			sf::Vector2i cell;
			if (world.findOpenCell(cell, 64)) {
				world.addPlant(cell);
				// Start with a small food patch around each plant
				for (uint32_t f(8); f--;) {
					const sf::Vector2i target{
						cell.x + to<int32_t>(RNGf::getFullRange(3.0f)),
						cell.y + to<int32_t>(RNGf::getFullRange(3.0f))
					};
					if (coordsInside(world, target) && !world.map.getCst(target).wall) {
						world.addFoodAt(target, 2);
					}
				}
			}
		}
	}

	// Winding food veins hidden inside rock and dirt formations, like
	// mineral veins. Ants can only reach them by digging tunnels, giving
	// them a real reason to carve through the walls.
	static void generateFoodVeins(World& world)
	{
		const auto cells_count = to<uint32_t>(world.map.width * world.map.height);
		const auto vein_count  = std::max(4u, cells_count / cells_per_food_vein);
		uint32_t created  = 0;
		uint32_t attempts = vein_count * 64;
		while (attempts-- && created < vein_count) {
			const sf::Vector2i start{
				1 + to<int32_t>(RNGf::getUnder(to<float>(world.map.width  - 2))),
				1 + to<int32_t>(RNGf::getUnder(to<float>(world.map.height - 2)))
			};
			// Veins only start fully buried inside walls
			if (!isBuried(world, start, vein_radius + 1)) {
				continue;
			}
			carveVein(world, start);
			++created;
		}
	}

	// Check that a disc around the cell is entirely made of rock
	static bool isBuried(const World& world, sf::Vector2i center, int32_t radius)
	{
		for (int32_t dx(-radius); dx <= radius; ++dx) {
			for (int32_t dy(-radius); dy <= radius; ++dy) {
				if (dx * dx + dy * dy > radius * radius) {
					continue;
				}
				const sf::Vector2i coord{center.x + dx, center.y + dy};
				if (!coordsInside(world, coord) || !world.map.getCst(coord).wall) {
					return false;
				}
			}
		}
		return true;
	}

	// Carve a meandering vein through the walls and fill it with food.
	// The vein only replaces wall cells, so it stays buried and never
	// spills food into already open areas.
	static void carveVein(World& world, sf::Vector2i position)
	{
		const sf::Vector2i start = position;
		float angle = RNGf::getUnder(2.0f * PI);
		for (uint32_t s(vein_walk_steps); s--;) {
			const int32_t radius = 1 + to<int32_t>(RNGf::getUnder(to<float>(vein_radius)));
			stampFoodDisc(world, position, radius);
			// Meander gently to keep the vein snake-like
			angle += RNGf::getFullRange(0.5f);
			position.x += to<int32_t>(std::round(to<float>(radius + 1) * cos(angle)));
			position.y += to<int32_t>(std::round(to<float>(radius + 1) * sin(angle)));
			// Stop when leaving the map or breaking out of the walls
			if (!coordsInside(world, position) || !world.map.getCst(position).wall) {
				break;
			}
		}
		// Sometimes a plant grows at the vein origin, making it a renewable prize
		if (RNGf::proba(0.25f)) {
			world.addPlant(start);
		}
	}

	// Hollow out a small disc of wall, filling the freed cells with food
	static void stampFoodDisc(World& world, sf::Vector2i center, int32_t radius)
	{
		for (int32_t dx(-radius); dx <= radius; ++dx) {
			for (int32_t dy(-radius); dy <= radius; ++dy) {
				if (dx * dx + dy * dy > radius * radius) {
					continue;
				}
				const sf::Vector2i coord{center.x + dx, center.y + dy};
				if (!coordsInside(world, coord)) {
					continue;
				}
				WorldCell& cell = world.map.get(coord);
				if (!cell.wall) {
					continue;
				}
				cell.wall     = 0;
				cell.dig_hits = 0;
				world.addFoodAt(coord, 1 + to<uint32_t>(RNGf::getUnder(2.0f)));
			}
		}
	}

	// Find open, wall free spots for colonies, far away from each other
	static std::vector<sf::Vector2f> findColonySpots(const World& world, uint32_t count)
	{
		std::vector<sf::Vector2f> spots;
		for (uint32_t i(0); i < count; ++i) {
			sf::Vector2f best_spot{world.size.x * 0.5f, world.size.y * 0.5f};
			float best_score = -1.0f;
			for (uint32_t attempt(256); attempt--;) {
				const sf::Vector2f candidate{
					RNGf::getRange(colony_clearance, world.size.x - colony_clearance),
					RNGf::getRange(colony_clearance, world.size.y - colony_clearance)
				};
				if (!isAreaOpen(world, candidate, colony_clearance)) {
					continue;
				}
				// Score by distance to the closest already placed colony
				float min_dist = world.size.x + world.size.y;
				for (const sf::Vector2f& other : spots) {
					min_dist = std::min(min_dist, getLength(candidate - other));
				}
				if (min_dist > best_score) {
					best_score = min_dist;
					best_spot  = candidate;
					if (best_score >= min_colony_spacing) {
						break;
					}
				}
			}
			spots.push_back(best_spot);
		}
		return spots;
	}

	// Check that a disc around the position is wall free
	static bool isAreaOpen(const World& world, sf::Vector2f position, float radius)
	{
		const auto cell_size   = to<int32_t>(world.map.cell_size);
		const auto cell_radius = to<int32_t>(radius) / cell_size;
		const sf::Vector2i center{to<int32_t>(position.x) / cell_size, to<int32_t>(position.y) / cell_size};
		for (int32_t dx(-cell_radius); dx <= cell_radius; ++dx) {
			for (int32_t dy(-cell_radius); dy <= cell_radius; ++dy) {
				if (dx * dx + dy * dy > cell_radius * cell_radius) {
					continue;
				}
				const sf::Vector2i coord{center.x + dx, center.y + dy};
				if (!coordsInside(world, coord) || world.map.getCst(coord).wall) {
					return false;
				}
			}
		}
		return true;
	}

	static bool coordsInside(const World& world, sf::Vector2i coord)
	{
		return coord.x > 0 && coord.x < world.map.width - 1 &&
		       coord.y > 0 && coord.y < world.map.height - 1;
	}
};
