// This poor driver needed a massive overhaul.
// Reverse-engineering that GAPS bridge took forever, but at least it's
// actually documented in here now.
// --Moopthehedgehog

#include "rtl8139.h"
#include "adapter.h"
#include "dcload.h"
#include "net.h"
#include "packet.h"
#include <string.h>

#include "dhcp.h"
#include "memfuncs.h"
#include "perfctr.h"

// TEMP
// #define LOOP_TIMING
// #define TX_LOOP_TIMING
// #define RX_LOOP_TIMING
// #define PKT_PROCESS_TIMING
// #define FULL_TRIP_TIMING

#ifdef LOOP_TIMING
// #include "perfctr.h"
#include "video.h"
static char uint_string_array[11] = {0};
#endif
// end TEMP

// Pull together all the goodies
adapter_t adapter_bba = {"Broadband Adapter (HIT-0400)",
                         {0}, // Mac address
                         {0}, // 2-byte alignment pad
                         rtl_bb_detect,
                         rtl_bb_init,
                         rtl_bb_start,
                         rtl_bb_stop,
                         rtl_bb_loop,
                         rtl_bb_tx};

static rtl_status_t rtl = {0};
static volatile unsigned char rtl_link_up = 0;
static volatile unsigned char rtl_is_copying = 0;

static void rtl_reset(void);
static void rtl_init(void);
static void pktcpy(unsigned char *dest, unsigned char *src, unsigned int n);
static int rtl_bb_rx(void);

// 8, 16, and 32 bit access to G2 addresses
static vuc *const g28 = REGC(0xa1000000);
static vus *const g216 = REGS(0xa1000000);
static vul *const g232 = REGL(0xa1000000);

/* 8, 16, and 32 bit access to the PCI I/O space (configured by GAPS) */
static vuc *const nic8 = REGC(0xa1001700);
static vus *const nic16 = REGS(0xa1001700);
static vul *const nic32 = REGL(0xa1001700);

/* 8, 16, and 32 bit access to the PCI MEMMAP space (configured by GAPS) */
// static vuc * const mem8 = REGC(0xa1840000);
// static vus * const mem16 = REGS(0xa1840000);
static vul *const mem32 = REGL(0xa1840000);

#define GAPS_RX_IO_AREA 0x81840000U
#define GAPS_TX_IO_AREA 0x81840000U

static vuc *const txdesc[4] = {
    REGC(GAPS_TX_IO_AREA + 0x6000), 
    REGC(GAPS_TX_IO_AREA + 0x6800),
    REGC(GAPS_TX_IO_AREA + 0x7000), 
    REGC(GAPS_TX_IO_AREA + 0x7800)
};

int rtl_bb_detect(void) {
    // This pointer's data is always aligned to 4 bytes--just look at the register address!
    const char *str = (char *)REGC(0xa1001400);
    if(!memcmp_32bit_eq(str, GAPSPCI_ID, 16 / 4)) {
        global_bg_color = BBA_BG_COLOR;
        installed_adapter = BBA_MODEL;

        g232[0x1414 / 4] = 0x00000000; // Set this to 0 first thing
        g232[0x1418 / 4] = 0x5a14a500; // Ensure GAPS is off

        return 0;
    } else {
        return -1;
    }
}

static void rtl_reset(void) {
    /* Soft-reset the chip */
    nic8[RT_CHIPCMD] = RT_CMD_RESET;

    /* Wait for it to come back */
    while(nic8[RT_CHIPCMD] & RT_CMD_RESET)
        ;
}

