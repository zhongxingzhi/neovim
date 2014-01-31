#ifndef NEOVIM_IO_H
#define NEOVIM_IO_H

#include "types.h"

void io_init();
int mch_inchar(char_u *buf, int maxlen, long wtime, int tb_change_cnt);
int mch_char_avail(void);
void mch_delay(long msec, int ignoreinput);

#endif /* NEOVIM_IO_H */
