#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

typedef struct {
    uint8_t left_shift;
    uint8_t right_shift;
} KeyboardState;

#define KB_EVENT_UP   ((char)0x11)
#define KB_EVENT_DOWN ((char)0x12)

void keyboard_init(void);
void keyboard_handler(void);
void keyboard_set_shell_task(int id);
const KeyboardState* keyboard_get_state(void);
void kb_push(char c);
char kb_getchar(void);
int  kb_available(void);

#endif
