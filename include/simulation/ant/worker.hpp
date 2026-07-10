#pragma once
#include "ant.hpp"


struct WorkerUpdater
{
	static void findMarker(Ant& ant, World& world)
	{
		// Consts
		constexpr float sample_angle_range    = PI * 0.5f;
		constexpr uint32_t sample_count       = 32u;
		constexpr float repellent_prob_factor = 0.3f;
		// Init
		SamplingResult result;
		result.max_direction      = ant.direction.getVec();
		const Mode marker_phase   = ant.getMarkersSamplingType();
		const float current_angle = getAngle(result.max_direction);

		// Sample the world
		for (uint32_t i(sample_count); i--;) {
			// Get random point in range
            const float delta_angle      = RNGf::getRange(sample_angle_range);
			const float sample_angle     = current_angle + delta_angle;
			const float distance         = RNGf::getUnder(Ant::marker_detection_max_dist);
			const sf::Vector2f to_marker = { cos(sample_angle), sin(sample_angle) };
			auto* cell                   = world.map.getSafe(ant.position + distance * to_marker);
			const HitPoint hit_result    = world.map.getFirstHit(ant.position, to_marker, distance);
			// Check cell
			if (!cell) {
				continue;
			}
			// Smell the food scent, even through walls: the scent field is
			// already attenuated by them, a faint trace hints where to dig
			if (marker_phase == Mode::ToFood && cell->food_scent > result.max_scent) {
				result.max_scent       = cell->food_scent;
				result.scent_direction = to_marker;
			}
			// Everything else requires a line of sight
			if (hit_result.cell) {
				continue;
			}
			// Check for food or colony
			if ((cell->isPermanent(ant.col_id) && marker_phase == Mode::ToHome) || (marker_phase == Mode::ToFood && cell->food)) {
				result.max_direction = to_marker;
				result.found_permanent = true;
				break;
			}
			// Check for enemy
			if (cell->checkEnemyPresence(ant.col_id)) {
				ant.detectEnemy();
			}
			// Help in case of a fight
			if (cell->checkFight(ant.col_id)) {
				result.found_fight = true;
				result.max_direction = to_marker;
				ant.detectEnemy();
				break;
			}
			// Flee if repellent
			if (cell->getRepellent(ant.col_id) > result.max_repellent) {
				result.max_repellent = cell->getRepellent(ant.col_id);
				result.repellent_cell = cell;
			}
			// Check for the most intense marker
            const float wall_rep = cell->wall_dist * cell->wall_dist;
			const auto marker_intensity = to<float>(cell->getIntensity(marker_phase, ant.col_id)) * wall_rep;
			if (marker_intensity > result.max_intensity) {
				result.max_intensity = marker_intensity;
				result.max_direction = to_marker;
				result.max_cell = cell;
			}
			// Eventually choose a different path
			if (RNGf::proba(ant.liberty_coef)) {
				break;
			}
		}

        if (result.found_fight) {
			ant.direction = getAngle(result.max_direction);
			ant.fight_mode = FightMode::ToFight;
			return;
		}
		// Check for repellent
		if (ant.phase == Mode::ToFood && result.max_repellent && !result.found_permanent) {
			if (RNGf::proba(repellent_prob_factor * (1.0f - result.max_intensity / to<float>(Conf::MARKER_INTENSITY)))) {
				ant.direction.addNow(RNGf::getUnder(2.0f * PI));
				// This cooldown prevent from searching markers when fleeing
				ant.search_markers.reset();
				return;
			}
		}
		// Degrade repellent if still food
		if (result.repellent_cell && ant.phase == Mode::ToHome) {
			result.repellent_cell->getRepellent(ant.col_id) *= 0.95f;
		}

        // Update direction: blend the pheromone track direction with the
        // pull of the food smell. The scent constantly bends the path of
        // food searching ants toward it, stronger the closer the food is.
		constexpr float scent_attraction = 2.0f;
		constexpr float scent_threshold  = 0.05f;
		sf::Vector2f steering{0.0f, 0.0f};
		if (result.max_intensity) {
			// Slowly degrade the track to accelerate its dissipation
			if (RNGf::proba(0.2f) && ant.phase == Mode::ToFood) {
				result.max_cell->degrade(ant.col_id, ant.phase, 0.99f);
			}
			steering = result.max_direction;
		}
		if (!result.found_permanent && ant.phase == Mode::ToFood && result.max_scent > scent_threshold) {
			// Both directions are unit vectors: near food the smell overrides
			// the markers, far away it only gently bends the trajectory.
			// Walls don't stop it, ants will dig toward the smell.
			steering += (scent_attraction * result.max_scent) * result.scent_direction;
		}
		if (steering.x != 0.0f || steering.y != 0.0f) {
			ant.direction = getAngle(steering);
		}
	}

	static void update(Ant& ant, World& world, float dt)
	{
		// Check collision with food
		if (ant.phase == Mode::ToFood) {
			ant.checkFood(world);
		}
		// Get current cell
		WorldCell& cell = world.map.get(ant.position);
		cell.addPresence();
		ant.search_markers.update(dt);
		if (ant.direction_update.updateAutoReset(dt)) {
			if (ant.search_markers.ready()) {
				findMarker(ant, world);
				ant.direction += RNGf::getFullRange(ant.direction_noise_range);
			}
			else {
				// Fleeing from repellent
				cell.degrade(ant.col_id, Mode::ToFood, 0.25f);
				ant.direction += RNGf::getFullRange(2.0f * ant.direction_noise_range);
			}
		}
		// Add marker
		if (ant.marker_add.updateAutoReset(dt)) {
			ant.addMarker(world);
		}
	}
};
