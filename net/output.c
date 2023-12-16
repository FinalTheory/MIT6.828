#include "ns.h"

extern union Nsipc nsipcbuf;

#define PKTMAP		0x10000000

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";
	// 	- read a packet from the network server
	//	- send the packet to the device driver

    cprintf("Network output env is running\n");
    envid_t src_env_id;
    struct jif_pkt *packet = (struct jif_pkt *)PKTMAP;
    int ret;
    while (1) {
        ipc_recv(&src_env_id, (void*)PKTMAP, 0);
        do {
//            cprintf("Sending packet from env 0x%08x\n", src_env_id);
            ret = sys_packet_try_send(packet->jp_data, packet->jp_len);
            if (ret == -E_INVAL) {
                panic("Invalid packet length: %d\n", packet->jp_len);
            }
        } while (ret == -E_BUF_FULL);
    }
}