static void rtl_init(void) {
    unsigned int tmp;

    /* Read MAC address */
    // Don't need to do anything with the eeprom if we're just reading it.
    tmp = nic32[RT_IDR0];
    rtl.mac[0] = tmp & 0xff;
    rtl.mac[1] = (tmp >> 8) & 0xff;
    rtl.mac[2] = (tmp >> 16) & 0xff;
    rtl.mac[3] = (tmp >> 24) & 0xff;
    tmp = nic32[RT_IDR0 + 1];
    rtl.mac[4] = tmp & 0xff;
    rtl.mac[5] = (tmp >> 8) & 0xff;
    memcpy(adapter_bba.mac, rtl.mac, 6);

    /* Soft-reset the chip to clear any garbage from power on */
    rtl_reset();

    /* Setup Rx and Tx buffers */
    nic32[RT_RXBUF / 4] = 0x01840000;
    nic32[RT_TXADDR0 / 4 + 0] = 0x01846000;
    nic32[RT_TXADDR0 / 4 + 1] = 0x01846800;
    nic32[RT_TXADDR0 / 4 + 2] = 0x01847000;
    nic32[RT_TXADDR0 / 4 + 3] = 0x01847800;

    asm volatile("nop\n" : : : "memory"); // Compiler barrier so that GCC doesn't get clever here

    // This is so strange, but ok...
    // reset it AGAIN...
    rtl_reset();

    // Another dance of some kind...
    nic8[RT_CHIPCMD] = RT_CMD_RX_ENABLE;
    if(nic8[RT_CHIPCMD] == RT_CMD_RX_ENABLE) {
        nic8[RT_CHIPCMD] = RT_CMD_TX_ENABLE;
        if(nic8[RT_CHIPCMD] == RT_CMD_TX_ENABLE) {
            nic8[RT_CHIPCMD] = 0; // Disable RX and TX now...
        }
    }

    asm volatile("nop\n" : : : "memory"); // Compiler barrier so that GCC doesn't get clever here

    // Yet another dance of some kind...
    nic32[RT_MAR0 / 4 + 0] = 0x55aaff00;
    nic32[RT_MAR0 / 4 + 1] = 0xaa5500ff;

    if((nic32[RT_MAR0 / 4 + 0] == 0x55aaff00) && (nic32[RT_MAR0 / 4 + 1] == 0xaa5500ff)) {
        nic8[RT_CHIPCMD] = RT_CMD_RX_ENABLE | RT_CMD_TX_ENABLE;
        nic32[RT_MAR0 / 4 + 0] = 0xffffffff;
        nic32[RT_MAR0 / 4 + 1] = 0xffffffff;
    }

    asm volatile("nop\n" : : : "memory"); // Compiler barrier so that GCC doesn't get clever here

    nic16[RT_INTRMASK / 2] = 0; // Disable interrupts
    /*
            //
            // Very strange initialization stuff. Maybe getting this crazy init sequence right
        doubles the RX transfer
            // speed, like how getting the GAPS memory-mapping registers right doubled the TX
        transfer speed? No idea.
            // NOTE: Maybe these weird packets need to be sent via loopback mode?
            // All this doesn't appear to be totally necessary--at least, things seem to work well
        without it. Wonder what it's for.
            //

            unsigned char * tx_weird = (unsigned char*)(0xa1840000 + 0x6000);
            unsigned int tx_weird_iter = 0;

            while(tx_weird_iter < 6)
            {
                tx_weird[tx_weird_iter] = 0xff; // broadcast mac
                tx_weird_iter++;
            }
            // iter = 6
            memcpy(&tx_weird[6], adapter_bba.mac, 6); // bba's mac
            tx_weird_iter += 6; // iter = 12
            tx_weird[12] = 0xea; // ethertype
            tx_weird[13] = 0x5; // err, this makes ethertype 0xea05, since network data is BE...
        Although LE 0x5ea is 1514--GAPS security thing?                             tx_weird_iter += 2; // iter = 14
            // Now for the last 1500 of weird packet 1
            while(tx_weird_iter < 1514)
            {
                tx_weird[tx_weird_iter] = 0x55 + (tx_weird_iter - 14); // weird packet 1 payload
                tx_weird_iter++;
            }
            // iter = 1514

            asm volatile ("nop\n" : : : "memory"); // Compiler barrier so that GCC doesn't get
        clever here

            // read back/check weird packet 1 payload (bytes)
            tx_weird_iter = 14;
            while(tx_weird_iter < 1514)
            {
                if( tx_weird[tx_weird_iter] == (unsigned char)(0x55 + (tx_weird_iter - 14)) )
                {
                    tx_weird_iter++;
                }
                else
                {
                    break;
                }
            }

            if(tx_weird_iter != 1514)
            {
                uint_to_string(tx_weird_iter, (unsigned char*)uint_string_array);
                draw_string(30, 30, uint_string_array, STR_COLOR);
                uint_to_string(tx_weird[tx_weird_iter], (unsigned char*)uint_string_array);
                draw_string(130, 30, uint_string_array, STR_COLOR);
            }

            asm volatile ("nop\n" : : : "memory"); // Compiler barrier so that GCC doesn't get
        clever here

            unsigned int temp_sr = 0;
            unsigned int temp_sr2 = 0;
            asm volatile (
                "stc SR, %[out]\n\t"
                "mov %[out], %[out2]\n\t"
                // preserve S and T
                "and #0x0f, %[out2]\n\t"
                // clear IMASK
                "shlr8 %[out]\n\t"
                "shll8 %[out]\n\t"
                // Put S and T back
                "or %[out], %[out2]\n\t"
                // Store SR for later
                "stc SR, %[out]\n\t"
                // Enable external interrupts
                "ldc %[out2], SR\n"
            : [out] "=&r" (temp_sr), [out2] "=z" (temp_sr2) // outputs
            : // inputs
            : // clobbers
            );

            nic16[RT_INTRMASK/2] = 0x53; // Enable specific interrupts

            asm volatile ("nop\n" : : : "memory"); // Compiler barrier so that GCC doesn't get
        clever here

            // Now do it again...
            tx_weird_iter = 0;
            while(tx_weird_iter < 6)
            {
                tx_weird[tx_weird_iter] = 0xff; // broadcast mac
                tx_weird_iter++;
            }
            // iter = 6
            memcpy(&tx_weird[6], adapter_bba.mac, 6); // bba's mac
            tx_weird_iter += 6; // iter = 12
            tx_weird[12] = 0xea; // ethertype
            tx_weird[13] = 0x5; // err, this makes ethertype 0xea05, since network data is BE...
        Although LE 0x5ea is 1514--GAPS security thing?                             tx_weird_iter += 2; // iter = 14
            // Now for the last 1500 of weird packet 2
            while(tx_weird_iter < 1514)
            {
                tx_weird[tx_weird_iter] = 0x5a + (tx_weird_iter - 14); // weird packet 2 payload
                tx_weird_iter++;
            }
            // iter = 1514

            asm volatile ("nop\n" : : : "memory"); // Compiler barrier so that GCC doesn't get
        clever here

            // read back/check weird packet 2 payload (words)
            tx_weird_iter = 14;
            while(tx_weird_iter < 1514)
            {
                if( ((unsigned short*)tx_weird)[tx_weird_iter/2] == ( (unsigned char)(0x5a +
        (tx_weird_iter - 14)) | ( (unsigned short)((unsigned char)(0x5a + (tx_weird_iter - 13))) <<
        8) ) )
                {
                    tx_weird_iter += 2;
                }
                else
                {
                    break;
                }
            }

            if(tx_weird_iter != 1514)
            {
                uint_to_string(tx_weird_iter, (unsigned char*)uint_string_array);
                draw_string(230, 30, uint_string_array, STR_COLOR);
                uint_to_string(tx_weird[tx_weird_iter], (unsigned char*)uint_string_array);
                draw_string(330, 30, uint_string_array, STR_COLOR);
            }

            asm volatile ("nop\n" : : : "memory"); // Compiler barrier so that GCC doesn't get
        clever here

            nic16[RT_INTRMASK/2] = 0; // Disable interrupts because we don't want them
            // Restore SR
            asm volatile ("ldc %[in], SR\n"
            : // outputs
            : [in] "r" (temp_sr) // inputs
            : "t" // clobbers
            );

            asm volatile ("nop\n" : : : "memory"); // Compiler barrier so that GCC doesn't get
        clever here
    */

    // TODO tune twister seems like a useful thing
    // See
    // https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/realtek/8139too.c#L1481

    /* Enable receive and transmit functions */
    nic8[RT_CHIPCMD] = RT_CMD_RX_ENABLE | RT_CMD_TX_ENABLE;

    /* Set Rx FIFO threshold to 16 bytes, Rx size to 16k+16, 1024 byte DMA burst */
    //    nic32[RT_RXCONFIG/4] = 0x00000e00; // (1<<7 = 0x80) for nowrap or bit 7 = 0 for wrap, 1024
    //byte dma burst (6<<8 = 0x600)
    // Why only 16k + 16? let's do 32k + 16.
    nic32[RT_RXCONFIG / 4] =
        0x00004980; // nowrap, 16k + 16, 32 byte DMA burst, 64 byte Rx threshold, Early Rx: none
    //    nic32[RT_RXCONFIG/4] = 0x00002980; // nowrap, 16k + 16, 32 byte DMA burst, 32 byte Rx
    //threshold, Early Rx: none     nic32[RT_RXCONFIG/4] = 0x00005180; // nowrap, 32k + 16, 32 byte DMA
    //burst, 64 byte Rx threshold, Early Rx: none

    //    nic32[RT_RXCONFIG/4] = 0x00005100; // wrap, 32k + 16, 32 byte DMA burst, 64 byte Rx
    //threshold, Early Rx: none     nic32[RT_RXCONFIG/4] = 0x00004900; // wrap, 16k + 16, 32 byte DMA
    //burst, 64 byte Rx threshold, Early Rx: none

    /* Set Tx 1024 byte DMA burst */
    // Found a bug: this was 00000600 before. 1024 byte DMA burst is 0x600 (hex), not 600 (dec).
    // See pgs. 20-21 of RTL8139 datasheet: http://realtek.info/pdf/rtl8139cp.pdf
    //>    nic32[RT_TXCONFIG/4] = 0x03000600; // Set IFG to NOT violate 802.3 standard, 1024 byte DMA
    //burst
    nic32[RT_TXCONFIG / 4] = 0x03000100; // Set IFG to NOT violate 802.3 standard, 32 byte DMA burst

    /* Enable writing to the config registers */
    nic8[RT_CFG9346] = 0xc0;

    /* Disable power management (zeroes are otherwise default values) */
    // and     /* Set the driver-loaded bit (0x20) */
    // LEDs are apparently meant to be set to 0b10. Maybe those pins are repurposed?
    nic8[RT_CONFIG1] = (nic8[RT_CONFIG1] | 0x80 | 0x20) & 0xbf;

    /* Enable FIFO auto-clear */
    nic8[RT_CONFIG4] |= 0x80;

    // Make internal FIFO address pointer increment downwards
    // This apparently can be set without unlocking the EEPROM,
    // but might as well keep it here with the others.
    nic8[RT_CONFIG5] |= 0x04;

    /* Switch back to normal operation mode */
    nic8[RT_CFG9346] = 0;

    /* Filter out all multicast packets */
    nic32[RT_MAR0 / 4 + 0] = 0;
    nic32[RT_MAR0 / 4 + 1] = 0;

    /* Disable all multi-interrupts */
    nic16[RT_MULTIINTR / 2] = 0;

    /* clear all interrupts */
    nic16[RT_INTRSTATUS / 2] = 0xffff;

    /* Reset RXMISSED counter */
    nic32[RT_RXMISSED / 4] = 0;

    /* Enable RX/TX once more */
    nic8[RT_CHIPCMD] = RT_CMD_RX_ENABLE | RT_CMD_TX_ENABLE;

    /* Enable auto-negotiation and restart that process */
    nic16[RT_MII_BMCR / 2] |= 0x9200;

    /* Initialize status vars */
    rtl.cur_tx = 0;
    rtl.cur_rx = 0;

    /* Enable receiving broadcast and physical match packets */
    nic32[RT_RXCONFIG / 4] |= 0x0000000a;
}

