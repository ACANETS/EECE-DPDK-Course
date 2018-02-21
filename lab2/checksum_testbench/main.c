#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_string_fns.h>
#include <rte_acl.h>

//=================================================================================================================
#include "hashtable.h"

//=================================================================================================================
#define MEMPOOL_CACHE_SIZE 256
#define MAX_PKT_BURST 32
#define NB_SOCKETS 8
#define PREFETCH_OFFSET	3 // Configure how many packets ahead to prefetch, when reading packets
#define MAX_RX_QUEUE_PER_LCORE 16
#define MAX_RX_QUEUE_PER_PORT 128
//=================================================================================================================
// Configurable number of RX/TX ring descriptors
#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 512
static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;
//=================================================================================================================
// ethernet addresses of ports
static struct ether_addr ports_eth_addr[RTE_MAX_ETHPORTS];
//=================================================================================================================
// mask of enabled ports
static uint32_t enabled_port_mask;
static int promiscuous_on = 1; // Ports set in promiscuous mode on by default.
static int numa_on = 0; // NUMA is disabled by default. Change this if the host is in NUMA.
//=================================================================================================================
// This expression is used to calculate the number of mbufs needed
// depending on user input, taking into account memory for rx and tx hardware
// rings, cache per lcore and mtable per port per lcore.
// RTE_MAX is used to ensure that NB_MBUF never goes below a
// minimum value of 16 * 1024
#define NB_MBUF	RTE_MAX(\
	(nb_ports * nb_rx_queue * RTE_TEST_RX_DESC_DEFAULT +	\
	nb_ports * nb_lcores * MAX_PKT_BURST +			\
	nb_ports * n_tx_queue * RTE_TEST_TX_DESC_DEFAULT +	\
	nb_lcores * MEMPOOL_CACHE_SIZE),			\
	(unsigned)(16 * 1024))

static struct rte_mempool *pktmbuf_pool[NB_SOCKETS];

