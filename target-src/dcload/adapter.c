#include "adapter.h"
#include "rtl8139.h"
#include "lan_adapter.h"

volatile unsigned char escape_loop = 0;
int timeout_loop = 0;
int loop_secs_elapsed = 0;

volatile unsigned int adapter_watchdog_counter = 0;
volatile unsigned char adapter_state = ADAPTER_STATE_INIT;
static volatile unsigned int init_attempt_count = 0;

adapter_t * bb;

__attribute__((aligned(32))) unsigned char raw_current_pkt[RAW_RX_PKT_BUF_SIZE];
__attribute__((aligned(2))) unsigned char * current_pkt = &(raw_current_pkt[2]);

#define ADAPTER_MAX_INIT_RETRIES 5
#define ADAPTER_INIT_DELAY_BASE 20000
#define ADAPTER_INIT_DELAY_MULT 2

static void adapter_delay_us(unsigned int us)
{
	volatile unsigned int *g2 = (volatile unsigned int*)0xa05f688c;
	unsigned int cycles = us * 25;
	while (cycles-- > 0)
		(void)*g2;
}

static void adapter_exponential_backoff(unsigned int attempt)
{
	unsigned int delay = ADAPTER_INIT_DELAY_BASE;
	unsigned int i;
	for (i = 0; i < attempt && i < 4; i++)
		delay *= ADAPTER_INIT_DELAY_MULT;
	adapter_delay_us(delay);
}

int adapter_detect(void)
{
	int init_result = -1;
	int retry_count;
	int detect_retries = 3;

	adapter_state = ADAPTER_STATE_DETECTING;
	bb = (adapter_t *)0;
	init_attempt_count = 0;

	while (detect_retries-- > 0 && bb == (adapter_t *)0) {
		if (adapter_bba.detect() >= 0) {
			bb = &adapter_bba;
			adapter_state = ADAPTER_STATE_DETECTED;
		} else if (adapter_la.detect() >= 0) {
			bb = &adapter_la;
			adapter_state = ADAPTER_STATE_DETECTED;
		} else {
			adapter_delay_us(50000);
		}
	}

	if (bb == (adapter_t *)0) {
		adapter_state = ADAPTER_STATE_ERROR;
		return -1;
	}

	adapter_state = ADAPTER_STATE_INITING;
	retry_count = ADAPTER_MAX_INIT_RETRIES;

	while (retry_count > 0) {
		init_attempt_count++;
		init_result = bb->init();
		if (init_result >= 0)
			break;
		retry_count--;
		if (retry_count > 0)
			adapter_exponential_backoff(ADAPTER_MAX_INIT_RETRIES - retry_count);
	}

	if (init_result < 0) {
		adapter_state = ADAPTER_STATE_ERROR;
		return -2;
	}

	adapter_state = ADAPTER_STATE_READY;
	escape_loop = 0;
	adapter_watchdog_counter = 0;
	return 0;
}