int rtl_bb_init(void) {
    int i;

    // The BBA uses the range 0x01840000-0x0184ffff for TX and RX (with usage above
    // 0x01848000 apparently for GAPS DMA), plus the 2 bytes at 0x0183fffc for... something

    /* Initialize the "GAPS" PCI glue controller. */

    // NOTE: Setting 0x5a14a500 turns off GAPS.
    g232[0x1418 / 4] = 0x5a14a501; /* M */
    i = 10000;
    // This delay is probably the part where the RealTek 8139C datasheet says to
    // wait 2ms for the chip to powerup and load its config from its EEPROM
    while((!(g232[0x1418 / 4] & 1)) && (i > 0))
        i--;
    if(!(g232[0x1418 / 4] & 1)) { // timed out waiting for rtl8139c to init
        return -1;
    }

    g232[0x1420 / 4] = 0x01000000;
    g232[0x1424 / 4] = 0x01000000;
    g232[0x1428 / 4] = 0x01840000; /* 32k SRAM Map Base (?) */
    /* Register offset 0x142c controls where the image area at GAPS offset 0x8000 (so 0x01848000)
     * points to, which is used by DMA */
    g232[0x1414 / 4] = 0x00000001; // Interrupt-related...? (Whatever this is, it gets 1 written to
                                   // it a couple times, and gets a zero written to it in the same
                                   // places where 0x1418 gets the turn-off code...)
    g232[0x1434 / 4] = 0x00000001;
    // There appears to be a register at offset 0x1410, which has 0x0c003000 in it. This appears to
    // be a pretty random address in the syscall area. It doesn't seem to be used for anything,
    // though, and it just seems to get read from in its unused function--it's never written to.
    // Similarly, 0x1430 has 0x00000010 in it, who knows what for. This one doesn't even have an
    // unused function for it.

    // Now configure the RTL8139's PCI configuration

    // VEN:DEV is 11db:1234 (vendor code is "Sega Enterprises, LTD")
    // The GAPS bridge is really just an MMU with a memory buffer that maps the RTL8139C to the
    // Dreamcast's memory space, so these are actually the PCI configuration registers for the
    // RTL8139, not GAPS (those are just the 0x1400 regs). It has a custom ven:dev ID, but the class
    // ID in 0x1608 indicates a network controller (byte 0x160b = 0x02 = network controller, 0x160a
    // = 0x00 = Ethernet controller) See PCI Local Bus Specification 2.2 (2.3 has all the 2.2 stuff
    // in it and the RTL8139C uses 2.2) This is also documented in the RTL8139C's datasheet, under
    // "PCI Configuration Space Registers"
    g216[0x1606 / 2] = 0xf900;     // PCI Status Register
    g232[0x1630 / 4] = 0x00000000; // PCI BMAR
    g28[0x163c] = 0x00;            // Interrupt Line
    g28[0x160d] = 0xf0;            // Primary Latency Timer
    g216[0x1604 / 2] |=
        0x6; // PCI Command Register (note: Fast Back-to-Back is read-only 0 on this bridge)
    // 0x1610 (BAR0, I/O BAR) reads back as 0x00000001, which is I/O Space Indicator
    g232[0x1614 / 4] = 0x01000000; // BAR1 (Memory BAR)
    // There are two extra regs here, though, that are GAPS-specific (0x1650 and 0x1654).
    if(g28[0x1650] & 1) // ???
    {
        g216[0x1654 / 2] = (g216[0x1654 / 2] & 0xfffc) | 0x8000;
    }

    // Apparently we do this again
    g232[0x1414 / 4] = 0x00000001;

    // Clear the GAPS area
    memset_zeroes_64bit((void *)0x01840000, 32768 / 8);
    CacheBlockPurge(
        (void *)0x01840000,
        32768 / 32); // Write back and invalidate the cache over that area since it's volatile

    // do this weird dance
    // Appears to set and check some magic numbers to know if it's being initted properly...?
    if(g232[0x141c / 4] == 0x41474553) { // Hah, this is ASCII for 'SEGA' in little-endian
        g232[0x141c / 4] = 0x55aaff00;

        if(g232[0x141c / 4] == 0x55aaff00) {
            g232[0x141c / 4] = 0xaa5500ff;

            if(g232[0x141c / 4] == 0xaa5500ff) {
                g232[0x141c / 4] = 0x41474553;
                // I think GAPS automatically pulls RSTB low for 120ns, which causes the
                // EEPROM to autoload all the registers initially. So we don't need to
                // worry about it.
                rtl_init();
            }
            else {
                return -3;
            }
        }
        else {
            return -2;
        }
    }
    else {
        return -1;
    }

    return 0;
}