//=================================================================================================================
static struct rte_eth_conf port_conf = {
	.rxmode = {
		.mq_mode	= ETH_MQ_RX_RSS,
		.max_rx_pkt_len = 9000,
		.split_hdr_size = 0,
		.header_split   = 0, //Header Split disabled
		.hw_ip_checksum = 0, //IP checksum offload enabled
		.hw_vlan_filter = 0, //VLAN filtering disabled
		.jumbo_frame    = 0, //Jumbo Frame Support disabled
		.hw_strip_crc   = 0, //CRC stripped by hardware
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			.rss_key_len = 40,
			.rss_hf = ETH_RSS_IPV4,
			//.rss_hf = ETH_RSS_IPV4 | ETH_RSS_IPV6 | ETH_RSS_L2_PAYLOAD,
			//.rss_hf =ETH_RSS_IP | ETH_RSS_UDP | ETH_RSS_TCP | ETH_RSS_SCTP,
		},
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

//=================================================================================================================
//=================================================================================================================
#define MAX_LCORE_PARAMS 1024
struct lcore_params 
{
	uint8_t port_id;
	uint8_t queue_id;
	uint8_t lcore_id;
} __rte_cache_aligned;

static struct lcore_params lcore_params_array[MAX_LCORE_PARAMS];
static struct lcore_params lcore_params_array_default[] = {
	{0, 0, 1},
	{0, 1, 3},
	{0, 2, 5},
	{0, 3, 7},
	{0, 4, 9},
	{0, 5, 11},
};

static struct lcore_params *lcore_params = lcore_params_array_default;
static uint16_t nb_lcore_params = sizeof(lcore_params_array_default) / sizeof(lcore_params_array_default[0]);
//=================================================================================================================
struct lcore_rx_queue 
{
	uint8_t port_id;
	uint8_t queue_id;
} __rte_cache_aligned;


struct lcore_conf {
	uint16_t n_rx_queue;
	struct lcore_rx_queue rx_queue_list[MAX_RX_QUEUE_PER_LCORE];
} __rte_cache_aligned;

static struct lcore_conf lcore_conf[RTE_MAX_LCORE];
//=================================================================================================================
//added for Ctrl+C to terminate the program and then report some statistics
volatile int keepRunning = 1;
void intHandler()
{
	keepRunning = 0;
}
//=================================================================================================================
// main processing loop
static int main_loop(void *args)
{
	struct app_packed_data* app_data = (struct app_packed_data*) args;
	uint8_t master_core_id = rte_get_master_lcore();
	uint8_t lcore_id = rte_lcore_id();
	int i, j, nb_rx; 
	uint8_t portid, queueid;
	struct lcore_conf *qconf;
	int socketid;

	qconf = &lcore_conf[lcore_id];
	socketid = rte_lcore_to_socket_id(lcore_id);
	
	if(lcore_id == master_core_id) //master core
	{
		sysuptime_stamp_master_lcore = rte_get_tsc_cycles();
		if (qconf->n_rx_queue > 0) 
		{
			printf("Error, Master core %u is not supposed to handle the RX queues. Please change the config parameters.\n", lcore_id);
			exit(1);
		}
		printf("Starting packet trace collection on master core %u ...\n", lcore_id);
		uint64_t master_core_total_cycle = rte_get_tsc_hz() * app_data->duration;
		while (likely(keepRunning)) {
			sleep(NETFLOW_GENERATION_INTERVAL);
			AggregateHashTable(app_data);
			if(rte_get_tsc_cycles() - sysuptime_stamp_master_lcore > master_core_total_cycle)
			{
				keepRunning = 0;
			}
		}
	}
	else
	{
		InitHashTable(&(ares_ht[lcore_id]));
		struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
		
		if (qconf->n_rx_queue == 0) 
		{
			printf("lcore %u has nothing to do\n", lcore_id);
			return 0;
		}
		printf("Updating hashtable on lcore %u\n", lcore_id);
		for (i = 0; i < qconf->n_rx_queue; i++) 
		{
			portid = qconf->rx_queue_list[i].port_id;
			queueid = qconf->rx_queue_list[i].queue_id;
			printf("\t lcoreid=%u portid=%hhu rxqueueid=%hhu\n", lcore_id, portid, queueid);
		}
	
		while (likely(keepRunning)) 
		{
			//Read packet from RX queues
			for (i = 0; i < qconf->n_rx_queue; i++) //iterate through all the rx queues assigned to this core
			{
				portid = qconf->rx_queue_list[i].port_id;
				queueid = qconf->rx_queue_list[i].queue_id;
				nb_rx = rte_eth_rx_burst(portid, queueid, pkts_burst, MAX_PKT_BURST);
				if(unlikely(nb_rx == 0)) {continue;}
				//printf("lcoreid=%u, portid=%hhu, rxqueueid=%hhu, received %d packets\n", lcore_id, portid, queueid, nb_rx);
				UpdateHashTable(pkts_burst, nb_rx, &(ares_ht[lcore_id]));
				for(j=0; j < nb_rx; j++) {rte_pktmbuf_free(pkts_burst[j]);}
			}
		}//while loop
	}//slave core
}//main loop
//=================================================================================================================
static int check_lcore_params(void)
{
	uint8_t queue, lcore;
	uint16_t i;
	int socketid;

	for (i = 0; i < nb_lcore_params; i++) 
	{
		queue = lcore_params[i].queue_id;
		if (queue >= MAX_RX_QUEUE_PER_PORT) 
		{
			printf("invalid queue number: %hhu\n", queue);
			return -1;
		}
		lcore = lcore_params[i].lcore_id;
		if (!rte_lcore_is_enabled(lcore)) 
		{
			printf("error: lcore %hhu is not enabled in lcore mask\n", lcore);
			return -1;
		}
		socketid = rte_lcore_to_socket_id(lcore);
		if (socketid != 0 && numa_on == 0) 
		{
			printf("warning: lcore %hhu is on socket %d with numa off\n", lcore, socketid);
		}
	}
	return 0;
}
//=================================================================================================================
static int check_port_config(const unsigned nb_ports)
{
	unsigned portid;
	uint16_t i;

	for (i = 0; i < nb_lcore_params; i++) 
	{
		portid = lcore_params[i].port_id;
		if ((enabled_port_mask & (1 << portid)) == 0) 
		{
			printf("port %u is not enabled in port mask\n", portid);
			return -1;
		}
		if (portid >= nb_ports) 
		{
			printf("port %u is not present on the board\n", portid);
			return -1;
		}
	}
	return 0;
}
//=================================================================================================================
static uint8_t get_port_n_rx_queues(const uint8_t port)
{
	int queue = -1;
	uint16_t i;

	for (i = 0; i < nb_lcore_params; i++) 
	{
		if (lcore_params[i].port_id == port && lcore_params[i].queue_id > queue)
			queue = lcore_params[i].queue_id;
	}
	return (uint8_t)(++queue);
}
//=================================================================================================================
static int init_lcore_rx_queues(void)
{
	uint16_t i, nb_rx_queue;
	uint8_t lcore;

	for (i = 0; i < nb_lcore_params; i++) 
	{
		lcore = lcore_params[i].lcore_id;
		nb_rx_queue = lcore_conf[lcore].n_rx_queue;
		if (nb_rx_queue >= MAX_RX_QUEUE_PER_LCORE) 
		{
			printf("error: too many queues (%u) for lcore: %u\n", (unsigned)nb_rx_queue + 1, (unsigned)lcore);
			return -1;
		} 
		else 
		{
			lcore_conf[lcore].rx_queue_list[nb_rx_queue].port_id = lcore_params[i].port_id;
			lcore_conf[lcore].rx_queue_list[nb_rx_queue].queue_id = lcore_params[i].queue_id;
			lcore_conf[lcore].n_rx_queue++;
		}
	}
	return 0;
}
//=================================================================================================================
// display usage
static void print_usage(const char *prgname)
{
	printf("%s [EAL options] -- -p PORTMASK [--config (port,queue,lcore)[,(port,queue,lcore)]]\n", prgname);
}
//=================================================================================================================
static int parse_portmask(const char *portmask)
{
	char *end = NULL;
	unsigned long pm;

	// parse hexadecimal string
	pm = strtoul(portmask, &end, 16);
	if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (pm == 0)
		return -1;

	return pm;
}
//=================================================================================================================
static int parse_config(const char *q_arg)
{
	char s[256];
	const char *p, *p0 = q_arg;
	char *end;
	enum fieldnames {
		FLD_PORT = 0,
		FLD_QUEUE,
		FLD_LCORE,
		_NUM_FLD
	};
	unsigned long int_fld[_NUM_FLD];
	char *str_fld[_NUM_FLD];
	int i;
	unsigned size;

	nb_lcore_params = 0;
	while ((p = strchr(p0, '(')) != NULL) 
	{
		++p;
		if ((p0 = strchr(p, ')')) == NULL)
			return -1;

		size = p0 - p;
		if (size >= sizeof(s))
			return -1;

		snprintf(s, sizeof(s), "%.*s", size, p);
		if (rte_strsplit(s, sizeof(s), str_fld, _NUM_FLD, ',') != _NUM_FLD)
			return -1;
		for (i = 0; i < _NUM_FLD; i++) 
		{
			errno = 0;
			int_fld[i] = strtoul(str_fld[i], &end, 0);
			if (errno != 0 || end == str_fld[i] || int_fld[i] > 255)
				return -1;
		}
		if (nb_lcore_params >= MAX_LCORE_PARAMS) 
		{
			printf("exceeded max number of lcore params: %hu\n", nb_lcore_params);
			return -1;
		}
		lcore_params_array[nb_lcore_params].port_id = (uint8_t)int_fld[FLD_PORT];
		lcore_params_array[nb_lcore_params].queue_id = (uint8_t)int_fld[FLD_QUEUE];
		lcore_params_array[nb_lcore_params].lcore_id = (uint8_t)int_fld[FLD_LCORE];
		++nb_lcore_params;
	}
	lcore_params = lcore_params_array;
	return 0;
}
//=================================================================================================================
// Parse the argument given in the command line of the application 
static int parse_args(int argc, char **argv)
{
	int opt, ret;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];
	static struct option lgopts[] = {
		{"config", 1, 0, 0},
		{NULL, 0, 0, 0}
	};

	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, "p:", lgopts, &option_index)) != EOF)
	{
		switch (opt) 
		{
		// portmask
		case 'p':
			enabled_port_mask = parse_portmask(optarg);
			if (enabled_port_mask == 0) 
			{
				printf("invalid portmask\n");
				print_usage(prgname);
				return -1;
			}
			break;

		// long options
		case 0:
			if (!strncmp(lgopts[option_index].name, "config", sizeof("config"))) 
			{
				ret = parse_config(optarg);
				if (ret) 
				{
					printf("invalid config\n");
					print_usage(prgname);
					return -1;
				}
			}
			break;

		default:
			print_usage(prgname);
			return -1;
		}
	}

	ret = optind-1;
	optind = 0; // reset getopt lib
	return ret;
}
//=================================================================================================================
static void print_ethaddr(const char *name, const struct ether_addr *eth_addr)
{
	char buf[ETHER_ADDR_FMT_SIZE];
	ether_format_addr(buf, ETHER_ADDR_FMT_SIZE, eth_addr);
	printf("%s%s", name, buf);
}
//=================================================================================================================
static int init_mem(unsigned nb_mbuf)
{
	int socketid;
	unsigned lcore_id;
	char s[64];

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		if (rte_lcore_is_enabled(lcore_id) == 0)
			continue;

		if (numa_on)
			socketid = rte_lcore_to_socket_id(lcore_id);
		else
			socketid = 0;
		if (socketid >= NB_SOCKETS) 
		{
			rte_exit(EXIT_FAILURE, "Socket %d of lcore %u is out of range %d\n", socketid, lcore_id, NB_SOCKETS);
		}
		
		if (pktmbuf_pool[socketid] == NULL) //there is only one mempool on each socket
		{
			snprintf(s, sizeof(s), "mbuf_pool_%d", socketid);
			pktmbuf_pool[socketid] = rte_pktmbuf_pool_create(s, nb_mbuf, MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, socketid);
			if (pktmbuf_pool[socketid] == NULL)
				rte_exit(EXIT_FAILURE, "Cannot init mbuf pool on socket %d\n", socketid);
			else
				printf("Allocated mbuf pool on socket %d\n", socketid);
		}
	}
	return 0;
}
//=================================================================================================================
// Check the link status of all ports in up to 9s, and print them finally
static void
check_all_ports_link_status(uint8_t port_num, uint32_t port_mask)
{
#define CHECK_INTERVAL 100 // 100ms
#define MAX_CHECK_TIME 90 // 9s (90 * 100ms) in total
	uint8_t portid, count, all_ports_up, print_flag = 0;
	struct rte_eth_link link;

	printf("\nChecking link status");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) 
	{
		all_ports_up = 1;
		for (portid = 0; portid < port_num; portid++) 
		{
			if ((port_mask & (1 << portid)) == 0)
				continue;
			memset(&link, 0, sizeof(link));
			rte_eth_link_get_nowait(portid, &link);
			// print link status if flag set 
			if (print_flag == 1) 
			{
				if (link.link_status)
					printf("Port %d Link Up - speed %u Mbps - %s\n", (uint8_t)portid, (unsigned)link.link_speed, (link.link_duplex == ETH_LINK_FULL_DUPLEX) ? ("full-duplex") : ("half-duplex\n"));
				else
					printf("Port %d Link Down\n", (uint8_t)portid);
				continue;
			}
			// clear all_ports_up flag if any link down 
			if (link.link_status == ETH_LINK_DOWN) {
				all_ports_up = 0;
				break;
			}
		}
		// after finally printing all link status, get out
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		// set the print_flag if all ports up or timeout 
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) 
		{
			print_flag = 1;
			printf("done\n");
		}
	}
}

