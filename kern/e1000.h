#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>

int e1000_attach(struct pci_func *);

int e1000_send_packet(void *buffer, size_t size);

int e1000_recv_packet(void *buffer, size_t size);

#endif  // SOL >= 6