void rtl_bb_start(void) { nic32[RT_RXCONFIG / 4] |= 0x0000000a; }

void rtl_bb_stop(void) { nic32[RT_RXCONFIG / 4] &= 0xfffffff5; }

int rtl_bb_tx(unsigned char *pkt,
              int len) { // pg. 15 in RTL8139C datasheet: http://realtek.info/pdf/rtl8139cp.pdf
    // According to KOS source we gotta wait for G2 FIFO to be empty by checking
    // this bit before reading from/writing to G2. So do that here.
    while((*(volatile unsigned int *)0xa05f688c) & 0x20U)
        ;

    while(!(nic32[RT_TXSTATUS0 / 4 + rtl.cur_tx] &
            0x2000U)) { // While tx is not complete (checking OWN)
        if(nic32[RT_TXSTATUS0 / 4 + rtl.cur_tx] & 0x40000000U) { // Check for abort
            // Found another bug: (nic32[RT_TXSTATUS0/4 + rtl.cur_tx] |= 1; // <-- If abort, set
            // descriptor size to 1)
            // |= the length to 1 doesn't do anything if the length is an odd number >= 1...
            // Should probably be RT_TXCONFIG register |= 1, which clears abort state and
            // retransmits, see pg 21 of RTL8139C or pg 17 of RTL8139D datasheet
            nic32[RT_TXCONFIG / 4] |= 0x1;
        }
    }

// Tx time
#ifdef TX_LOOP_TIMING
    unsigned long long int first_array = PMCR_RegRead(DCLOAD_PMCR);
#endif

	unsigned char *copyback_pkt_base = to_p1(&pkt[-2]); // copyback base in cached memory area

    __builtin_prefetch(copyback_pkt_base);

    // Set GAPS DMA image offset pointer to relevant TX region
    g232[0x142c / 4] = (unsigned int)txdesc[rtl.cur_tx];

    /* 8139 doesn't auto-pad */
    if(len < 60) { // This condition may look a little gnarly, but that's because it's meant for speed
                   // above all else.
        // So pad it.
        //
        // We absolutely need to pad this, otherwise prior packets can leak into the
        // padding. It's called "EtherLeak," and the RTL8139 is apparently a poster
        // child chipset for the issues that come from lacking auto-pad.

        unsigned int copyback_pkt_offset_len = 2 + len;
        unsigned int copyback_pkt_len_align4 =
            copyback_pkt_offset_len & -4; // relative to copyback packet base
        unsigned int copyback_pkt_extras = copyback_pkt_offset_len - copyback_pkt_len_align4;
        unsigned int copyback_pkt_extras_end =
            (copyback_pkt_offset_len + 3) &
            -4; // This is either equal to or 4 bytes greater than copyback_pkt_len_align4

        // Mix in trailing zeros if there are 1, 2, or 3 extra bytes
        // Using 4-byte alignment for best performance.
        if(copyback_pkt_extras) {
            unsigned int last_data = 0x00000000;

            // Need the 4-byte-aligned offset to store this padding-mixed data
            unsigned int output_offset = copyback_pkt_len_align4;

            last_data |= copyback_pkt_base[copyback_pkt_len_align4++]; // 1 extra byte

            if(copyback_pkt_len_align4 != (unsigned int)copyback_pkt_offset_len) {
                last_data |= (unsigned int)(copyback_pkt_base[copyback_pkt_len_align4++])
                             << 8; // 2 extra bytes
            }

            if(copyback_pkt_len_align4 != (unsigned int)copyback_pkt_offset_len) {
                last_data |= (unsigned int)(copyback_pkt_base[copyback_pkt_len_align4])
                             << 16; // 3 extra bytes
            }

            // One 4-byte write instead of up to 3x 1-byte writes. For uncached setups, this makes
            // odd-size small packets take the same total time to copy to the NIC as
            // multiple-of-4-sized packets, meaning this is all running as fast as possible.
            *(unsigned int *)(copyback_pkt_base + output_offset) = last_data;
        }

        // Pad the rest of the packet with zeros, if more trailing zeroes need to be written beyond
        // the mixed bytes
        if(copyback_pkt_extras_end < 64) { // The only time this won't be true for small packets is
                                           // for one with 17 payload bytes.
            // 2 unmodified bytes will end up being written to the RTL's TX buffer (since we just
            // want 60 bytes, which offsets to 62, and 62 then 4-byte-aligns to 64. Offset
            // everything back 2 bytes to undo the internal ethernet alignment for transmit results
            // in bytes 65-66 getting copied unzeroed), but that's OK here. The RTL8139C will
            // clobber them with the second half of a 4-byte CRC because it's been configured to
            // append a CRC, anyways--not to mention we set a length parameter for transmit, which
            // makes those bytes doubly irrelevant.

            unsigned int zero_remain = 64 - copyback_pkt_extras_end;
            unsigned int zeroing_top = (unsigned int)copyback_pkt_base + copyback_pkt_extras_end +
                                       zero_remain; // add zero_remain for pre-dec
            zero_remain /= 4;                       // will equal 1, 2, 3, 4, or 5
            unsigned int zero_data = 0;

            asm volatile(
                "clrs\n" // Align for parallelism (CO) - SH4a use "stc SR, Rn" instead with a dummy
                         // Rn
                "dt %[size]\n\t" // Decrement and test size here once to prevent extra jump (EX 1)
                ".align 2\n"
                "1:\n\t"
                // *--nextd = val
                "mov.l %[data], @-%[out]\n\t" // (LS 1/1)
                "bf.s 1b\n\t"                 // (BR 1/2)
                " dt %[size]\n\t"             // (--size) ? 0 -> T : 1 -> T (EX 1)
                : [out] "+r"(zeroing_top), [size] "+&r"(zero_remain) // outputs
                : [data] "r"(zero_data)                              // inputs
                : "t", "memory"                                      // clobbers
            );
            //
            // GCC does a mediocre job of optimizing memset on SH4 for some reason.
            // So, for reference, the above assembly just does this (except each loop takes only 2
            // cycles per iteration):
            //
            //            unsigned int zero_remain = (64 - copyback_pkt_extras_end) / 4;
            //            unsigned int * zeroing_base = (unsigned int*)(copyback_pkt_base +
            //copyback_pkt_extras_end);
            //
            //            while(zero_remain--)
            //            {
            //                *zeroing_base++ = 0;
            //            }
            //
            // See DreamHAL for a complete set of similarly highly-optimized SH4 functions
            //
        }

        // Synchronize zeroed out data with tx buffer
        // Note that 60 bytes would be offset by 62, still within 2nd cache block
        // But we write to 64 bytes... which is still within the 2nd cache block ;).
        CacheBlockWriteBack((unsigned char *)(copyback_pkt_base + 32), 1);

        len = 60; // Finally, set length for transmit

        // NOTE: The reason the minimum length is hardcoded to 60 is because the minimum
        // frame size allowed is 46 bytes + 14 byte ethernet header. Well, it's actually 64,
        // but the NIC is configured to auto-append a 4-byte CRC.
    }

	// Copy packet over to RTL via GAPS while also accounting for dcload-ip's packet alignment 
    // offset
	SH4_mem_to_pkt_X_movca_32((unsigned char*)0x81848000, copyback_pkt_base, len);
	// Technically this will prefetch beyond 1536 for packets between 1504 and 1514 in size, but 
    // that's not an issue.

// Tx time end
#ifdef TX_LOOP_TIMING
    unsigned long long int second_array = PMCR_RegRead(DCLOAD_PMCR);
    unsigned int loop_difference = (unsigned int)(second_array - first_array);

    clear_lines(222, 24, global_bg_color);
    uint_to_string_dec(loop_difference, (char *)uint_string_array);
    draw_string(30, 222, uint_string_array, STR_COLOR);
#endif

    // Set len (SIZE field), destructively zeroing out all other R/W settings. OWN needs to be
    // cleared by software; it does here. Software writes don't impact the read-only bits. Zeroing
    // also sets Early FIFO TX threshold to 8 bytes. Finally, writing to the status register
    // triggers the packet send.
    nic32[RT_TXSTATUS0 / 4 + rtl.cur_tx] = len | 0x20000; // Set Early TX to 64 bytes
    // nic32[RT_TXSTATUS0/4 + rtl.cur_tx] = len | 0x10000; // Set Early TX to 32 bytes
    //    nic32[RT_TXSTATUS0/4 + rtl.cur_tx] = len;

    rtl.cur_tx = (rtl.cur_tx + 1) % 4; // Move to next txdesc buffer

    return 1;
}

