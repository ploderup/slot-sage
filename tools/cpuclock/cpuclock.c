// cpuclock: set or print the Miyoo Mini / Mini Plus CPU clock.
//
// Adapted from Onion's src/cpuclock/cpuclock.c (https://github.com/OnionUI/Onion,
// GPL-3.0), which programs the SigmaStar SSD202D's CPU PLL directly through
// /dev/mem, the way the community has run these consoles above their stock
// 1200 MHz for years. Slot is GPL-3.0-or-later, so this copy is under GPL-3.0.
//
// Changes from Onion's tool: no device-model check (it ships only in the Mini
// Plus pak), a narrower accepted range, and errors are reported instead of
// writing through a failed mmap.
//
//   cpuclock          print the current clock in MHz
//   cpuclock <MHz>    set the clock (1000-1900), then print it
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define BASE_REG_RIU_PA (0x1F000000)
#define BASE_REG_MPLL_PA (BASE_REG_RIU_PA + 0x103000 * 2)
#define PLL_SIZE (0x1000)

#define MIN_MHZ 1000
#define MAX_MHZ 1900

static void *pll_map;

static void print_clock(void)
{
    volatile uint8_t *p8 = (uint8_t *)pll_map;
    volatile uint16_t *p16 = (uint16_t *)pll_map;

    uint32_t lpf_value = p16[0x2A4] + (p16[0x2A6] << 16);
    uint32_t post_div = p16[0x232] + 1;
    if (lpf_value == 0)
        lpf_value = (p8[0x2C2 << 1] << 16) + (p8[0x2C1 << 1] << 8) + p8[0x2C0 << 1];
    if (lpf_value == 0) {
        puts("0");
        return;
    }

    // LPF_value(5.19) = (432MHz / Ref_clk) * 2^19 for post_div=2,
    // Ref_clk = CPU_CLK * 2 / 32
    static const uint64_t divsrc = 432000000llu * 524288;
    uint32_t rate = (divsrc / lpf_value * 2 / post_div * 16);
    printf("%u\n", rate / 1000000);
}

static void writefile(const char *fname, const char *str)
{
    int fd = open(fname, O_WRONLY);
    if (fd >= 0) {
        ssize_t n = write(fd, str, strlen(str));
        (void)n; // best effort: the PLL write below is what sets the clock
        close(fd);
    }
}

// Set governor = userspace before calling (main does).
static void set_cpuclock(int khz)
{
    uint32_t post_div;
    if (khz >= 800000)
        post_div = 2;
    else if (khz >= 400000)
        post_div = 4;
    else if (khz >= 200000)
        post_div = 8;
    else
        post_div = 16;

    static const uint64_t divsrc = 432000000llu * 524288;
    uint32_t rate = (khz * 1000) / 16 * post_div / 2;
    uint32_t lpf = (uint32_t)(divsrc / rate);
    volatile uint16_t *p16 = (uint16_t *)pll_map;

    uint32_t cur_post_div = (p16[0x232] & 0x0F) + 1;
    uint32_t tmp_post_div = cur_post_div;
    if (post_div > cur_post_div) {
        while (tmp_post_div != post_div) {
            tmp_post_div <<= 1;
            p16[0x232] = (p16[0x232] & 0xF0) | ((tmp_post_div - 1) & 0x0F);
        }
    }

    p16[0x2A8] = 0x0000;       // reg_lpf_enable = 0
    p16[0x2AE] = 0x000F;       // reg_lpf_update_cnt = 32
    p16[0x2A4] = lpf & 0xFFFF; // target freq to LPF high
    p16[0x2A6] = lpf >> 16;
    p16[0x2B0] = 0x0001;       // switch to LPF control
    p16[0x2B2] |= 0x1000;      // from low to high
    p16[0x2A8] = 0x0001;       // reg_lpf_enable = 1
    while (!(p16[0x2BA] & 1))
        ;                      // wait for the PLL to settle
    p16[0x2A0] = lpf & 0xFFFF; // store freq to LPF low
    p16[0x2A2] = lpf >> 16;

    if (post_div < cur_post_div) {
        while (tmp_post_div != post_div) {
            tmp_post_div >>= 1;
            p16[0x232] = (p16[0x232] & 0xF0) | ((tmp_post_div - 1) & 0x0F);
        }
    }
}

int main(int argc, char *argv[])
{
    int mhz = 0;
    if (argc == 2) {
        mhz = atoi(argv[1]);
        if (mhz < MIN_MHZ || mhz > MAX_MHZ) {
            fprintf(stderr, "cpuclock: %s is outside %d-%d MHz\n", argv[1], MIN_MHZ, MAX_MHZ);
            return 2;
        }
    } else if (argc != 1) {
        fprintf(stderr, "usage: cpuclock [MHz, %d-%d]\n", MIN_MHZ, MAX_MHZ);
        return 2;
    }

    int fd_mem = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd_mem < 0) {
        perror("cpuclock: /dev/mem");
        return 1;
    }
    pll_map = mmap(0, PLL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_mem, BASE_REG_MPLL_PA);
    if (pll_map == MAP_FAILED) {
        perror("cpuclock: mmap");
        close(fd_mem);
        return 1;
    }

    if (mhz) {
        char khz[16];
        snprintf(khz, sizeof khz, "%d", mhz * 1000);
        writefile("/sys/devices/system/cpu/cpufreq/policy0/scaling_governor", "userspace");
        writefile("/sys/devices/system/cpu/cpufreq/policy0/scaling_setspeed", khz);
        set_cpuclock(mhz * 1000);
    }
    print_clock();

    munmap(pll_map, PLL_SIZE);
    close(fd_mem);
    return 0;
}
