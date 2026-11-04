/* ce_emu: what the harness (ce_emu.c) and its OS (os.c) share. */
#ifndef CE_EMU_H
#define CE_EMU_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* TI-OS's RAM, as the CE toolchain's ti84pceg.inc names it */
#define FLAGS 0xD00080
#define KBD_SCAN_CODE 0xD00587
#define CUR_ROW 0xD00595
#define CUR_COL 0xD00596
#define OP1 0xD005F8
#define OP4 0xD00619
#define TEXT_SHADOW 0xD006C0
#define ERR_NO 0xD008DF
#define ERR_SP 0xD008E0
#define ASM_PRGM_SIZE 0xD0118C
#define TEMP_MEM 0xD02587
#define FPS_BASE 0xD0258A
#define FPS 0xD0258D
#define OP_BASE 0xD02590
#define OPS 0xD02593
#define P_TEMP_CNT 0xD02596
#define P_TEMP 0xD0259A
#define PROG_PTR 0xD0259D
#define NEW_DATA_PTR 0xD025A0
#define STACK_BOT 0xD1987E
#define STACK_TOP 0xD1A87E
#define USER_MEM 0xD1A881
#define SYM_TABLE 0xD3FFFF
#define VRAM 0xD40000
#define ARC_START 0x0C0000
#define ARC_END 0x3B0000

/* the flash image's own entry points (rom.S) */
#define HLE_BASE 0x023000
#define HLE_SLOTS 64
enum { HLE_START, HLE_EXIT, HLE_GETKEY_CODE };

/* a variable of a .8xp/.8xv/.8xg file */
typedef struct {
  uint8_t type, version, flag; /* flag 0x80: archived */
  uint8_t name[8], namelen;
  uint8_t *data; /* with its 2-byte size first, as in memory */
  uint32_t size;
} tivar_t;

/* memory */
uint8_t *mem_at(uint32_t addr, uint32_t len);
uint8_t rd8(uint32_t a);
uint32_t rd16(uint32_t a);
uint32_t rd24(uint32_t a);
void wr8(uint32_t a, uint8_t v);
void wr16(uint32_t a, uint32_t v);
void wr24(uint32_t a, uint32_t v);

/* os.c */
int ti_read_file(const char *path, tivar_t *out, int max);
void os_setup(void);
bool os_add_var(const tivar_t *v, bool archived);
bool os_set_program(const char *name);
bool os_trap(uint32_t pc);
void os_names_load(const char *inc_path);
const char *os_name(uint32_t addr);
void os_report(FILE *f);
int os_save_vars(const char *dir);
extern bool os_trace;
extern double os_float_scale;
extern uint64_t os_float_cycles;

/* ce_emu.c, for os.c */
void emu_stop(const char *status, const char *fmt, ...);
void emu_warn(const char *fmt, ...);
void emu_text(const char *s);
bool emu_key_held(int group, int bit);
uint32_t emu_key_presses(int group, int bit);
uint64_t emu_ms(void);
void emu_charge(uint32_t cycles);
void emu_jump(uint32_t pc);
void emu_program_started(void);
void emu_program_exited(uint32_t code);
#endif