static void pktcpy(unsigned char *dest, unsigned char *src,
                   unsigned int n) { // dest and src should already be in a copyback memory region
    if(n > RX_PKT_BUF_SIZE)
        return;

    // According to KOS source we gotta wait for G2 FIFO to be empty by checking
    // this bit before reading from/writing to G2. So do that here.
    while((*(volatile unsigned int *)0xa05f688c) & 0x20U)
        ;

    // Set GAPS DMA image offset pointer to relevant RX region
    //--    g232[0x142c/4] = (unsigned int)src;
    g232[0x142c / 4] = (unsigned int)src - 2; // Yup, this works. So we can just use memcpy_32bit()

    // NOWRAP
    // Note: the +3 may mean we read some of the CRC for not-byte-multiple packets. That's fine: it
    // doesn't cause us any problems.
    //--    SH4_pkt_to_mem_X_movca_32(dest, (unsigned char*)0x01848000, n); // This takes full n now
    // SH4_pkt_to_mem_X_movca_32_linear(dest, (unsigned char*)0x01848000, n + 2); // This takes full
    // n now
    memcpy_32bit(dest, (unsigned char *)0x81848000,
                 (n + 2 + 3) / 4); // Lol this is as fast as the asm functions
    CacheBlockInvalidate((unsigned char *)0x81848000,
                         (n + 2 + 31) / 32); // Need to invalidate the src packet
    CacheBlockWriteBack(dest, (2 + n + 31) / 32);
}

