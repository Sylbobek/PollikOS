/* Interactive console TTY for x86_64.
 *
 * The legacy console is a polled COM1 transmit path plus the host terminal
 * emulator on the other side. This module adds the missing receive half: a
 * bounded byte queue filled from the UART (drained on the timer tick) that
 * userspace read(0, ...) consumes. The queue is deliberately small and raw;
 * line editing, echo and history live in the userspace shell, so a future
 * canonical/termios mode can layer on top without kernel changes.
 */
#include "tty.h"
#include "scheduler.h"
#include "mouse.h"
#include "console_fb.h"
#include "window.h"
#include "../../hal.h"
extern void kernel64_debug_bytes(const char *data, size_t length);

#define TTY64_QUEUE 1024
#define TTY64_POLL_LIMIT 64
#define TTY64_INTR 0x03
static uint8_t queue[TTY64_QUEUE];
static unsigned head, tail, count;
static int enabled;
static uint64_t foreground_pgid;
static int shift_left, shift_right, control, alt, extended;

static uint8_t in8(uint16_t port) {
    return hal_port_read8(port);
}
void tty64_init(void) { head = tail = count = 0; foreground_pgid = 0; }
void tty64_enable(void) {
    enabled = 1;
    if (mouse64_init()) {
        static const char message[]="[INPUT64] PS/2 mouse enabled\n";
        static const char wheel[]="[INPUT64] PS/2 wheel packets enabled\n";
        console_fb_write(message,sizeof(message)-1);
        kernel64_debug_bytes(mouse64_wheel_enabled()?wheel:message,
                            mouse64_wheel_enabled()?sizeof(wheel)-1:sizeof(message)-1);
    } else {
        static const char message[]="[INPUT64] PS/2 mouse unavailable\n";
        console_fb_write(message,sizeof(message)-1);
    }
}
int tty64_enabled(void) { return enabled; }
void tty64_set_foreground(uint64_t pgid) { foreground_pgid = pgid; }
static void queue_byte(uint8_t byte) {
    if (byte == TTY64_INTR && foreground_pgid) {
        (void)process64_signal_group(foreground_pgid, USER_SIG_INT);
        foreground_pgid = 0;
        return;
    }
    if (count >= TTY64_QUEUE) return;
    queue[tail]=byte; tail=(tail+1)%TTY64_QUEUE; ++count;
}
static void queue_escape(uint8_t final) { queue_byte(0x1b); queue_byte('['); queue_byte(final); }
static uint32_t key_modifiers(void) {
    return (shift_left||shift_right?USER_INPUT_MOD_SHIFT:0) |
           (control?USER_INPUT_MOD_CONTROL:0) | (alt?USER_INPUT_MOD_ALT:0);
}
static void ps2_key(uint8_t code) {
    static const uint8_t plain[128] = {
        [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
        [12]='-',[13]='=',[14]=0x7f,[15]='\t',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',
        [21]='y',[22]='u',[23]='i',[24]='o',[25]='p',[26]='[',[27]=']',[28]='\r',[30]='a',
        [31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',[39]=';',
        [40]='\'',[41]='`',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',
        [50]='m',[51]=',',[52]='.',[53]='/',[57]=' '
    };
    static const uint8_t shifted[128] = {
        [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',[10]='(',[11]=')',
        [12]='_',[13]='+',[26]='{',[27]='}',[39]=':',[40]='"',[41]='~',[43]='|',
        [51]='<',[52]='>',[53]='?'
    };
    if (code==0xe0) { extended=1; return; }
    int released=code&0x80; code&=0x7f;
    int was_extended=extended;
    extended=0;
    uint32_t modifier_key=0;
    if (!was_extended && code==42) { shift_left=!released; modifier_key=USER_KEY_LEFT_SHIFT; }
    else if (!was_extended && code==54) { shift_right=!released; modifier_key=USER_KEY_RIGHT_SHIFT; }
    else if (code==29) { control=!released; modifier_key=USER_KEY_CONTROL; }
    else if (code==56) { alt=!released; modifier_key=USER_KEY_ALT; }
    if (modifier_key) {
        (void)window64_key_event(modifier_key,key_modifiers(),!released);
        return;
    }

    uint32_t key=0;
    if (was_extended) {
        if (code==72) key=USER_KEY_UP;
        else if (code==80) key=USER_KEY_DOWN;
        else if (code==75) key=USER_KEY_LEFT;
        else if (code==77) key=USER_KEY_RIGHT;
        else if (code==71) key=USER_KEY_HOME;
        else if (code==79) key=USER_KEY_END;
        else if (code==83) key=USER_KEY_DELETE;
    } else if (code==14) key=8;
    else if (code==15) key='\t';
    else if (code==28) key='\r';
    else if (code==1) key=27;
    else if (code<sizeof(plain)) {
        key=plain[code];
        if (key>='a'&&key<='z' && (shift_left||shift_right)) key-=32;
        else if ((shift_left||shift_right) && shifted[code]) key=shifted[code];
    }
    if (!key) return;
    if (window64_key_event(key,key_modifiers(),!released) || released) return;
    if (was_extended) {
        if (key==USER_KEY_UP) queue_escape('A'); else if (key==USER_KEY_DOWN) queue_escape('B');
        else if (key==USER_KEY_LEFT) queue_escape('D'); else if (key==USER_KEY_RIGHT) queue_escape('C');
        else if (key==USER_KEY_HOME) queue_escape('H'); else if (key==USER_KEY_END) queue_escape('F');
        else if (key==USER_KEY_DELETE) { queue_byte(0x1b); queue_byte('['); queue_byte('3'); queue_byte('~'); }
        return;
    }
    if (key>='a'&&key<='z' && control) key=(uint32_t)(key-'a'+1);
    else if (control && key>='A'&&key<='Z') key=(uint32_t)(key-'A'+1);
    if (key==8) key=0x7f; /* keep the shell's existing DEL backspace contract */
    if (key) queue_byte((uint8_t)key);
}
/* Called from the PIT handler while the scheduler runs: drain whatever the
 * 16550 has buffered. Reading LSR/RBR only; no UART interrupt is enabled. */
void tty64_poll(void) {
    if (!enabled) return;
    unsigned limit = TTY64_POLL_LIMIT;
    while (limit-- && (in8(0x3fd) & 0x01)) {
        uint8_t byte = in8(0x3f8);
        queue_byte(byte);
    }
    limit=TTY64_POLL_LIMIT;
    while (limit-- && (in8(0x64)&1)) {
        uint8_t status=in8(0x64), byte=in8(0x60);
        if (status&0x20) mouse64_byte(byte);
        else ps2_key(byte);
    }
}
size_t tty64_available(void) { return count; }
size_t tty64_pop(void *destination, size_t maximum) {
    uint8_t *out = destination;
    size_t taken = 0;
    while (taken < maximum && count) {
        out[taken++] = queue[head];
        head = (head + 1) % TTY64_QUEUE;
        --count;
    }
    return taken;
}
