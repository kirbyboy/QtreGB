#ifndef QTREGB_FETCHER_BG_H
#define QTREGB_FETCHER_BG_H

#include "common.h"
#include "fetcher.h"
#include "fifo.h"

class FetcherBG
{
	public:
		FetcherBG(FIFO & bgw_fifo);

	private:
		FIFO & m_bgw_fifo;
		FetchEntry m_current_state;
};

#endif
