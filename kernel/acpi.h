#ifndef ACPI_H
#define ACPI_H

#include "bootinfo.h"
#include "types.h"

void acpi_init(BootInfo* bootInfo);
void* acpi_find_table(const char signature[4]);

#endif
