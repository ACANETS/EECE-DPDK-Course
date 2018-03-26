/* main.c
 * 
 * sample RX program for DPDK Lab
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

#include <rte_common.h>
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
#include <signal.h>
#include <unistd.h>


#define APP_PARAM_NAME_SIZE                      256 //the string size for storing the name
#define RTE_MAX_ETHPORTS                         32  //the maximum possible ports

#ifdef RTE_PORT_IN_BURST_SIZE_MAX 
#undef RTE_PORT_IN_BURST_SIZE_MAX
#define RTE_PORT_IN_BURST_SIZE_MAX               64 //the busrt size of the RX queue
#endif

#ifdef RTE_MAX_LCORE
#undef RTE_MAX_LCORE
#define RTE_MAX_LCORE                            12  //the maximum possible cores
#endif
//==========================================================================================================
#define PORT_MASK                                0x01 //to choose the port you need, only 1 port in this example
//==========================================================================================================
struct rte_ring* app_ring;
//==========================================================================================================
struct app_mempool_params {
	uint32_t buffer_size;
	uint32_t pool_size;
	uint32_t cache_size;
};
//these settings are used for memory pool allocation, they are pretty standard. Please don't modify them unless you know what you are doing
static const struct app_mempool_params mempool_params_default = { 
	.buffer_size = 2048 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM,
	.pool_size = 16 * 1024,
	.cache_size = 256,
};
//==========================================================================================================
struct app_link_params { 
	uint32_t promisc;
	uint64_t mac_addr; 
	struct rte_eth_conf conf;
};
//these settings are used for setting up the port, they are pretty standard. Please don't modify them unless you know what you are doing
//if you want to enable RSS, please change it accordingly
static const struct app_link_params link_params_default = {
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
				.rss_key_len = 40,
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
	uint32_t size; //the number of ring descriptor of each RX queue
	struct rte_eth_rxconf conf;
};
//these settings are used for setting up the RX queue, they are pretty standard. Please don't modify them unless you know what you are doing
static const struct app_pktq_hwq_in_params default_hwq_in_params = {
	.size = 512, 
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
	struct rte_eth_txconf conf;
};
//these settings are used for setting up the TX queue, they are pretty standard. Please don't modify them unless you know what you are doing
static const struct app_pktq_hwq_out_params default_hwq_out_params = {
	.size = 1024, //the number of ring descriptor of each TX queue

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
//Start the EAL, which is must-do
static void app_init_eal(int argc, char **argv)
{
	int status;
	status = rte_eal_init(argc, argv);
	if (status < 0) { rte_panic("EAL init error\n"); }
}
//get the socket ID for the port ID
static inline int app_get_cpu_socket_id(uint32_t pmd_id)
{
	int status = rte_eth_dev_socket_id(pmd_id);

	return (status != SOCKET_ID_ANY) ? status : 0;
}
//basically, 3 important APIs 
//1. rte_eth_dev_configure()
//2. rte_eth_rx_queue_setup()
//3. rte_eth_dev_start()
static void app_init_link()
{
	int status;
	uint32_t pmd_id;
	for (pmd_id = 0; pmd_id < RTE_MAX_ETHPORTS; pmd_id++)    //iterate through all the possible ports
	{
		if ((PORT_MASK & (1LLU << pmd_id)) == 0) {continue;} //check if the port is needed
		
		// SOCKET ID, get the socket ID for each port in the NUMA, for the best allocation of the memory pool 
		int socket_id = app_get_cpu_socket_id(pmd_id);
		
		// MEMPOOL, create a memory pool for each port
		char name[128]; sprintf(name, "MEMPOOL%u", pmd_id);
		struct rte_mempool * mp;
		mp = rte_mempool_create(
								name,//mempool name
								mempool_params_default.pool_size,
								mempool_params_default.buffer_size,
								mempool_params_default.cache_size,
								sizeof(struct rte_pktmbuf_pool_private),
								rte_pktmbuf_pool_init, NULL,
								rte_pktmbuf_init, NULL,
								socket_id,
								0 //for safe, always 0, unless you know what you are doing.
							);
		
		if(mp == NULL) { printf("Error, can not create mempool for dev %u \n", pmd_id); exit(1); }
		
		// Port, each port has 1 RX queue and 0 TX queue
		struct app_link_params link_temp; //create a copy from the initialized static one
		memcpy(&link_temp, &link_params_default, sizeof(struct app_link_params));
		//1 RX queue and 1 TX queue
		status = rte_eth_dev_configure(pmd_id, 1, 1, &link_temp.conf);
		if (status < 0) { printf("Error, can not init dev %u\n", pmd_id); exit(1); }
		
		//one way to get the corresponding mac address for each port
		rte_eth_macaddr_get(pmd_id, (struct ether_addr *) &link_temp.mac_addr);
		
		//enable promiscuous mode, which means this port will receive all the packets
		if (link_temp.promisc) { rte_eth_promiscuous_enable(pmd_id); }

		// RXQ 
		printf("########## Setting UP RXQ For PORT %u ########## \n", pmd_id);
		status = rte_eth_rx_queue_setup(
										pmd_id,
										0,//queue id, since in this example we don't use RSS, this implies only one RX queue
										default_hwq_in_params.size,
										socket_id,
										&default_hwq_in_params.conf,
										mp //memory pool address 
									);
		if (status < 0) { printf("Error, can not set up queue for dev %u \n", pmd_id);  exit(1); }

		//TXQ
		printf("########## Setting UP TXQ For PORT %u ########## \n", pmd_id);
		status = rte_eth_tx_queue_setup(
										pmd_id,
										0, //the relative queue id
										default_hwq_out_params.size,//the number of tx descriptor
										socket_id,
										&default_hwq_out_params.conf);
		if (status < 0) { printf("Error, can not set up tx queue for dev %u \n", pmd_id);  exit(1); }

		// LINK START
		status = rte_eth_dev_start(pmd_id);
		if (status < 0) { printf("Error, can not start dev %u \n", pmd_id);  exit(1); }

	}
}

//
int app_init(int argc, char **argv)
{
	app_init_eal(argc, argv);
	app_init_link();
	
	return 0;
}
//==========================================================================================================
int app_thread(void *arg)
{
	uint32_t lcore_id = rte_lcore_id();
	uint32_t master_core_id = rte_get_master_lcore();
	uint32_t i; 
	int status;
	struct rte_mbuf *pkts[RTE_PORT_IN_BURST_SIZE_MAX]; //the pointer array that will store the pointer to each received packet
	uint32_t n_pkts; //the number of received packets during one burst
	//
	if(lcore_id == master_core_id)
	{
		printf("Hello from master core %u !\n", lcore_id);
		 
		while(1)
		{
			uint32_t total_time_in_sec = 10; //for report, in second
			uint64_t p_ticks = total_time_in_sec * rte_get_tsc_hz(); //for report, calculate the total CPU cycles 
			uint64_t p_start = rte_get_tsc_cycles(); //get the current CPU cycle
			uint32_t total_pkts = 0; //for statistics
			while(rte_get_tsc_cycles() - p_start < p_ticks)
			{
				//only 1 port, only 1 queue
				n_pkts = rte_eth_rx_burst(0, 0, pkts, RTE_PORT_IN_BURST_SIZE_MAX); //trying to receive packts 
				if(unlikely(n_pkts == 0)) {continue;} //if no packet received, then start the next try
				total_pkts += n_pkts;
				
				//retrieving the data from each packet
				for(i=0; i<n_pkts; i++)
				{
					rte_ring_sp_enqueue(app_ring, (void *)(pkts[i]));
				}
			}
			printf("lcore %u, received %u packets in %u seconds.\n", lcore_id, total_pkts, total_time_in_sec);
		}			
			
	}
	else
	{
		printf("Hello from the slave core %u !\n", lcore_id);
		struct rte_mbuf* m;
		int status;
		while(1)
		{
			status = rte_ring_sc_dequeue(app_ring, (void **)(&m)); //deque one packet
			if(status == 0) {
				rte_eth_tx_burst(0, 0, (struct rte_mbuf**)(&m), 1); //send the dequeued packet out 
			}
		}
	}
	//
	return 0;	
}
//==========================================================================================================
int main(int argc, char **argv)
{
	app_init(argc, argv);
	app_ring = rte_ring_create("rx_to_tx", 128*1024, 0, RING_F_SP_ENQ | RING_F_SC_DEQ);
	rte_eal_mp_remote_launch(app_thread, NULL, CALL_MASTER);
}


