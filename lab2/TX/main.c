
/* main.c
 * 
 * sample TX program for DPDK Lab
 * EECE.7290 Special Topics on SDN 
 * University of Massachusetts Lowell
 * 2018
 */
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>
#include <sys/queue.h>
#include <time.h>

#include <netinet/ip.h>
#include <netinet/udp.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <arpa/inet.h> 
#include <unistd.h>

#include <rte_common.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ring.h>
#include <rte_sched.h>
#include <cmdline_parse.h>
#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_eal.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_port_ethdev.h>
#include <rte_port_ring.h>
#include <rte_pipeline.h>


#define PORT_MASK                                0x01 //only 1 port, change it for different case
                                                      //which follows the global device[]

#ifdef RTE_MAX_ETHPORTS
#undef RTE_MAX_ETHPORTS
#define RTE_MAX_ETHPORTS                         2 //The maxinumber of NICs on the Dell server
#endif

#ifdef RTE_MAX_LCORE
#undef RTE_MAX_LCORE
#define RTE_MAX_LCORE                            16 //The Dell server has only 12 cores
#endif

#define DEFAULT_PKT_BURST                        128
#define DEFAULT_TX_DESC                          512  
#define MAX_MBUFS_PER_PORT                       1024
#define MBUF_CACHE_SIZE                          256 
#define DEFAULT_BUFF_SIZE                        2048
#define DEFAULT_PRIV_SIZE                        0
#define MBUF_SIZE                                (DEFAULT_BUFF_SIZE - sizeof(struct rte_mbuf) - DEFAULT_PRIV_SIZE)

#define FCS_SIZE                                 4
#ifndef ETHER_MAX_LEN
#define ETHER_MAX_LEN                            1518
#endif
#define MAX_PKT_SIZE                             (ETHER_MAX_LEN - FCS_SIZE)
//#define PKT_SIZE                                 MBUF_SIZE


#define PKTQ_HWQ_OUT_BURST_SIZE                  DEFAULT_PKT_BURST // burst size of tx queues

#define PAYLOAD_SIZE                             22
//==========================================================================================================
//==========================================================================================================
struct rte_mempool* mempool[RTE_MAX_ETHPORTS][RTE_MAX_LCORE];
struct rte_mbuf* app_mtable[RTE_MAX_ETHPORTS][RTE_MAX_LCORE][PKTQ_HWQ_OUT_BURST_SIZE+1];
uint32_t lcoreid_to_queueid[RTE_MAX_ETHPORTS][RTE_MAX_LCORE];
#define APP_THREAD_SENDNUMPKTS_ROUND             (64 *1024 * 1024 / PKTQ_HWQ_OUT_BURST_SIZE)     
//==========================================================================================================
struct app_mempool_params {
	uint32_t pool_size;
	uint32_t priv_size;
	uint32_t data_size;
	uint32_t cache_size;
};
//
static const struct app_mempool_params mempool_params_default = {
	.pool_size = MAX_MBUFS_PER_PORT,
	.priv_size = DEFAULT_PRIV_SIZE,
	.data_size = MBUF_SIZE,
	.cache_size = ((MBUF_CACHE_SIZE > RTE_MEMPOOL_CACHE_MAX_SIZE) ? RTE_MEMPOOL_CACHE_MAX_SIZE : MBUF_CACHE_SIZE),
};
//==========================================================================================================
struct app_link_params {
	uint32_t pmd_id;   // Generated based on port mask 
	uint32_t promisc; 
	uint64_t mac_addr; // Read from HW 

	struct rte_eth_conf conf;
};
//
static const struct app_link_params link_params_default = {
	.pmd_id = 0,
    .promisc = 1,
	.mac_addr = 0,

	.conf = {
		.link_speeds = 0,
		.rxmode = {
			.mq_mode = ETH_MQ_RX_NONE,

			.header_split   = 0, // Header split 
			.hw_ip_checksum = 0, // IP checksum offload 
			.hw_vlan_filter = 0, // VLAN filtering 
			.hw_vlan_strip  = 0, // VLAN strip 
			.hw_vlan_extend = 0, // Extended VLAN 
			.jumbo_frame    = 0, // Jumbo frame support 
			.hw_strip_crc   = 0, // CRC strip by HW 
			.enable_scatter = 0, // Scattered packets RX handler 

			.max_rx_pkt_len = 9000, // Jumbo frame max packet len 
			.split_hdr_size = 0, // Header split buffer size 
		},
		.rx_adv_conf = {
			.rss_conf = {
				.rss_key = NULL,
				.rss_hf = 0,
			},
		},
		.txmode = {
			.mq_mode = ETH_MQ_TX_NONE,
		},
		.lpbk_mode = 0,
	},
};
//==========================================================================================================
struct app_pktq_hwq_in_params {
	uint32_t size;
	uint32_t burst;

