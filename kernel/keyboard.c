#include "keyboard.h"
#include "display.h"
#include "io.h"
#include "irq.h"

#define KB_BUF_SIZE 64

static char    kb_buf[KB_BUF_SIZE];
static uint8_t kb_head = 0;
static uint8_t kb_tail = 0;
static int shell_task_id = -1;
static KeyboardState g_state;
static uint8_t g_have_e0 = 0;

static const char scancode_map[128] = {
    0,   0,  '1','2','3','4','5','6','7','8','9','0','-','=', '\b', 0,
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s',
    'd','f','g','h','j','k','l',';','\'','`', 0, '\\','z','x','c','v',
    'b','n','m',',','.','/', 0,  '*', 0,  ' ', 0,
};

static const char scancode_map_shift[128] = {
    0,   0,  '!','@','#','$','%','^','&','*','(',')','_','+', '\b', 0,
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0, 'A','S',
    'D','F','G','H','J','K','L',':','"','~', 0, '|','Z','X','C','V',
    'B','N','M','<','>','?', 0,  '*', 0,  ' ', 0,
};

static int shift_active(void)
{
    return g_state.left_shift || g_state.right_shift;
}

static char translate_scancode(uint8_t scancode)
{
    if (scancode >= 128)
        return 0;

    if (shift_active())
        return scancode_map_shift[scancode];
    return scancode_map[scancode];
}

void keyboard_set_shell_task(int id)
{
    shell_task_id = id;
}

const KeyboardState* keyboard_get_state(void)
{
    return &g_state;
}

void kb_push(char c)
{
    uint8_t next = (uint8_t)((kb_head + 1) % KB_BUF_SIZE);
    if (next == kb_tail) return;

    kb_buf[kb_head] = c;
    kb_head = next;

    if (shell_task_id >= 0) {
        extern void task_wake(int id);
        task_wake(shell_task_id);
    }
}

char kb_getchar(void)
{
    if (kb_head == kb_tail) return 0;

    char c = kb_buf[kb_tail];
    kb_tail = (uint8_t)((kb_tail + 1) % KB_BUF_SIZE);
    return c;
}

int kb_available(void)
{
    return kb_head != kb_tail;
}

void keyboard_init(void)
{
    while (inb(0x64) & 0x01) inb(0x60);

    outb(0x64, 0x20);
    while (!(inb(0x64) & 0x01));
    uint8_t config = inb(0x60);

    config |= (1 << 6);
    config |= (1 << 0);

    outb(0x64, 0x60);
    while (inb(0x64) & 0x02);
    outb(0x60, config);

    outb(0x64, 0xAE);

    g_state.left_shift = 0;
    g_state.right_shift = 0;
    g_have_e0 = 0;

    print("Keyboard initialized\n");
}

void keyboard_handler(void)
{
    uint8_t scancode = inb(0x60);
    irq_note_keyboard_irq();

    if (scancode == 0xE0) {
        g_have_e0 = 1;
        return;
    }

    if (g_have_e0) {
        g_have_e0 = 0;
        // Arrow up/down for shell history.
        if (scancode == 0x48) kb_push(KB_EVENT_UP);
        else if (scancode == 0x50) kb_push(KB_EVENT_DOWN);
        return;
    }

    uint8_t key = (uint8_t)(scancode & 0x7F);
    int released = (scancode & 0x80) != 0;

    if (key == 0x2A)
        g_state.left_shift = released ? 0 : 1;
    else if (key == 0x36)
        g_state.right_shift = released ? 0 : 1;

    if (released)
        return;

    char c = translate_scancode(key);
    if (c)
        kb_push(c);
}
