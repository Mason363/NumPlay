/* A save state is the SM5xx processor's registers and RAM (sm510_t). Whatever a state file
 * holds, the processor's fixed setup (its widths, masks, stack, pins) stays as it started, and
 * its registers inside it: no read outside its RAM or its program. */
#include "gw_type_defs.h"
#define GW_NO_SM510_DEFINES
#include "sm510.h"

static sm510_t chip; /* the processor as it started */

void gw_chip_keep(void) { chip = sm510; }

void gw_chip_fix(void) {
  sm510_t *c = &sm510;
  c->m_prgwidth = chip.m_prgwidth, c->m_datawidth = chip.m_datawidth;
  c->m_prgmask = chip.m_prgmask, c->m_datamask = chip.m_datamask;
  c->m_stack_levels = chip.m_stack_levels, c->m_r_mask_option = chip.m_r_mask_option;
  c->m_o_pins = chip.m_o_pins, c->flag_lcd_deflicker_level = chip.flag_lcd_deflicker_level;
  if (c->m_clk_div != 2 && c->m_clk_div != 4) c->m_clk_div = chip.m_clk_div;
  c->m_pc &= (u16)chip.m_prgmask, c->m_prev_pc &= (u16)chip.m_prgmask;
  for (int i = 0; i < 4; i++) c->m_stack[i] &= (u16)chip.m_prgmask;
  c->m_icount = 0;
  c->m_acc &= 15, c->m_bl &= 15, c->m_bm &= 15, c->m_c &= 1;
  c->m_cn &= 1, c->m_mx &= 1, c->m_cb &= 1;
  for (int i = 0; i < 128; i++) c->gw_ram[i] &= 15, c->gw_ram_state[i] &= 15;
  /* (the flags read as the bytes they are: a bool holding anything but 0 or 1 isn't one) */
#define FLAG(f) c->f = *(const volatile unsigned char *)&c->f != 0
  FLAG(m_sbm), FLAG(m_sbl), FLAG(m_skip), FLAG(m_k_active), FLAG(m_halt), FLAG(m_1s), FLAG(m_bc), FLAG(m_rsub);
}
