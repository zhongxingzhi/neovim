#ifndef NEOVIM_IO_H
#define NEOVIM_IO_H

#include "types.h"

void io_init();
void mch_exit(int);
void io_teardown();
int mch_inchar(char_u *, int, long, int);
int mch_char_avail(void);
void mch_delay(long, int);
int mch_call_shell(char_u *, int);

#endif /* NEOVIM_IO_H */
