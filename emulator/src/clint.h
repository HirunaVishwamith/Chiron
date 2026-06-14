
#include <vector>
#include <sys/time.h>
#include <cstdint>
#include <cstddef>

class CLINT
{
private:
    static const uint64_t CLINT_BASE = 0x2000000;
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
        mtimecmp.resize(num_harts, 0);
        gettimeofday(&tv, NULL);
    }

    void write(uint64_t addr, uint64_t value)
    {

        if ((addr >= CLINT_BASE + MSIP_BASE) && (addr < CLINT_BASE + MSIP_BASE + 4 * num_harts))
        {
            hart = (addr - (CLINT_BASE + MSIP_BASE)) / 4;
            msip[hart] = value & 1;
        }
        else if ((addr >= CLINT_BASE + MTIMECMP_BASE) && (addr < MTIMECMP_BASE + 8 * num_harts))
        {
            hart = (addr - (CLINT_BASE + MTIMECMP_BASE)) / 8;
            mtimecmp[hart] = value;
        }
        else if ((addr >= CLINT_BASE + MTIME_OFFSET) && (addr < CLINT_BASE + MTIME_OFFSET + 8))
        {

            mtime = value;
        }
    }

    uint64_t read(uint64_t addr)
    {
        if ((addr >= CLINT_BASE + MSIP_BASE) && (addr < CLINT_BASE + MSIP_BASE + 4 * num_harts))
        {
            hart = (addr - (CLINT_BASE + MSIP_BASE)) / 4;
            return msip[hart];
        }
        else if ((addr >= CLINT_BASE + MTIMECMP_BASE) && (addr < MTIMECMP_BASE + 8 * num_harts))
        {
            hart = (addr - (CLINT_BASE + MTIMECMP_BASE)) / 8;

            return mtimecmp[hart];
        }
        else if ((addr >= CLINT_BASE + MTIME_OFFSET) && (addr < CLINT_BASE + MTIME_OFFSET + 8))
        {

            return mtime;
        }
        return 0;
    }

    void tick()
    {
        // mtime++;
        struct timeval now;
        uint64_t diff_usecs;

        gettimeofday(&now, NULL);
        diff_usecs = ((now.tv_sec - tv.tv_sec) * 1000000) + (now.tv_usec - tv.tv_usec);
        mtime = diff_usecs * freq_hz / 1000000;
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