static int rtl_bb_rx() {
    int processed;
    unsigned int rx_status;
    unsigned int rx_size, pkt_size, ring_offset;
    unsigned char *pkt;

    processed = 0;

    /* While we have frames left to process... */
    while(!(nic8[RT_CHIPCMD] & 1)) {

        /* Get frame size and status */
        // Don't need the % there for nowrap since it happens later.
        ring_offset = rtl.cur_rx;
        // Use uncached area for this intentionally, in case of rtl_is_copying.
        // Don't want to accidentally cache an unfinished packet.
        // This also means we don't have to worry about invalidating a block holding the status
        // byte. Nice!
        rx_status = mem32[0x0000 / 4 + ring_offset / 4];
        rx_size = (rx_status >> 16) & 0xffffU;

        /* apparently this means the rtl8139 is still copying */
        if(rx_size == 0xfff0U) {
            rtl_is_copying =
                1; // Really don't want to run a DHCP renewal while data is in flight...
            break;
        }
        rtl_is_copying = 0;

        pkt_size = rx_size - 4;

// Full loop timing
#ifdef FULL_TRIP_TIMING
        unsigned long long int first_array1 = PMCR_RegRead(DCLOAD_PMCR);
#endif

        if((rx_status & 1) && (pkt_size <= RX_PKT_BUF_SIZE)) {
            pkt = (unsigned char *)(GAPS_RX_IO_AREA + 0x0000 + ring_offset +
                                    4); // + 4 to skip the status byte (DMA)

// Rx time
#ifdef RX_LOOP_TIMING
            unsigned long long int first_array = PMCR_RegRead(DCLOAD_PMCR);
#endif
            // SH4_pkt_to_mem() will shift it by 2 for current_pkt
            pktcpy(raw_current_pkt, pkt, pkt_size);

// Rx time end
#ifdef RX_LOOP_TIMING
            unsigned long long int second_array = PMCR_RegRead(DCLOAD_PMCR);
            unsigned int loop_difference = (unsigned int)(second_array - first_array);

            clear_lines(246, 24, global_bg_color);
            uint_to_string_dec(loop_difference, (char *)uint_string_array);
            draw_string(30, 246, uint_string_array, STR_COLOR);
#endif

// Process time
#ifdef PKT_PROCESS_TIMING
            asm volatile("nop\n" : : : "memory");
            unsigned long long int first_array2 = PMCR_RegRead(DCLOAD_PMCR);
#endif

			//process_pkt(current_pkt);
			process_pkt(to_p1(current_pkt));

// Process time end
#ifdef PKT_PROCESS_TIMING
            unsigned long long int second_array2 = PMCR_RegRead(DCLOAD_PMCR);
            unsigned int loop_difference2 = (unsigned int)(second_array2 - first_array2);

            clear_lines(270, 24, global_bg_color);
            uint_to_string_dec(loop_difference2, (char *)uint_string_array);
            draw_string(30, 270, uint_string_array, STR_COLOR);
#endif
        }

        // Align next packet to 4-bytes (add 4 to account for transmit status; the 4 extra bytes
        // included in rx_size are the CRC)
        rtl.cur_rx = (rtl.cur_rx + rx_size + 4 + 3) & ~3;

        if(rtl.cur_rx >= RX_BUFFER_LEN) {
            // Prevent underflowing the RX buffer
            rtl.cur_rx %= RX_BUFFER_LEN;
            nic16[RT_RXBUFTAIL / 2] = 0x7ff0;
            // According to the RTL8139C datasheet, 0xfff0 = 65520 is the default value of the
            // register, and the register cannot be written to before data has been read from the
            // buffer for some reason. So, presumably, we can just use that value here.
            //
            // Although, in the specific case of this system with the GAPS PCI Bridge, we can also
            // just use 0x7ff0 since that's a well-known memory location (it's the last 16 bytes of
            // the last txdesc. Because each txdesc is 2048 bytes and no more than 1536 bytes will
            // ever be written there, it seems like a pretty safe place to put... whatever that
            // apparently 100% necessary offset of -16 is for).
        }
        else {
            rtl.cur_rx %= RX_BUFFER_LEN;
            nic16[RT_RXBUFTAIL / 2] = rtl.cur_rx - 16;
            // Why 16? NetBSD and Linux do this, too. Status is 4, CRC appended is 4, what's the
            // other 8? Things don't work if this isn't 16, anyways (I tried changing it). Maybe
            // this is 16 for DMA reasons? RealTek does it here:
            // https://www.cs.usfca.edu/~cruse/cs326f04/RTL8139_ProgrammersGuide.pdf Maybe this is
            // why 16: initial value is 0x0fff0 according to the RTL8139C datasheet:
            // https://people.freebsd.org/~wpaul/RealTek/spec-8139c(160).pdf
            // This stays the same regardless of wrap/nowrap, as well.
            // Wow, even QEMU emulates this "off by 16" thing here:
            // https://github.com/qemu/qemu/blob/master/hw/net/rtl8139.c#L2532
        }

        // Ack it
        unsigned short i = nic16[RT_INTRSTATUS / 2];
        if(i & RT_INT_RX_ACK)
            nic16[RT_INTRSTATUS / 2] = RT_INT_RX_ACK;

        processed++;

#ifdef FULL_TRIP_TIMING
        unsigned long long int second_array1 = PMCR_RegRead(DCLOAD_PMCR);
        unsigned int loop_difference1 = (unsigned int)(second_array1 - first_array1);

        clear_lines(412, 24, global_bg_color);
        uint_to_string_dec(loop_difference1, (char *)uint_string_array);
        draw_string(30, 412, uint_string_array, STR_COLOR);
#endif
    }

    return processed;
}