	struct rte_eth_rxconf conf;
};

static const struct app_pktq_hwq_in_params default_hwq_in_params = {
	.size = 128,
	.burst = 32, //not used

	.conf = {
		.rx_thresh = {
				.pthresh = 8,
				.hthresh = 8,
				.wthresh = 4,
		},
		.rx_free_thresh = 64,
		.rx_drop_en = 0,
		.rx_deferred_start = 0,
	}
};
//==========================================================================================================
struct app_pktq_hwq_out_params {
	uint32_t size;
	uint32_t burst;

	struct rte_eth_txconf conf;
};
//
static const struct app_pktq_hwq_out_params default_hwq_out_params = {
	.size = DEFAULT_TX_DESC,
	.burst = PKTQ_HWQ_OUT_BURST_SIZE,

	.conf = {
		.tx_thresh = {
			.pthresh = 36,
			.hthresh = 0,
			.wthresh = 0,
		},
		.tx_rs_thresh = 0,
		.tx_free_thresh = 0,
		.txq_flags = ETH_TXQ_FLAGS_NOMULTSEGS | ETH_TXQ_FLAGS_NOOFFLOADS,
		.tx_deferred_start = 0,
	}
};
//==========================================================================================================
uint32_t total_num_lcores()
{
	uint32_t total = 0;
	uint32_t i;
	for (i = 0; i < RTE_MAX_LCORE; i++)
	{
		if ( rte_lcore_is_enabled(i) )
		{
			total += 1;
		}
	}
	return total;
}
//==========================================================================================================
static void app_init_eal(int argc, char **argv)
{
	int status;
	status = rte_eal_init(argc, argv);
	if (status < 0) { rte_panic("EAL init error\n"); }
}
//
static void app_init_mempool()
{
	uint32_t i, pmd_id;
	for (pmd_id = 0; pmd_id < RTE_MAX_ETHPORTS; pmd_id++)
	{
		//check if the port is needed
		if ((PORT_MASK & (1LLU << pmd_id)) == 0) {continue;} 
		
		for (i = 0; i < RTE_MAX_LCORE; i++)
		{
			if ( rte_lcore_is_enabled(i) )
			{
				uint32_t sid = rte_lcore_to_socket_id(i);
				char name[128]; sprintf(name, "MEMPOOL_TXQ%u.%u", pmd_id, i);
				mempool[pmd_id][i] = rte_pktmbuf_pool_create(
											name,
											mempool_params_default.pool_size,
											mempool_params_default.cache_size,
											mempool_params_default.priv_size,
											mempool_params_default.data_size,
											sid);

				if (mempool[pmd_id][i] == NULL) { rte_panic("%s init error\n", name); }
			}
		}
	}
}
//
static inline int app_get_cpu_socket_id(uint32_t pmd_id)
{
	int status = rte_eth_dev_socket_id(pmd_id);

	return (status != SOCKET_ID_ANY) ? status : 0; //On the DPDK box, only 1 socket, so 0
}
//
static void app_init_link()
{
	int status;
	uint32_t pmd_id, pmd_sid, i;
	
	//
	for (pmd_id = 0; pmd_id < RTE_MAX_ETHPORTS; pmd_id++)
	{
		//check if the port is needed
		if ((PORT_MASK & (1LLU << pmd_id)) == 0) {continue;} 
		
		//get pmd_sid
		pmd_sid = app_get_cpu_socket_id(pmd_id);
		
		// LINK
		struct app_link_params link_temp; //create a copy from the initialized static one
		memcpy(&link_temp, &link_params_default, sizeof(struct app_link_params));
		
		status = rte_eth_dev_configure(pmd_id, 1, total_num_lcores(), &link_temp.conf);
		if (status < 0) { printf("Error, can not init dev %u\n", pmd_id); exit(1); }

		printf("Getting the MAC address of the selected port...\n");
		rte_eth_macaddr_get(pmd_id, (struct ether_addr *) &link_temp.mac_addr);
		unsigned char* ptr = (unsigned char*)&link_temp.mac_addr;
		int i;
		for(i=0; i<6; i++)
		{
			printf("%.2x:", ptr[i]);
		}
		printf("\n");
		if (link_temp.promisc) { rte_eth_promiscuous_enable(pmd_id); }
			
		//RXQ
		printf("========Setting up the RXQ%d.0==========\n", pmd_id);
		// MEMPOOL FOR RXQ, only 1
		char name[128]; sprintf(name, "MEMPOOL_RXQ%u.0", pmd_id);
		struct rte_mempool * mp;
		mp = rte_pktmbuf_pool_create(
									name,
									mempool_params_default.pool_size,
									mempool_params_default.cache_size,
									mempool_params_default.priv_size,
									mempool_params_default.data_size,
									pmd_sid);
		
		if(mp == NULL) { printf("Error, can not create rxq mempool for dev %u \n", pmd_id); exit(1); }
			
		status = rte_eth_rx_queue_setup(
			                pmd_id,//port id
			                0,//queue id
			                default_hwq_in_params.size,//the number of rx descriptor
			                pmd_sid,//socket id
			                &default_hwq_in_params.conf,//config
			                mp);//mempool	
		if (status < 0) { printf("Error, can not set up rx queue for dev %u \n", pmd_id);  exit(1); }	

		//TXQ 
		uint32_t count = 0;
		for(i = 0; i < RTE_MAX_LCORE; i++) 
		{
			if(rte_lcore_is_enabled(i))
			{			
				printf("========Setting up the TXQ%d.%d==========\n", pmd_id, i);
				//set up the lcoreid_to_queueid
				lcoreid_to_queueid[pmd_id][i] = count;
				count += 1;
				status = rte_eth_tx_queue_setup(
										pmd_id,
										lcoreid_to_queueid[pmd_id][i], //the relative queue id
										default_hwq_out_params.size,//the number of tx descriptor
										pmd_sid,
										&default_hwq_out_params.conf);
				if (status < 0) { printf("Error, can not set up tx queue for dev %u \n", pmd_id);  exit(1); }
			}
		}

		// LINK START
		status = rte_eth_dev_start(pmd_id);
		if (status < 0) { printf("Error, can not start dev %u \n", pmd_id);  exit(1); }

		// LINK UP
		status = rte_eth_dev_set_link_up(pmd_id);
		if (status < 0) { printf("Error, can not set link up for dev %u \n", pmd_id);  exit(1); }
	}
}
//
int app_init(int argc, char **argv)
{
	app_init_eal(argc, argv);
	app_init_mempool();
	app_init_link();

	return 0;
}
//==========================================================================================================
//==========================================================================================================
//
int myrand(int lcore_id)
{
	return (rand() * (lcore_id+1) * (lcore_id+1));
}
//
void random_ip_gen(char* src_addr_temp, int lcore_id)
{
	int i;
	int offset = 0;
	for(i=0; i<4; i++)
	{
		offset += sprintf(src_addr_temp+offset, "%d", myrand(lcore_id) & 255);
		if(i != 3) { offset += sprintf(src_addr_temp+offset, "."); }
	}
	src_addr_temp[offset] = '\0';
}
//
struct Pseudo_IP_Header
{
	uint32_t src_addr; //ip
	uint32_t dst_addr; //ip
	unsigned short protocol; //ip
	unsigned short udp_len; //udp
};
struct ether_header {
	unsigned char ether_dhost[6];     
	unsigned char ether_shost[6];     
	unsigned short ether_type;         
};
//
unsigned short Compute_Checksum_UDP(unsigned char* addr, int count, struct Pseudo_IP_Header PIH)
{
	//Compute Internet Checksum for "count" bytes beginning at location "addr".  
	register long sum = 0;
	while( count > 1 )  {sum += * (unsigned short*) addr; addr += 2; count -= 2;}

	//Add left-over byte, if any
	if( count > 0 ) { sum += * (unsigned char *) addr;}
	
	
	//Handle the Pseudo_IP_Header
	addr = (unsigned char*) &PIH;
	count = sizeof(struct Pseudo_IP_Header); // The number of unsigned chars, careful
	while( count !=0 )  {sum += * (unsigned short*) addr; addr += 2; count -= 2;}
	
    // Fold 32-bit sum to 16 bits
	while (sum>>16) { sum = (sum & 0xffff) + (sum >> 16); }
		
	unsigned short checksum = ~sum;
	return checksum;
}
//
unsigned short csum(unsigned short *buf, int nwords)
{
    unsigned long sum;
    for(sum=0; nwords>0; nwords--)
        sum += *buf++;
    sum = (sum >> 16) + (sum &0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}
//
int packet_fillin_random(uint8_t* sendbuf, int lcore_id)
{
	int i;
	//prepare the buffer and the pointer of each header
	int tx_len = 0;
	//memset(sendbuf, 0, MAX_PKT_SIZE);
	struct ether_header * eh = (struct ether_header *) sendbuf;
	struct iphdr * iph = (struct iphdr *) (sendbuf + sizeof(struct ether_header));
	struct udphdr *udph = (struct udphdr *) (sendbuf + sizeof(struct iphdr) + sizeof(struct ether_header));
	//fill the mac with random
	for(i=0; i<ETH_ALEN; i++){ eh->ether_shost[i] = myrand(lcore_id) & 255; }
	for(i=0; i<ETH_ALEN; i++){ eh->ether_dhost[i] = myrand(lcore_id) & 255; }
	eh->ether_type = htons(ETH_P_IP);
	
	tx_len += sizeof(struct ether_header);
	//fill the ip with random
	iph->ihl = 5; //with no IP options, ihl*4 is the IP header length
	iph->version = 4;
	iph->tos = 16+8+4+2; // minimize delay, maximize throughput, maximize reliability and minimize monetary cost
	iph->ttl = 64; // hops
	iph->protocol = 17; // UDP
	
	char src_addr_temp[16];
	random_ip_gen(src_addr_temp, lcore_id);
	const char* src_addr = src_addr_temp;
	iph->saddr = inet_addr(src_addr);
	
	char dst_addr_temp[16];
	random_ip_gen(dst_addr_temp, lcore_id);
	const char* dst_addr = dst_addr_temp;
	iph->daddr = inet_addr(dst_addr);
	
	tx_len += sizeof(struct iphdr); //with no IP header options
	//fill the udp with random
	udph->source = htons(myrand(lcore_id) & 0xffff);
	udph->dest = htons(myrand(lcore_id) & 0xffff);
	
	tx_len += sizeof(struct udphdr);
	//fill the payload with charater A
	uint8_t * payload = (uint8_t *)sendbuf + tx_len;
	
	//int payload_size = (myrand(lcore_id) & 1023) + 64;
	//for(i=0; i<payload_size; i++) {payload[i] = 'A';}
	
	int payload_size = PAYLOAD_SIZE;
	//memset(payload, 0, payload_size);
	
	tx_len += payload_size;
	//update the UDP header
	udph->len = htons(tx_len - sizeof(struct ether_header) - sizeof(struct iphdr));
	
	struct Pseudo_IP_Header PIH; memset((unsigned char*)&PIH, 0, sizeof(struct Pseudo_IP_Header));
	PIH.src_addr = iph->saddr;
	PIH.dst_addr = iph->daddr;
	PIH.protocol = htons((unsigned short)iph->protocol);
	PIH.udp_len = udph->len;
	udph->check = Compute_Checksum_UDP((unsigned char*)udph, ntohs(udph->len), PIH);
	
	//update the IP header
	iph->tot_len = htons(tx_len - sizeof(struct ether_header));
	
	iph->check = csum((unsigned short *)(sendbuf+sizeof(struct ether_header)), sizeof(struct iphdr)/2);
	
	//return the pktsize
	return tx_len;
}

//Fill in each mempool with real data for the corresponding core with lcore_id
int pktgen_setup_packets(int lcore_id)
{
	int total_size = 0;
	uint32_t pmd_id;
	for (pmd_id = 0; pmd_id < RTE_MAX_ETHPORTS; pmd_id++)
	{
		//check if the port is needed
		if ((PORT_MASK & (1LLU << pmd_id)) == 0) {continue;} 
		
		struct rte_mempool* mp = mempool[pmd_id][lcore_id];
		struct rte_mbuf *m = NULL;
		struct rte_mbuf *mm = NULL;
		//fill in the complete entire mempool, so the "loop number" is MAX_MBUFS_PER_PORT
		for(; ;)
		{
			m = rte_pktmbuf_alloc(mp);
			if (unlikely(m == NULL) ) {break;}
			
			//chain the rte_mbuf
			m->next = mm;
			mm = m;
			//fill in the buffer with the packet
			uint8_t * buffer = (uint8_t *)(m->buf_addr + m->data_off);
			int pktSize = packet_fillin_random(buffer, lcore_id);
			total_size += pktSize;
			
			//update the packet size
			m->pkt_len  = pktSize;
			m->data_len = pktSize;
		}
		if (mm != NULL) {rte_pktmbuf_free(mm);}	
	}
	//total_size is the sum of the sizes of all the packets in the mempool corresponding to each thread, for all the NICs 
	return total_size;
}
//==========================================================================================================
//==========================================================================================================
//
static inline void __pktmbuf_alloc_noreset(struct rte_mbuf *m)
{
	m->next = NULL;
	m->nb_segs = 1;
	m->port = 0xff;

	m->data_off = (RTE_PKTMBUF_HEADROOM <= m->buf_len) ? RTE_PKTMBUF_HEADROOM : m->buf_len;
	rte_mbuf_refcnt_set(m, 1);
}
//
static inline int wr_pktmbuf_alloc_bulk_noreset(struct rte_mempool *mp, struct rte_mbuf *m_list[], unsigned int cnt)
{
	int ret;
	unsigned int i;

	ret = rte_mempool_get_bulk(mp, (void * *)m_list, cnt);
	if (ret == 0) {
		for (i = 0; i < cnt; i++)
			__pktmbuf_alloc_noreset(*m_list++);
		ret = cnt;
	}
	else
	{
		printf("rte_mempool_get_bulk return error!!!\n");
		exit(1);
	}
	return ret;
}
//==========================================================================================================
//==========================================================================================================
//
int pktgen_get_pkts_modify(int lcore_id)
{
	uint32_t pmd_id;
	for (pmd_id = 0; pmd_id < RTE_MAX_ETHPORTS; pmd_id++)
	{
		//check if the port is needed
		if ((PORT_MASK & (1LLU << pmd_id)) == 0) {continue;}
		
		struct rte_mempool* mp = mempool[pmd_id][lcore_id];
		wr_pktmbuf_alloc_bulk_noreset(mp, app_mtable[pmd_id][lcore_id], PKTQ_HWQ_OUT_BURST_SIZE);
	}
}
//
void pktgen_send_pkts_modify(int lcore_id)
{
	uint32_t pmd_id;
	for (pmd_id = 0; pmd_id < RTE_MAX_ETHPORTS; pmd_id++)
	{
		//check if the port is needed
		if ((PORT_MASK & (1LLU << pmd_id)) == 0) {continue;}
		int pos = 0;
		int cnt = PKTQ_HWQ_OUT_BURST_SIZE;
		while (cnt) 
		{
			int ret = rte_eth_tx_burst(pmd_id, lcoreid_to_queueid[pmd_id][lcore_id], &(app_mtable[pmd_id][lcore_id][pos]), cnt);
			pos += ret;
			cnt -= ret;
		}
	}
}
//The app_thread_throughput() separate the wr_pktmbuf_alloc_bulk_noreset() from rte_eth_tx_burst() in each round
int app_thread_throughput(void *arg)
{
	unsigned lcore_id;
	lcore_id = rte_lcore_id();
	printf("Hello from core %u!!!\n", lcore_id);
    	
	//set up packets
	pktgen_setup_packets(lcore_id);
	for(; ;)
	{
		int total_time_in_sec = 10;
		uint64_t p_ticks = total_time_in_sec * rte_get_tsc_hz();
		
		
		//call wr_pktmbuf_alloc_bulk_noreset()	
		pktgen_get_pkts_modify(lcore_id);
			
		int rounds = 0;
		uint64_t p_start = rte_get_tsc_cycles();
		while(rte_get_tsc_cycles() - p_start < p_ticks)
		{
			//call rte_eth_tx_burst()
			pktgen_send_pkts_modify(lcore_id);
			rounds += 1;
		}
			
		printf("lcore %d: Throughput is %lf GBPS \n", lcore_id, 
					(double)8 * rounds * PKTQ_HWQ_OUT_BURST_SIZE * (PAYLOAD_SIZE+42) / total_time_in_sec / 1000 /1000 /1000 );
	}
	return 0;	
}

//==========================================================================================================
//==========================================================================================================
int main(int argc, char **argv)
{
	srand(time(NULL));
	app_init(argc, argv);
	rte_eal_mp_remote_launch(app_thread_throughput, NULL, CALL_MASTER);
}
