#ifndef QTREGB_IFETCHER_REGS_H
#define QTREGB_IFETCHER_REGS_H

#include "types.h"

namespace qtreGB
{

// Read-only view of the LCD registers the fetcher needs. Implemented by LCD so
// the fetcher does not depend on the concrete register file (its layout, its
// setters or its serialization).
class IFetcherRegs
{
	public:
		virtual ~IFetcherRegs() = default;

		virtual u8 get_lcdc() const = 0;
		virtual u8 get_ly() const = 0;
		virtual u8 get_scroll_x() const = 0;
		virtual u8 get_scroll_y() const = 0;
		virtual u8 get_win_x() const = 0;
		virtual u8 get_win_y() const = 0;

		virtual u8 get_bg_palette() const = 0;
		virtual u8 get_obj_palette_0() const = 0;
		virtual u8 get_obj_palette_1() const = 0;

		virtual bool get_lcdc_bgw_enable() const = 0;
		virtual bool get_lcdc_obj_enable() const = 0;

		virtual u8 get_lcdc_obj_height() const = 0;
		virtual bool get_lcdc_win_enable() const = 0;
		virtual u16 get_lcdc_bg_map_area() const = 0;
		virtual u16 get_lcdc_bgw_data_area() const = 0;
		virtual u16 get_lcdc_win_map_area() const = 0;
};

} // namespace qtreGB
#endif