void rtl_bb_loop(int is_main_loop) {
    unsigned int intr = 0;
    unsigned int loop_start[2] = {0};
    unsigned int loop_measure[2] = {0};
    unsigned int prev_loop_elapsed = 0;

    if(is_main_loop) {
        if(!(booted || running)) {
            disp_info();
        }

        // Need to wait for a link change before it's OK to do anything
        rtl_link_up = 0;
    }

    if(timeout_loop > 0) {
        PMCR_Read(DCLOAD_PMCR, loop_start);
    }

    // OMG this is polling the network adapter. Well, ok then.
    while(!escape_loop) {

        /* Check interrupt status */
        if(nic16[RT_INTRSTATUS / 2] != intr) {
            intr = nic16[RT_INTRSTATUS / 2];
            nic16[RT_INTRSTATUS / 2] = intr & ~RT_INT_RX_ACK;
        }

        /* Did we receive some data? */
        if(intr & RT_INT_RX_ACK) {
            // i = rtl_bb_rx();
            rtl_bb_rx();
        }

        /* link change */
        if(__builtin_expect(intr & RT_INT_RXFIFO_UNDERRUN, 0)) {

            if(booted && (!running)) {
                disp_status("link change...");
            }

            nic16[RT_MII_BMCR / 2] = 0x9200;

            /* wait for valid link */
            while(!(nic16[RT_MII_BMSR / 2] & 0x20))
                ;

            /* wait for the additional link change interrupt that is coming */
            while(!(nic16[RT_INTRSTATUS / 2] & RT_INT_RXFIFO_UNDERRUN))
                ;
            nic16[RT_INTRSTATUS / 2] = RT_INT_RXFIFO_UNDERRUN;

            if(booted && (!running)) {
                disp_status("idle...");
            }

            /* if we were waiting in a loop with a timeout when link changed, timeout
             * immediately upon bringing link back up, so we can retry immediately */
            if(timeout_loop > 0) {
                dhcp_attempts = 0;
                timeout_loop = -1;
                escape_loop = 1;
            }

            rtl_link_up = 1; // Good to go!
        }

        /* Rx FIFO overflow */
        if(intr & RT_INT_RXFIFO_OVERFLOW) {
            /* must clear Rx Buffer Overflow too for some reason */
            // It's an errata (hardware bug/quirk), this needs to be done.
            nic16[RT_INTRSTATUS / 2] = RT_INT_RXBUF_OVERFLOW;
        }

        /* Rx Buffer overflow */
        if(intr & RT_INT_RXBUF_OVERFLOW) {
            /*
                        // Update CAPR
                        rtl.cur_rx = nic16[RT_RXBUFHEAD];
                        nic16[RT_RXBUFTAIL] = rtl.cur_rx - 16;
                        rtl.cur_rx = 0;

                        // Disable receive
                        nic8[RT_CHIPCMD] = RT_CMD_TX_ENABLE;

                        // Wait for it
                        while ( !(nic8[RT_CHIPCMD] & RT_CMD_RX_ENABLE))
                            nic8[RT_CHIPCMD] = RT_CMD_TX_ENABLE | RT_CMD_RX_ENABLE; // Weirdly, keep
               spamming re-enable receive

                        // Re-set RXCONFIG
                        nic32[RT_RXCONFIG/4] = 0x0000f60a; // This should be whatever is set in init
               plus enabling packet reception (| 0x...a)

                        // clear interrupts
                        nic16[RT_INTRSTATUS/2] = 0xffff;
                */
            // NetBSD, FreeBSD, and OpenBSD all just do a full re-init if this happens.
            rtl_init();
        }

        if(is_main_loop && rtl_link_up && (!rtl_is_copying)) // Only want this to run in main loop
        {
            // Do we need to renew our IP address?
            // This will override set_ip_from_file() if the ip is in the 0.0.0.0/8 range
            set_ip_dhcp();
        }

        if(timeout_loop > 0) {
            PMCR_Read(DCLOAD_PMCR, loop_measure);
            unsigned int loop_secs_elapsed =
                (unsigned int)((*(unsigned long long int *)loop_measure -
                                *(unsigned long long int *)loop_start) /
                               200000000);
            if(prev_loop_elapsed != loop_secs_elapsed) {
                if(dhcp_attempts > 1) // Don't show a counter yet if it's the first attempt
                {
                    disp_dhcp_attempts_count();
                    disp_dhcp_next_attempt(timeout_loop - loop_secs_elapsed + 1);
                }
                if(loop_secs_elapsed > (unsigned int)timeout_loop) {
                    timeout_loop = -1;
                    escape_loop = 1;
                }
                prev_loop_elapsed = loop_secs_elapsed;
            }
        }
    }
    escape_loop = 0;
}
