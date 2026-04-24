#include "irq.h"
#include "pic.h"
#include "klog.h"

void irq_controller_init(void)
{
    pic_init();
    klog_info("IRQ controller: legacy PIC backend active");
}

void irq_unmask(uint8_t irq)
{
    pic_unmask(irq);
}

void irq_mask(uint8_t irq)
{
    pic_mask(irq);
}

void irq_send_eoi(uint8_t irq)
{
    pic_send_eoi(irq);
}
