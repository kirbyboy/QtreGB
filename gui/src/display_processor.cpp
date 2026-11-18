#include "display_processor.h"

#include <algorithm>
#include <cmath>

DisplayProcessor::DisplayProcessor() = default;

void DisplayProcessor::reset()
{
	m_initialized = false;
	m_last_native.clear();
	m_levels.clear();
	m_origins.clear();
	m_elapsed_ms.clear();
	m_durations_ms.clear();
	m_targets.clear();
	m_pixels.clear();
}

void DisplayProcessor::set_settings(const DisplaySettings& settings)
{
	const bool ghosting_was_enabled = m_settings.ghosting;
	m_settings = settings;
	update_palette();

	if(ghosting_was_enabled && !m_settings.ghosting &&
	   !m_last_native.empty() && m_levels.size() == m_last_native.size())
	{
		// Turning ghosting off should show the target frame immediately.
		for(size_t i = 0; i < m_last_native.size(); ++i)
		{
			m_levels[i] = static_cast<float>(m_last_native[i]);
			m_targets[i] = m_last_native[i];
			m_elapsed_ms[i] = 0.0f;
			m_durations_ms[i] = 0.0f;
		}
	}
	update_pixels();
}

const std::vector<u32>& DisplayProcessor::process(
	const std::vector<u8>& native, double seconds)
{
	if(native.empty())
	{
		return m_pixels;
	}

	update_palette();

	if(!m_initialized || m_last_native.size() != native.size())
	{
		m_last_native = native;
		m_pixels.resize(native.size());
		m_levels.resize(native.size());
		m_origins.resize(native.size());
		m_elapsed_ms.resize(native.size());
		m_durations_ms.resize(native.size());
		m_targets.resize(native.size());
		for(size_t i = 0; i < native.size(); ++i)
		{
			const float level = static_cast<float>(native[i]);
			m_levels[i] = level;
			m_origins[i] = level;
			m_elapsed_ms[i] = 0.0f;
			m_durations_ms[i] = 0.0f;
			m_targets[i] = native[i];
		}
		m_initialized = true;
	}
	else
	{
		m_last_native = native;
		if(m_settings.ghosting)
		{
			advance_levels(seconds);
		}
		else
		{
			for(size_t i = 0; i < native.size(); ++i)
			{
				m_levels[i] = static_cast<float>(native[i]);
				m_targets[i] = native[i];
				m_elapsed_ms[i] = 0.0f;
				m_durations_ms[i] = 0.0f;
			}
		}
	}

	update_pixels();
	return m_pixels;
}

const std::vector<u32>& DisplayProcessor::refresh()
{
	update_palette();
	update_pixels();
	return m_pixels;
}

void DisplayProcessor::update_palette()
{
	m_palette = m_settings.colors();
}

void DisplayProcessor::advance_levels(double seconds)
{
	if(seconds <= 0.0 || m_last_native.empty() ||
	   m_levels.size() != m_last_native.size())
	{
		return;
	}

	const float dt_ms = static_cast<float>(std::min(seconds, 0.25)) * 1000.0f;
	for(size_t i = 0; i < m_last_native.size(); ++i)
	{
		const u8 target_index = m_last_native[i];
		if(target_index != m_targets[i])
		{
			const u8 previous_target = m_targets[i];
			const float current = m_levels[i];
			const float target = static_cast<float>(target_index);

			// Drive coherence: a high-contrast dark edge becomes solid as
			// soon as it appears. Without this, faster moving black areas
			// are still half-light when they leave, producing a light band
			// inside the ghost trail. The return to light still ghosts.
			if(static_cast<int>(target_index) - static_cast<int>(previous_target) >= 2)
			{
				m_targets[i] = target_index;
				m_levels[i] = target;
				m_elapsed_ms[i] = 0.0f;
				m_durations_ms[i] = 0.0f;
				continue;
			}
			// Base the duration on the target change, not on the remaining
			// distance from an intermediate level. Otherwise a pixel that is
			// still half-darkened when the moving line leaves it fades faster
			// than a fully black pixel and creates a light band inside the
			// trail (dark -> light -> dark).
			const int target_distance = std::abs(
				static_cast<int>(target_index) - static_cast<int>(previous_target));
			const int milliseconds = target_index > previous_target
				? m_settings.ghosting_dark_ms
				: m_settings.ghosting_light_ms;

			m_targets[i] = target_index;
			m_origins[i] = current;
			m_elapsed_ms[i] = 0.0f;
			m_durations_ms[i] = milliseconds > 0
				? target_distance * static_cast<float>(milliseconds)
				: 0.0f;
			if(m_durations_ms[i] == 0.0f)
			{
				m_levels[i] = target;
				continue;
			}
		}

		if(m_durations_ms[i] > 0.0f)
		{
			m_elapsed_ms[i] += dt_ms;
			const float t = std::clamp(
				m_elapsed_ms[i] / m_durations_ms[i], 0.0f, 1.0f);
			// Ease-out cubic: a newly covered pixel darkens promptly (the
			// LCD drive starts fast) and the transition stops smoothly.
			// The per-tone duration is preserved, so 0 -> 3 still takes
			// three times as long.
			const float remaining = 1.0f - t;
			const float eased = 1.0f - remaining * remaining * remaining;
			const float target = static_cast<float>(m_targets[i]);
			m_levels[i] = m_origins[i] +
				(target - m_origins[i]) * eased;
			if(t >= 1.0f)
			{
				m_levels[i] = target;
				m_durations_ms[i] = 0.0f;
			}
		}
	}
}

void DisplayProcessor::update_pixels()
{
	if(m_levels.size() != m_last_native.size())
	{
		return;
	}

	for(size_t i = 0; i < m_levels.size(); ++i)
	{
		const float level = std::clamp(m_levels[i], 0.0f, 3.0f);
		const int lower = std::clamp(
			static_cast<int>(std::floor(level)), 0, 3);
		const int upper = std::min(lower + 1, 3);
		const float fraction = level - static_cast<float>(lower);
		const Rgb& a = m_palette[static_cast<size_t>(lower)];
		const Rgb& b = m_palette[static_cast<size_t>(upper)];
		const auto mix = [fraction](u8 av, u8 bv)
		{
			return static_cast<u8>(std::lround(
				static_cast<float>(av) +
				(static_cast<float>(bv) - static_cast<float>(av)) * fraction));
		};
		m_pixels[i] = DisplaySettings::to_rgba({
			mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)
		});
	}
}
