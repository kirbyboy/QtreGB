#include "fps_counter.h"

void FpsCounter::reset()
{
	m_started = false;
	m_has_value = false;
	m_window_start = 0.0;
	m_last_frame = 0.0;
	m_frames = 0;
	m_fps = 0.0;
}

void FpsCounter::add_frame(double now_seconds)
{
	// A long gap means the machine was not producing frames (LCD off, ROM
	// being loaded). Start a fresh window instead of averaging the pause in.
	if(!m_started || now_seconds - m_last_frame > IdleSeconds)
	{
		m_started = true;
		m_window_start = now_seconds;
		m_frames = 0;
		m_has_value = false;
		m_fps = 0.0;
	}
	m_last_frame = now_seconds;
	++m_frames;

	const double elapsed = now_seconds - m_window_start;
	if(elapsed >= WindowSeconds)
	{
		m_fps = m_frames / elapsed;
		m_has_value = true;
		m_window_start = now_seconds;
		m_frames = 0;
	}
}

double FpsCounter::fps(double now_seconds, double idle_seconds) const
{
	if(!has_value(now_seconds, idle_seconds))
	{
		return 0.0;
	}
	return m_fps;
}

bool FpsCounter::has_value(double now_seconds, double idle_seconds) const
{
	if(!m_has_value)
	{
		return false;
	}
	return now_seconds - m_last_frame <= idle_seconds;
}
