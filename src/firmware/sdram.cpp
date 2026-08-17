#include "sdram.h"

SDRAM_DATA volatile uint32_t g_sdram_data_probe_words[16];
SDRAM_DATA volatile uint32_t g_sdram_test_scratch_words[SDRAM_TEST_SCRATCH_WORDS];
