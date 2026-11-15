#ifndef DOCBOYBOOTROMHELPERS_H
#define DOCBOYBOOTROMHELPERS_H

// Runtime counterpart of DocBoy's compile-time boot-ROM selector. QtreGB can
// load a boot ROM at runtime, so the PPU reset path must consult a flag.
extern bool g_docboy_bootrom;

template <typename T>
T if_bootrom_else(T y, T n)
{
	return g_docboy_bootrom ? y : n;
}

#endif // DOCBOYBOOTROMHELPERS_H