int main(int argc, char **argv)
{
	signal(SIGINT, intHandler); // CTRL+C to terminate the program
	
	struct lcore_conf *qconf;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf *txconf;
	int ret, i;
	unsigned nb_ports;
	uint16_t queueid;
	unsigned lcore_id;
	uint32_t n_tx_queue, nb_lcores;
	uint8_t portid, nb_rx_queue, queue, socketid;

	// init EAL
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
	argc -= ret;
	argv += ret;

	// parse application arguments (after the EAL ones)
	ret = parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid parameters\n");
	argc -= ret;
	argv += ret;
	//===============================================================================================================================
	// parse the packet trace related information
	if(argc != 2)//including the leading --
	{
		printf("Error, the packet validation request format is wrong.\n");
		exit(1);
	}
	
	
	int duration = atoi(argv[1]);
	if(duration > NETFLOW_GENERATION_FORCE_TIMEOUT)
	{
		duration = NETFLOW_GENERATION_FORCE_TIMEOUT;
		printf("Warning, the input duration is too big. It has been adjusted to %d.\n", NETFLOW_GENERATION_FORCE_TIMEOUT);
	}
	
	struct app_packed_data* app_data = (struct app_packed_data*)malloc(sizeof(struct app_packed_data));
	if(app_data == NULL)
	{
		printf("Error, cannot allocate memory for app_data.\n");
		exit(1);
	}
	app_data->duration = duration;
	
	//===============================================================================================================================
	if (check_lcore_params() < 0)
		rte_exit(EXIT_FAILURE, "check_lcore_params failed\n");

	ret = init_lcore_rx_queues();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "init_lcore_rx_queues failed\n");

	nb_ports = rte_eth_dev_count();

	if (check_port_config(nb_ports) < 0)
		rte_exit(EXIT_FAILURE, "check_port_config failed\n");

	nb_lcores = rte_lcore_count();

	// initialize all ports
	for (portid = 0; portid < nb_ports; portid++) 
	{
		// skip ports that are not enabled
		if ((enabled_port_mask & (1 << portid)) == 0) {
			printf("\nSkipping disabled port %d\n", portid);
			continue;
		}

		// init port
		printf("Initializing port %d ... ", portid);
		fflush(stdout);

		nb_rx_queue = get_port_n_rx_queues(portid);
		n_tx_queue = nb_rx_queue;
		printf("Creating queues: nb_rxq=%d nb_txq=%u... ", nb_rx_queue, (unsigned)n_tx_queue);
		ret = rte_eth_dev_configure(portid, nb_rx_queue, (uint16_t)n_tx_queue, &port_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%d\n", ret, portid);

		rte_eth_macaddr_get(portid, &ports_eth_addr[portid]);
		print_ethaddr(" Address:", &ports_eth_addr[portid]);
		printf(", ");

		// init memory
		ret = init_mem(NB_MBUF);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "init_mem failed\n");
		printf("\n");
		
		// setup tx queue, required by QLogic
		queueid = 0;
		for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
			if (rte_lcore_is_enabled(lcore_id) == 0 || lcore_id == rte_get_master_lcore())
				continue;

			if (numa_on)
				socketid = (uint8_t)rte_lcore_to_socket_id(lcore_id);
			else
				socketid = 0;

			printf("txq=%u,%d,%d ", lcore_id, queueid, socketid);
			fflush(stdout);

			rte_eth_dev_info_get(portid, &dev_info);
			txconf = &dev_info.default_txconf;
			ret = rte_eth_tx_queue_setup(portid, queueid, nb_txd, socketid, txconf);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup: err=%d, port=%d\n", ret, portid);
			queueid++;
		}
		printf("\n");
	}

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) 
	{
		if (rte_lcore_is_enabled(lcore_id) == 0)
			continue;
		qconf = &lcore_conf[lcore_id];
		printf("\nInitializing rx queues on lcore %u ... ", lcore_id);
		fflush(stdout);
		// init RX queues
		for (queue = 0; queue < qconf->n_rx_queue; queue++) {
			portid = qconf->rx_queue_list[queue].port_id;
			queueid = qconf->rx_queue_list[queue].queue_id;

			if (numa_on)
				socketid = (uint8_t)rte_lcore_to_socket_id(lcore_id);
			else
				socketid = 0;

			printf("rxq = portid %d, queueid %d, socketid %d ", portid, queueid, socketid);
			fflush(stdout);

			ret = rte_eth_rx_queue_setup(portid, queueid, nb_rxd, socketid, NULL, pktmbuf_pool[socketid]);
			if (ret < 0)
				rte_exit(EXIT_FAILURE,"rte_eth_rx_queue_setup: err=%d, port=%d\n", ret, portid);
		}
	}

	printf("\n");

	// start ports
	for (portid = 0; portid < nb_ports; portid++) 
	{
		if ((enabled_port_mask & (1 << portid)) == 0)
			continue;

		// Start device 
		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_start: err=%d, port=%d\n", ret, portid);
		
		//set up rss
		struct rte_eth_dev_info dev_info;
		struct rte_eth_rss_reta_entry64 reta_conf[(ETH_RSS_RETA_SIZE_512 / RTE_RETA_GROUP_SIZE)];
		memset(&dev_info, 0, sizeof(dev_info));
		rte_eth_dev_info_get(portid, &dev_info);
		
		if (dev_info.reta_size == 0) 
		{ printf("Error, can not set up RSS(null RETA size) for link %u\n", portid); exit(1); }
		if (dev_info.reta_size > ETH_RSS_RETA_SIZE_512) 
		{ printf("Error, can not set up RSS (RETA size too big) for link %u\n", portid); exit(1); }
		
		memset(reta_conf, 0, sizeof(reta_conf));
		int i;
		for (i = 0; i < dev_info.reta_size; i++) 
		{			
			uint32_t reta_id = i / RTE_RETA_GROUP_SIZE;
			uint32_t reta_pos = i % RTE_RETA_GROUP_SIZE;
			uint32_t rss_qs_pos = i % (get_port_n_rx_queues(portid));
			reta_conf[reta_id].mask = UINT64_MAX;
			reta_conf[reta_id].reta[reta_pos] = (uint16_t) rss_qs_pos;
		}

		// RETA update 
		int status = rte_eth_dev_rss_reta_update(portid, reta_conf, dev_info.reta_size);
		if (status != 0){printf("Error, can not set up RSS (RETA update failed) for link %u\n", portid); exit(1);}
		
		// Enable the promiscuous mode
		rte_eth_promiscuous_enable(portid);
	}

	check_all_ports_link_status((uint8_t)nb_ports, enabled_port_mask);

	// launch per-lcore init on every lcore 
	rte_eal_mp_remote_launch(main_loop, (void*)app_data, CALL_MASTER);
	
	//stop and close the ports
	sleep(1);
	printf("stop and close the port ... \n");
	for (portid = 0; portid < nb_ports; portid++) 
	{
		// skip ports that are not enabled
		if ((enabled_port_mask & (1 << portid)) == 0) {
			printf("\nSkipping disabled port %d\n", portid);
			continue;
		}
		rte_eth_dev_stop(portid);
		rte_eth_dev_close(portid);
		printf("Done with port %d ...\n", portid);
	}

	return 0;
}
