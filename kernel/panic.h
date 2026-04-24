#ifndef PANIC_H
#define PANIC_H

void panic(const char* message);
void kassert(int condition, const char* message);

#endif
