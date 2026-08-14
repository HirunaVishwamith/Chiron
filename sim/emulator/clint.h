#pragma once

#include <vector>
#include <sys/time.h>
#include <cstdint>
#include <cstddef>

class CLINT
{
private:
    static const uint64_t CLINT_BASE_ADDR = 0x2000000;
    static const uint64_t MSIP_BASE = 0x0000;
    static const uint64_t MTIMECMP_BASE = 0x4000;
    static const uint64_t MTIME_OFFSET = 0xBFF8;
    static const uint32_t freq_hz = 50000000;

    std::vector<uint32_t> msip;     // Machine Software Interrupt Pending
    std::vector<uint64_t> mtimecmp; // Machine Time Compare
    uint64_t mtime;                 // Machine Time
    struct timeval tv;

    uint8_t num_harts;
    uint32_t hart;

public:
    CLINT(uint8_t num_harts) : num_harts(num_harts), mtime(0)
    {
        msip.resize(num_harts, 0);
        mtimecmp.resize(num_harts, ~0ULL);  // like old: large value so timer does not fire until OS sets mtimecmp
        gettimeofday(&tv, NULL);
    }

    // Force MSIP[hart] from an external source (RTL pin in lock-step).
    void set_msip(uint32_t h, uint32_t value)
    {
        if (h < num_harts)
            msip[h] = value & 1;
    }

    uint32_t get_msip(uint32_t h) const
    {
        return (h < num_harts) ? (msip[h] & 1) : 0;
    }

    // Force mtime from RTL (lock-step). Guest code (clint_get_cycles64) loads
    // the live MMIO mtime; independent golden advance() drifts from the RTL
    // 16-cycle-prescaled counter and fails lockstep after timer init.
    void set_mtime(uint64_t value) { mtime = value; }
    uint64_t get_mtime() const { return mtime; }

    void set_mtimecmp(uint32_t h, uint64_t value)
    {
        if (h < num_harts)
            mtimecmp[h] = value;
    }
    uint64_t get_mtimecmp(uint32_t h) const
    {
        return (h < num_harts) ? mtimecmp[h] : ~0ULL;
    }

    void write(uint64_t addr, uint64_t value, int size = 8)
    {
        if ((addr >= CLINT_BASE_ADDR + MSIP_BASE) && (addr < CLINT_BASE_ADDR + MSIP_BASE + 4 * num_harts))
        {
            hart = (addr - (CLINT_BASE_ADDR + MSIP_BASE)) / 4;
#ifdef LOCKSTEP
            // In RTL lock-step, MSIP is driven exclusively from the RTL
            // msipShared pins (see lockstep_linux sync). Golden store timing
            // is instantaneous while the RTL write takes many AXI cycles after
            // the store commits — applying the store here makes secondaries
            // leave the MSIP wait loop (csrr mip / andi / bnez) on golden
            // while RTL is still WFI'ing → PC mismatch.
            (void)value;
#else
            msip[hart] = value & 1;   // only bit 0 is used for MSIP
#endif
        }
        else if ((addr >= CLINT_BASE_ADDR + MTIMECMP_BASE) && (addr < CLINT_BASE_ADDR + MTIMECMP_BASE + 8 * num_harts))
        {
            uint64_t h = (addr - (CLINT_BASE_ADDR + MTIMECMP_BASE)) / 8 ;
            if (size >= 8) {
                mtimecmp[h] = value;
            } else if (size == 4) {
                uint64_t data = value & 0xFFFFFFFFULL;  // 32-bit store always places datum in low bits of value
                if ((addr & 7) == 0) {
                    mtimecmp[h] = (mtimecmp[h] & 0xFFFFFFFF00000000ULL) | data;
                } else {
                    mtimecmp[h] = (mtimecmp[h] & 0x00000000FFFFFFFFULL) | (data << 32);
                }
            }
            // ignore other sizes
        }
        else if ((addr >= CLINT_BASE_ADDR + MTIME_OFFSET) && (addr < CLINT_BASE_ADDR + MTIME_OFFSET + 8))
        {
            if (size >= 8) {
                mtime = value;
            } else if (size == 4) {
                uint64_t data = value & 0xFFFFFFFFULL;
                if ((addr & 7) == 0) {
                    mtime = (mtime & 0xFFFFFFFF00000000ULL) | data;
                } else {
                    mtime = (mtime & 0x00000000FFFFFFFFULL) | (data << 32);
                }
            }
        }
    }

    uint64_t read(uint64_t addr)
    {
        if ((addr >= CLINT_BASE_ADDR + MSIP_BASE) && (addr < CLINT_BASE_ADDR + MSIP_BASE + 4 * num_harts))
        {
            hart = (addr - (CLINT_BASE_ADDR + MSIP_BASE)) / 4;
            return msip[hart];
        }
        else if ((addr >= CLINT_BASE_ADDR + MTIMECMP_BASE) && (addr < CLINT_BASE_ADDR + MTIMECMP_BASE + 8 * num_harts))
        {
            hart = (addr - (CLINT_BASE_ADDR + MTIMECMP_BASE)) / 8;
            if ((addr & 7) == 4) {
                return mtimecmp[hart] >> 32;
            }
            return mtimecmp[hart];
        }
        else if ((addr >= CLINT_BASE_ADDR + MTIME_OFFSET) && (addr < CLINT_BASE_ADDR + MTIME_OFFSET + 8))
        {
            if ((addr & 7) == 4) {
                return mtime >> 32;
            }
            return mtime;
        }
        return 0;
    }

    void tick()
    {
        // Wall time driven mtime (resynced from ctor base). Keeps guest timer
        // rate, jiffies and printk timestamps behaving like real hardware.
        // Used by the ISA/lockstep harnesses, where OS timer livelock isn't a
        // concern. NOT used by the standalone Linux boot (see advance()).
        struct timeval now;
        uint64_t diff_usecs;

        gettimeofday(&now, NULL);
        diff_usecs = ((now.tv_sec - tv.tv_sec) * 1000000) + (now.tv_usec - tv.tv_usec);
        mtime = diff_usecs * freq_hz / 1000000;
    }

    // Emulated-progress driven mtime: advances by a fixed amount per call
    // instead of tracking real wall-clock. Wall-clock mtime (tick(), above)
    // decouples the guest timer period from actual emulated throughput: an
    // interpretive multi-hart emulator can take longer (in real time) to
    // service a timer interrupt than the guest-programmed period represents,
    // so the next tick is already "due" the instant it returns -> the hart
    // spends 100% of its budget re-entering the timer ISR and never makes
    // forward progress (observed as secondary harts permanently soft-locked,
    // unable to join a stop_machine() rendezvous during Linux SMP boot).
    // Tying mtime to actual simulation steps instead makes the timer period
    // scale with real emulated progress, so it can never outrun it.
    void advance(uint64_t delta = 1)
    {
        mtime += delta;
    }

    bool check_timer_interrupt(uint32_t hart)
    {
        return mtime >= mtimecmp[hart];
    }

    bool check_software_interrupt(uint32_t hart)
    {
        return msip[hart] != 0;
    }
};