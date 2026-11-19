#ifndef QTREGB_RECENT_FILES_H
#define QTREGB_RECENT_FILES_H

#include <cstddef>
#include <string>
#include <vector>

// Most-recent-first list of ROM paths. Paths are unique and the list is
// capped at MaxEntries. Only the normal player 1 ROM flow uses this list;
// Link/player 2 ROMs are deliberately excluded.
class RecentFiles
{
	public:
		static constexpr size_t MaxEntries = 10;

		// Replaces the list, preserving the given order, dropping empty and
		// duplicate entries and truncating to MaxEntries.
		void set(const std::vector<std::string>& paths);
		// Moves path to the front, adding it if needed.
		void remember(const std::string& path);
		void remove(const std::string& path);
		void clear() { m_paths.clear(); }

		const std::vector<std::string>& paths() const { return m_paths; }
		bool empty() const { return m_paths.empty(); }
		size_t size() const { return m_paths.size(); }

	private:
		std::vector<std::string> m_paths;
};

#endif
