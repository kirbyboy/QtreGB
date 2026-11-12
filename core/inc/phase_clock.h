#ifndef QTREGB_PHASE_CLOCK_H
#define QTREGB_PHASE_CLOCK_H

#include "types.h"

namespace qtreGB
{

enum class Phase : u8
{
    T0 = 0,
    T1,
    T2,
    T3,
};

class PhaseClock
{
    public:
        Phase get() const
        {
            return static_cast<Phase>(m_value);
        }

        Phase advance()
        {
            m_value = static_cast<u8>((m_value + 1) & 0x03);
            return get();
        }

        void reset()
        {
            m_value = 0;
        }

    private:
        u8 m_value = 0;
};

} // namespace qtreGB

#endif
