#include "vim.h"

enum {
  NORMAL_INITIAL,
  NORMAL_GETCOUNT_BEFORE,
  NORMAL_GETCOUNT_AFTER,
  NORMAL_CHECK_CURSORHOLD
} normal_state;

cmdarg_T ca;
oaparg_T oap;
int toplevel;


void init_state() {
  toplevel = true;
  vim_memset(&ca, 0, sizeof(ca));
}


void main_loop2() {
  int c;

  while (true) {
    c = safe_vgetc();
    LANGMAP_ADJUST(c, TRUE);
    switch (State) {
      case NORMAL:
      case NORMAL_BUSY:
      case REPLACE:
      case VREPLACE:
      case LREPLACE:
        switch (normal_state) {
          case NORMAL_INITIAL:
            keypress_normal_initial(c);
            break;
          case NORMAL_GETCOUNT_BEFORE:
            keypress_normal_getcount_before(c);
            --no_zero_mapping;
            if (ctrl_w) {
              --no_mapping;
              --allow_keys;
            }
            need_flushbuf |= add_to_showcmd(c);
            break;
          case NORMAL_GETCOUNT_AFTER:
            keypress_normal_getcount_after(c);
            break;
        }
        break;
      case SETWSIZE:
      case SHOWMATCH:
      case ASKMORE:
      case CONFIRM:
      case EXTERNCMD:
      case HITRETURN:
      case INSERT:
      case LANGMAP:
        fprintf(stderr, "Unkown state\n");
        exit(1);
    }
  }
}


void keypress_normal_initial(int c) {
  /* Use a count remembered from before entering an operator.  After typing
   * "3d" we return from normal_cmd() and come back here, the "3" is
   * remembered in "opcount". */
  ca.opcount = opcount;
  /*
   * If there is an operator pending, then the command we take this time
   * will terminate it. Finish_op tells us to finish the operation before
   * returning this time (unless the operation was cancelled).
   */
#ifdef CURSOR_SHAPE
  c = finish_op;
#endif
  finish_op = (oap->op_type != OP_NOP);
#ifdef CURSOR_SHAPE
  if (finish_op != c) {
    ui_cursor_shape();                  /* may show different cursor shape */
  }
#endif
  /* When not finishing an operator and no register name typed, reset the
   * count. */
  if (!finish_op && !oap->regname) {
    ca.opcount = 0;
    set_prevcount = TRUE;
  }
  /* Restore counts from before receiving K_CURSORHOLD.  This means after
   * typing "3", handling K_CURSORHOLD and then typing "2" we get "32", not
   * "3 * 2". */
  if (oap->prev_opcount > 0 || oap->prev_count0 > 0) {
    ca.opcount = oap->prev_opcount;
    ca.count0 = oap->prev_count0;
    oap->prev_opcount = 0;
    oap->prev_count0 = 0;
  }
  mapped_len = typebuf_maplen();
  State = NORMAL_BUSY;
#ifdef USE_ON_FLY_SCROLL
  dont_scroll = FALSE;          /* allow scrolling here */
#endif
  /* Set v:count here, when called from main() and not a stuffed
   * command, so that v:count can be used in an expression mapping
   * when there is no count. */
  if (toplevel && stuff_empty())
    set_vcount_ca(&ca, &set_prevcount);
  /*
   * If a mapping was started in Visual or Select mode, remember the length
   * of the mapping.  This is used below to not return to Insert mode for as
   * long as the mapping is being executed.
   */
  if (restart_edit == 0)
    old_mapped_len = 0;
  else if (old_mapped_len
           || (VIsual_active && mapped_len == 0 && typebuf_maplen() > 0))
    old_mapped_len = typebuf_maplen();

  if (c == NUL)
    c = K_ZERO;

  /*
   * In Select mode, typed text replaces the selection.
   */
  if (VIsual_active
      && VIsual_select
      && (vim_isprintc(c) || c == NL || c == CAR || c == K_KENTER)) {
    /* Fake a "c"hange command.  When "restart_edit" is set (e.g., because
     * 'insertmode' is set) fake a "d"elete command, Insert mode will
     * restart automatically.
     * Insert the typed character in the typeahead buffer, so that it can
     * be mapped in Insert mode.  Required for ":lmap" to work. */
    ins_char_typebuf(c);
    if (restart_edit != 0)
      c = 'd';
    else
      c = 'c';
    msg_nowait = TRUE;          /* don't delay going to insert mode */
    old_mapped_len = 0;         /* do go to Insert mode */
  }

  need_flushbuf = add_to_showcmd(c);
}

void keypress_normal_getcount_before(int c) {
  if (!VIsual_active || !VIsual_select) {
    normal_state = cursorhold;
    return;
  }
  /*
   * Handle a count before a command and compute ca.count0.
   * Note that '0' is a command and not the start of a count, but it's
   * part of a count after other digits.
   */
  if ((c >= '1' && c <= '9') || (ca.count0 != 0 &&
        (c == K_DEL || c == K_KDEL || c == '0'))) {
    if (c == K_DEL || c == K_KDEL) {
      ca.count0 /= 10;
      del_from_showcmd(4);            /* delete the digit and ~@% */
    } else
      ca.count0 = ca.count0 * 10 + (c - '0');
    if (ca.count0 < 0)            /* got too large! */
      ca.count0 = 999999999L;
    /* Set v:count here, when called from main() and not a stuffed
     * command, so that v:count can be used in an expression mapping
     * right after the count. */
    if (toplevel && stuff_empty())
      set_vcount_ca(&ca, &set_prevcount);
    if (ctrl_w) {
      ++no_mapping;
      ++allow_keys;                   /* no mapping for nchar, but keys */
    }
    ++no_zero_mapping;                /* don't map zero here */
    normal_state = NORMAL_GETCOUNT_AFTER;
  }
}

void keypress_normal_getcount_after(c) {
  /*
   * If we got CTRL-W there may be a/another count
   */
  if (c == Ctrl_W && !ctrl_w && oap->op_type == OP_NOP) {
    ctrl_w = TRUE;
    ca.opcount = ca.count0;           /* remember first count */
    ca.count0 = 0;
    ++no_mapping;
    ++allow_keys;                     /* no mapping for nchar, but keys */
    c = plain_vgetc();                /* get next character */
    LANGMAP_ADJUST(c, TRUE);
    --no_mapping;
    --allow_keys;
    need_flushbuf |= add_to_showcmd(c);
    return;
  }

  normal_state = NORMAL_CHECK_CURSORHOLD;
}
