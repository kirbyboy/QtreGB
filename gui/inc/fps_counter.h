#ifndef QTREGB_FPS_COUNTER_H
#define QTREGB_FPS_COUNTER_H

// Counts emulated frames per wall-clock second over a short window. The
// window is independent of the presentation rate: at 200% fast forward the
// counter reports about 120 fps even though only one frame is drawn per
// display refresh.
class FpsCounter
{
	public:
		void reset();
		void add_frame(double now_seconds);
		// Frames per second, or 0 when the last frame is older than
		// idle_seconds (for example while the LCD is off).
		double fps(double now_seconds, double idle_seconds = 1.0) const;
		bool has_value(double now_seconds, double idle_seconds = 1.0) const;

	private:
		static constexpr double WindowSeconds = 0.5;
		static constexpr double IdleSeconds = 1.0;
		bool m_started = false;
		bool m_has_value = false;
		double m_window_start = 0.0;
		double m_last_frame = 0.0;
		int m_frames = 0;
		double m_fps = 0.0;
};

#endif
