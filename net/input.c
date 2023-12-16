#include "ns.h"

extern union Nsipc nsipcbuf;

#define PKTMAP		0x10000000

#define NUM_RECV_CACHE 16
static void *rx_buf[NUM_RECV_CACHE];

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.

    cprintf("Network input env is running\n");
    int ret;
    for (int i = 0; i < NUM_RECV_CACHE; i++) {
        rx_buf[i] = (void *)(i * PGSIZE + PKTMAP);
        if ((ret = sys_page_alloc(0, rx_buf[i], PTE_P | PTE_U | PTE_W)) < 0)
            panic("sys_page_alloc: %e", ret);
    }
    uint32_t idx = 0;
    while (1) {
        struct jif_pkt *packet = (struct jif_pkt *)rx_buf[(idx++) % NUM_RECV_CACHE];
        const int buf_size = PGSIZE - sizeof(packet->jp_len);
        ret = sys_packet_recv(packet->jp_data, buf_size);
        if (ret == -E_INVAL) {
            panic("Invalid buffer length: %d\n", buf_size);
        }
        packet->jp_len = ret;
        ipc_send(ns_envid, NSREQ_INPUT, packet, PTE_P | PTE_U);
        sys_yield();
    }
}
