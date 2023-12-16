#include <kern/e1000.h>
#include <kern/e1000_hw.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/error.h>

volatile uint32_t *e1000_base = NULL;

#define E1000_TX_BUF_SIZE 32
#define E1000_RX_BUF_SIZE 128
#define ETHERNET_PACKET_SIZE 1518
#define REG_INDEX(REG_OFFSET) (REG_OFFSET / sizeof(*e1000_base))

static struct e1000_tx_desc tx_desc_buffer[E1000_TX_BUF_SIZE] __attribute__ ((aligned (16)));
static struct e1000_rx_desc rx_desc_buffer[E1000_RX_BUF_SIZE] __attribute__ ((aligned (16)));
static char tx_packet_buffer[E1000_TX_BUF_SIZE][ETHERNET_PACKET_SIZE];
static char rx_packet_buffer[E1000_RX_BUF_SIZE][2048];

int e1000_send_packet(void *buffer, size_t size) {
    uint32_t tail = e1000_base[REG_INDEX(E1000_TDT)];
    // check if the tail slot is available
    if (!(tx_desc_buffer[tail].upper.fields.status & E1000_RXD_STAT_DD)) {
        return -E_BUF_FULL;
    }
    if (size > ETHERNET_PACKET_SIZE) { return -E_INVAL; }
    memmove(tx_packet_buffer[tail], buffer, size);
    tx_desc_buffer[tail].buffer_addr = PADDR(tx_packet_buffer[tail]);
    tx_desc_buffer[tail].lower.flags.length = size;
    // set RS bit
    tx_desc_buffer[tail].lower.flags.cmd = (0x1 << 3) | (0x1);
    tx_desc_buffer[tail].lower.flags.cso = 0;
    // clear DD bit
    tx_desc_buffer[tail].upper.fields.status &= ~E1000_RXD_STAT_DD;

    // notify HW that new packet is available
    e1000_base[REG_INDEX(E1000_TDT)] = (tail + 1) % E1000_TX_BUF_SIZE;
    return size;
}


int e1000_recv_packet(void *buffer, size_t size) {
    uint32_t read_pos = (e1000_base[REG_INDEX(E1000_RDT)] + 1) % E1000_RX_BUF_SIZE;
    if (!rx_desc_buffer[read_pos].status) {
        return -E_BUF_EMPTY;
    }
    rx_desc_buffer[read_pos].status = 0;
    int length = rx_desc_buffer[read_pos].length;
    if (length > size) {
        return -E_INVAL;
    }
    assert(length <= sizeof(rx_packet_buffer[0]));
    memmove(buffer, rx_packet_buffer[read_pos], length);
    e1000_base[REG_INDEX(E1000_RDT)] = read_pos;
    return length;
}


int e1000_attach(struct pci_func *pcif) {
    pci_func_enable(pcif);
    e1000_base = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    memset(tx_desc_buffer, 0, sizeof(tx_desc_buffer));
    memset(rx_desc_buffer, 0, sizeof(rx_desc_buffer));
    // mark all TX descriptors available
    for (int i = 0; i < E1000_TX_BUF_SIZE; i++) {
        tx_desc_buffer[i].upper.fields.status |= E1000_RXD_STAT_DD;
    }
    assert(e1000_base != NULL);
    cprintf("E1000 device status register=0x%08x\n", e1000_base[REG_INDEX(E1000_STATUS)]);
    cprintf("E1000 device MMIO address space size=0x%08x\n", pcif->reg_size[0]);
    cprintf("tx desc buffer base addr=0x%08x\n", PADDR(tx_desc_buffer));
    // setup send packet
    e1000_base[REG_INDEX(E1000_TDBAH)] = 0;
    e1000_base[REG_INDEX(E1000_TDBAL)] = PADDR(tx_desc_buffer);
    e1000_base[REG_INDEX(E1000_TDLEN)] = sizeof(tx_desc_buffer);
    // fill 0 to head/tail pointer indicating empty queue
    e1000_base[REG_INDEX(E1000_TDH)] = 0;
    e1000_base[REG_INDEX(E1000_TDT)] = 0;
    e1000_base[REG_INDEX(E1000_TCTL)] = E1000_TCTL_EN | E1000_TCTL_PSP | (E1000_TCTL_CT & (0x10 << 4)) | (E1000_TCTL_COLD & (0x40 << 12));
    e1000_base[REG_INDEX(E1000_TIPG)] = (10) | (4 << 10) | (6 << 20);


    // ------------


    // setup recv
    e1000_base[REG_INDEX(E1000_RA)] = 0x12005452; // RAL[0]
    e1000_base[REG_INDEX(E1000_RA) + 1] = 0x5634 | E1000_RAH_AV; // RAH[0] with Address Valid bit set
    // Initialize the MTA (Multicast Table Array) to 0b
    for (int i = 0; i < 128; i++) {
        e1000_base[REG_INDEX(E1000_MTA) + i] = 0;
    }
    // do not enable any interrupts yet
    // e1000_base[REG_INDEX(E1000_IMS)] = E1000_IMS_RXT0 | E1000_IMS_RXO | E1000_IMS_RXDMT0 | E1000_IMS_RXSEQ | E1000_IMS_LSC;
    e1000_base[REG_INDEX(E1000_IMS)] = 0;
    e1000_base[REG_INDEX(E1000_RDBAH)] = 0;
    e1000_base[REG_INDEX(E1000_RDBAL)] = PADDR(rx_desc_buffer);
    e1000_base[REG_INDEX(E1000_RDLEN)] = sizeof(rx_desc_buffer);
    for (int i = 0; i < E1000_RX_BUF_SIZE; i++) {
        rx_desc_buffer[i].buffer_addr = PADDR(rx_packet_buffer[i]);
    }
    e1000_base[REG_INDEX(E1000_RDH)] = 0;
    e1000_base[REG_INDEX(E1000_RDT)] = E1000_RX_BUF_SIZE - 1;
    // init control register
    // RCTL.BSEX = 0b
    // RCTL.BSIZE = 00b => 2048 Bytes
    e1000_base[REG_INDEX(E1000_RCTL)] = E1000_RCTL_EN | E1000_RCTL_SECRC;
    return 1;
}

