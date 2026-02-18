#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

// US QWERTY scancode set 1
static const char scancode_map[128] = {
    0,   0,  '1','2','3','4','5','6','7','8','9','0','-','=', 0,  0,  // 0x00-0x0F
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s', // 0x10-0x1F
    'd','f','g','h','j','k','l',';','\'','`', 0, '\\','z','x','c','v',// 0x20-0x2F
    'b','n','m',',','.','/', 0,  '*', 0,  ' ', 0,                     // 0x30-0x3A
};

void keyboard_init(void);
void keyboard_handler(void);

#endif