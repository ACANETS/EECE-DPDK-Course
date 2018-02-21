#ifndef _HASHTABLE_H_
#define _HASHTABLE_H_

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <rte_mbuf.h>
#include <rte_atomic.h>
#include <assert.h>
#include "netflow_config.h"
struct app_packed_data
{
	int duration;
};
//================================================================================================================================
//================================================================================================================================
static uint64_t sysuptime_stamp_master_lcore; //Declare the sysuptime for each lcore
//================================================================================================================================
//================================================================================================================================
struct Pseudo_IP_Header
{
	uint32_t src_addr; //ip
	uint32_t dst_addr; //ip
	unsigned short protocol; //ip
	unsigned short len; //udp or tcp
};
struct Pseudo_IP_Header PIH;
//================================================================================================================================
unsigned short Compute_Checksum(unsigned char* addr, int count, struct Pseudo_IP_Header PIH)
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
//=================================================================================================================
unsigned short csum(unsigned short *buf, int nwords)
{
    unsigned long sum;
    for(sum=0; nwords>0; nwords--)
        sum += *buf++;
    sum = (sum >> 16) + (sum &0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}
//================================================================================================================================
//================================================================================================================================
// ethernet headers are always exactly 14 bytes
#define SIZE_ETHERNET 14
// Ethernet addresses are 6 bytes
#define ETHER_ADDR_LEN_SNIFFER	6
// Ethernet header, 14 bytes
struct sniff_ethernet 
{
        u_char  dmac[ETHER_ADDR_LEN_SNIFFER];    // destination host address 
        u_char  smac[ETHER_ADDR_LEN_SNIFFER];    // source host address 
        u_short ether_type;                      // IP? ARP? RARP? etc 
};
//================================================================================================================================
// IP header, 20 bytes (without option) 
struct sniff_ip
{
        u_char  ip_vhl;                 // version << 4 | header length >> 2
        u_char  ip_tos;                 // type of service
        u_short ip_len;                 // total length
        u_short ip_id;                  // identification
        u_short ip_off;                 // fragment offset field
        #define IP_RF 0x8000            // reserved fragment flag
        #define IP_DF 0x4000            // dont fragment flag
        #define IP_MF 0x2000            // more fragments flag
        #define IP_OFFMASK 0x1fff       // mask for fragmenting bits
        u_char  ip_ttl;                 // time to live
        u_char  ip_p;                   // protocol
        u_short ip_sum;                 // checksum
        struct  in_addr ip_src,ip_dst;  // source and dest address
};
#define IP_HL(ip)               (((ip)->ip_vhl) & 0x0f)
#define IP_V(ip)                (((ip)->ip_vhl) >> 4)
//================================================================================================================================
// TCP header, 20 bytes (without option)
typedef u_int tcp_seq;
struct sniff_tcp 
{
        u_short th_sport;               // source port
        u_short th_dport;               // destination port
        tcp_seq th_seq;                 // sequence number
        tcp_seq th_ack;                 // acknowledgement number
        u_char  th_offx2;               // data offset, rsvd 
#define TH_OFF(th)      (((th)->th_offx2 & 0xf0) >> 4)
        u_char  th_flags;
        #define TH_FIN  0x01
        #define TH_SYN  0x02
        #define TH_RST  0x04
        #define TH_PUSH 0x08
        #define TH_ACK  0x10
        #define TH_URG  0x20
        #define TH_ECE  0x40
        #define TH_CWR  0x80
        #define TH_FLAGS        (TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)
        u_short th_win;                 // window
        u_short th_sum;                 // checksum
        u_short th_urp;                 // urgent pointer
};

//================================================================================================================================
// udp headers are always exactly 8 bytes
#define SIZE_UDP 8
// UDP header, 8 bytes
struct sniff_udp
{
	u_short uh_sport;                   // source port
	u_short uh_dport;                   // destination port
	u_short uh_len;                     // length of the header and the payload
	u_short uh_sum;                     // checksum
};

//================================================================================================================================
//================================================================================================================================
#define HASH_TABLE_BLOCK_NUM      256 //the size of growth
#define HASH_TABLE_BUCKET_NUM     (32*1024) //must be power of 2
#define HASH_TABLE_BUCKET_MASK    (HASH_TABLE_BUCKET_NUM - 1)
//
struct FlowKey
{
	struct in_addr src_addr; //network order
	struct in_addr dst_addr; //network order
	u_short src_port; //host order
	u_short dst_port; //host order
	u_char protocol;
	u_char padding[3];
};
//
struct FlowValue
{
	uint32_t th_seq; //host order
	uint32_t th_ack; //host order
	uint64_t stime;
	uint64_t ltime;
	uint32_t total_count;
	uint32_t total_byte;
	uint32_t ploss_count;
	uint32_t ploss_byte;
	u_char  th_flags; //newly added	
	u_short th_win; //host order
	u_char ip_ttl;
	u_char ip_tos; 
	u_char dmac[ETHER_ADDR_LEN_SNIFFER];
	u_char smac[ETHER_ADDR_LEN_SNIFFER];
	uint32_t bad_ip;
	uint32_t bad_udp;
	uint32_t bad_tcp;
	uint64_t bad_count;
};

//
struct HashTableElem
{
	uint32_t hashed_value;
	struct FlowKey fk;
	struct FlowValue fv;	
};
//
struct HashTableBucketInfo
{
	uint32_t cur_size;
	uint32_t max_size;
	volatile uint32_t realloc_count;
	struct HashTableElem* mem_addr;
};
//
struct HashTable
{
	struct HashTableBucketInfo* htbi; //not manually freed before the main() is done
};
static struct HashTable ares_ht[RTE_MAX_LCORE]; //Declare the hashtable pointer for each lcore
//
void InitHashTable(struct HashTable* ht)
{
	ht->htbi = (struct HashTableBucketInfo*) calloc(HASH_TABLE_BUCKET_NUM, sizeof(struct HashTableBucketInfo));
	if(ht->htbi == NULL) {printf("Error, can not allocate enough memory for the HashTable.\n"); exit(1);}
}
//
static inline uint32_t HashFlowFunc(struct FlowKey* fk)
{
	uint32_t hv = 0;
	uint32_t sz_ph = sizeof(struct FlowKey);
	uint32_t sz_int = sizeof(uint32_t);
	uint32_t n_int = sz_ph / sz_int;
	uint32_t n_char = sz_ph - n_int * sz_int; //assume sizeof(unsigned char) == 1

	uint32_t* start = (uint32_t *) fk;
	uint32_t i;
	for (i=0; i<n_int; i++)
	{
		hv ^= (*start);
		start += 1;
	}
	
	unsigned char* end = (unsigned char*) start;
	for(i=0; i<n_char; i++)
	{
		hv += (*end);
		end += 1;
	}
	
	return hv;
}
//================================================================================================================================
//================================================================================================================================
//ONLY FOR ETHER PACKETS
static inline void SniffPacketHeader(struct rte_mbuf* temp, struct FlowKey* fk, struct FlowValue* fv)
{
	// declare pointers to packet headers
	const struct sniff_ethernet *ethernet;  // The ethernet header
	const struct sniff_ip *ip;              // The IP header
	const struct sniff_udp *udp;            // The UDP header
	const struct sniff_tcp *tcp;            // The TCP header
	u_char *payload;                        // Packet payload //not used

	int size_ip;
	int size_tcp;
	int size_payload; //not used
	
	//define/compute ethernet header
	u_char* packet = rte_pktmbuf_mtod(temp, u_char*);
	ethernet = (struct sniff_ethernet*) packet;
	//
	uint32_t VLAN_SIZE_ETHERNET = SIZE_ETHERNET;
	if(ntohs(ethernet->ether_type) == 0x8100)
	{
		VLAN_SIZE_ETHERNET += 4;
		if(ntohs(*((u_short*)(packet+16))) != 0x0800)
		{
			printf("Warning, packet is not the standard ethernet packet. Skipped.\n"); 
			return;
		}
	}
	else
	{
		if(ntohs(ethernet->ether_type) != 0x0800) 
		{
			printf("Warning, packet is not the standard ethernet packet. Skipped.\n"); 
			return;
		}
	}
	
	// define/compute ip header offset
	ip = (struct sniff_ip*)(packet + VLAN_SIZE_ETHERNET);
	size_ip = IP_HL(ip)*4;
	if (IP_V(ip) != 4 || size_ip < 20) {printf("Warning, not IPV4 or the size of ip header is less than 20. Skipped\n"); return;}
	memcpy(&(fk->src_addr), &(ip->ip_src), sizeof(struct in_addr)); //network order
	memcpy(&(fk->dst_addr), &(ip->ip_dst), sizeof(struct in_addr)); //network order
	
	//To check the ip checksum
	unsigned short IP_Checksum = 0x0000;
	if(ntohs(ip->ip_sum) != 0)
	{
		IP_Checksum = csum((unsigned short *)(ip), size_ip/2);
	}
	if( IP_Checksum != 0x0000) 
	{
		fv->bad_ip = 1;
		fv->bad_count = 1;
	}
	PIH.src_addr = *((uint32_t*)(&(ip->ip_src))); 
	PIH.dst_addr = *((uint32_t*)(&(ip->ip_dst)));
	PIH.protocol = htons((unsigned short)ip->ip_p); // very important
	PIH.len = htons(ntohs(ip->ip_len) - size_ip);
		
	// determine protocol	
	fk->protocol = ip->ip_p;
	
	// update some info in the fv
	fv->total_byte = ntohs(ip->ip_len) + VLAN_SIZE_ETHERNET + 4;
	fv->total_count = 1;
	memcpy(fv->dmac, ethernet->dmac, ETHER_ADDR_LEN_SNIFFER);
	memcpy(fv->smac, ethernet->smac, ETHER_ADDR_LEN_SNIFFER);
	fv->ip_ttl = ip->ip_ttl;
	fv->ip_tos = ip->ip_tos;
	fv->stime = rte_get_tsc_cycles();
	fv->ltime = fv->stime;
		
	// We only consider the TCP and UDP packet with their ports
	if(ip->ip_p == IPPROTO_UDP)
	{
		udp = (struct sniff_udp*)(packet + VLAN_SIZE_ETHERNET + size_ip);
		fk->src_port = ntohs(udp->uh_sport); //host order
		fk->dst_port = ntohs(udp->uh_dport); //host order
		
		//check the udp checksum
		unsigned short UDP_Checksum = 0x0000;
		if(ntohs(udp->uh_sum) != 0)
		{
			UDP_Checksum = Compute_Checksum((unsigned char*)udp, ntohs(PIH.len), PIH);
		}
		if (UDP_Checksum != 0x0000)
		{
			fv->bad_udp = 1;
			if( IP_Checksum == 0x0000)
			{
				fv->bad_count = 1;
			}
		}
	}
	else if(ip->ip_p == IPPROTO_TCP)
	{
		tcp = (struct sniff_tcp*)(packet + VLAN_SIZE_ETHERNET + size_ip);
		size_tcp = TH_OFF(tcp)*4;
		if (size_tcp < 20) { printf("Warning, the size of tcp header is less than 20. Skipped. \n"); return; }
		fk->src_port = ntohs(tcp->th_sport); //host order
		fk->dst_port = ntohs(tcp->th_dport); //host order
		fv->th_seq = ntohl(tcp->th_seq); //host order
		fv->th_ack = ntohl(tcp->th_ack); //host order
		fv->th_flags = tcp->th_flags; //newly added
		fv->th_win = ntohs(tcp->th_win); //host order
		
		//check the tcp checksum
		unsigned short TCP_Checksum = 0x0000;
		if(ntohs(tcp->th_sum) != 0)
		{
			TCP_Checksum = Compute_Checksum((unsigned char*)tcp, ntohs(PIH.len), PIH);
		}
		if (TCP_Checksum != 0x0000)
		{
			fv->bad_tcp = 1;
			if( IP_Checksum == 0x0000)
			{
				fv->bad_count = 1;
			}
		}
	}
	
	return;	
}

//
void UpdateHashTable(struct rte_mbuf **pkts, uint32_t n_pkts, struct HashTable* ht)
{
	struct FlowKey fk;
	struct FlowValue fv;
	uint32_t i, j;
	for(i=0; i<n_pkts; i++)
	{
		memset(&fk, 0, sizeof(struct FlowKey));
		memset(&fv, 0, sizeof(struct FlowValue));
		SniffPacketHeader( pkts[i], &fk, &fv );
		
		uint32_t hashed_value = HashFlowFunc( &fk );
		uint32_t bucket = (hashed_value & HASH_TABLE_BUCKET_MASK);
		if(ht->htbi[bucket].cur_size + 1 >= ht->htbi[bucket].max_size)
		{
			ht->htbi[bucket].max_size = ht->htbi[bucket].max_size + HASH_TABLE_BLOCK_NUM;
			ht->htbi[bucket].realloc_count += 1;
			ht->htbi[bucket].mem_addr = realloc(ht->htbi[bucket].mem_addr, ht->htbi[bucket].max_size * sizeof(struct HashTableElem));
			if(ht->htbi[bucket].mem_addr == NULL){ printf("Error, can not realloc the memory! \n"); exit(1); }
			ht->htbi[bucket].realloc_count += 1;
		}
		
		uint32_t find_flag = 0;
		for(j=0; j < ht->htbi[bucket].cur_size; j++)
		{
			if( (hashed_value == ht->htbi[bucket].mem_addr[j].hashed_value) && (memcmp(&fk, &(ht->htbi[bucket].mem_addr[j].fk), sizeof(struct FlowKey)) == 0) )
			{
				find_flag = 1;
				break;
			}
		}
		
		if(find_flag == 0)
		{	
			// j now is ht->htbi[bucket].cur_size
			memcpy(&(ht->htbi[bucket].mem_addr[j].fk), &fk, sizeof(struct FlowKey));
			memcpy(&(ht->htbi[bucket].mem_addr[j].fv), &fv, sizeof(struct FlowValue));
			ht->htbi[bucket].mem_addr[j].hashed_value = hashed_value;
			ht->htbi[bucket].cur_size += 1;
		}
		else
		{
			// j now points to the existed flow, update it
			ht->htbi[bucket].mem_addr[j].fv.total_count += fv.total_count;
			ht->htbi[bucket].mem_addr[j].fv.total_byte += fv.total_byte; 
			ht->htbi[bucket].mem_addr[j].fv.ltime = rte_get_tsc_cycles();
			ht->htbi[bucket].mem_addr[j].fv.bad_ip += fv.bad_ip;
			ht->htbi[bucket].mem_addr[j].fv.bad_udp += fv.bad_udp;
			ht->htbi[bucket].mem_addr[j].fv.bad_tcp += fv.bad_tcp;
			ht->htbi[bucket].mem_addr[j].fv.bad_count += fv.bad_count;
			//seq
			if(fv.th_seq < ht->htbi[bucket].mem_addr[j].fv.th_seq)
			{
				ht->htbi[bucket].mem_addr[j].fv.ploss_count += fv.total_count;
				ht->htbi[bucket].mem_addr[j].fv.ploss_byte += fv.total_byte;
			}
			else
			{
				ht->htbi[bucket].mem_addr[j].fv.th_seq = fv.th_seq;
			}
			/*
			//ack
			if(fv.th_ack < ht->htbi[bucket].mem_addr[j].fv.th_ack)
			{
				ht->htbi[bucket].mem_addr[j].fv.ploss_count += fv.total_count;
				ht->htbi[bucket].mem_addr[j].fv.ploss_byte += fv.total_byte;
			}
			else
			{
				ht->htbi[bucket].mem_addr[j].fv.th_ack = fv.th_ack;
			}
			*/			
		}		
	}// i loop through each packet	
}
//
static inline uint32_t IsFlowBad(struct FlowValue* fv, uint64_t* good_count, uint64_t* bad_count)
{
	assert(fv != NULL);
	*good_count += (fv->total_count) - (fv->bad_count);
	*bad_count += (fv->bad_count);
	return (fv->bad_ip || fv->bad_udp || fv->bad_tcp);
}

//
static inline void PrintFlow(struct app_packed_data* app_data, char* message, uint32_t* offset, struct HashTableElem* hte, uint32_t* print_count)
{
	assert(app_data != NULL);
	assert(message != NULL);
	assert(offset != NULL);
	assert(hte != NULL);
	assert(print_count != NULL);
	
	if((*offset) > (CLIENT_BUFFER_SIZE-1024)) 
	{
		printf("Warning, the size of the returned message has almost reached the limit.\n"); 
		return;
	}
	
	//beginning
	*print_count += 1;
	//start the dict of JSON
	(*offset) += sprintf(message+(*offset), "{ \"Warning\" : { \"Machine_Name\" : \"%s\", \"Sequence_Number\" : \"%u\", ", MEASUREMENT_NODE, *print_count);
	//print the 5-tuple
	(*offset) += sprintf(message+(*offset), "\"src_addr\": \"%s\", ", inet_ntoa(hte->fk.src_addr)); //convert to host order
	(*offset) += sprintf(message+(*offset), "\"dst_addr\": \"%s\", ", inet_ntoa(hte->fk.dst_addr)); //convert to host order
	(*offset) += sprintf(message+(*offset), "\"src_port\": \"%u\", ", hte->fk.src_port); //already in host order
	(*offset) += sprintf(message+(*offset), "\"dst_port\": \"%u\", ", hte->fk.dst_port); //already in host order
	(*offset) += sprintf(message+(*offset), "\"protocol\": \"%u\", ", hte->fk.protocol);
	(*offset) += sprintf(message+(*offset), " \"lastseen\" : \"%u\", ", (uint32_t)(((double)(hte->fv.ltime - sysuptime_stamp_master_lcore))/rte_get_tsc_hz()*1000));
	(*offset) += sprintf(message+(*offset), " \"total packets\" : \"%d\" ", hte->fv.total_count);
	(*offset) += sprintf(message+(*offset), " \"bad ip\" : \"%d\" ", hte->fv.bad_ip);
	(*offset) += sprintf(message+(*offset), " \"bad udp\" : \"%d\" ", hte->fv.bad_udp);
	(*offset) += sprintf(message+(*offset), " \"bad tcp\" : \"%d\" ", hte->fv.bad_tcp);
	//complete the dict of JSON
	(*offset) += sprintf(message+(*offset), "} }, ");
}

//
void AggregateHashTable(struct app_packed_data* app_data)
{
	//
	char* message = (char*) malloc(CLIENT_BUFFER_SIZE * sizeof(char));
	if(message == NULL)
	{
		printf("Error, cannot allocate memory for message buffer.\n");
		exit(1);
	}
	//
	uint32_t i, j, k;
	uint32_t offset = 0;
	uint32_t print_count = 0;
	uint64_t good_count = 0;
	uint64_t bad_count = 0;
	//print the JSON list
	offset += sprintf(message+offset, "[      ");
	for(i=0; i<RTE_MAX_LCORE; i++)//iterate through all the non-empty hashtable of each lcore
	{
		if(ares_ht[i].htbi != NULL)
		{
			printf("Collecting packet validation record on lcore %u ... \n", i);
			for(j=0; j<HASH_TABLE_BUCKET_NUM; j++)//iterate through all the buckets on the column
			{
				for(k=0; k<ares_ht[i].htbi[j].cur_size; k++)//iterate through all the buckets on the row
				{
					//check the realloc_count
					uint32_t old_realloc_count;
					uint32_t old_offset = offset;
					uint32_t old_print_count = print_count;
					uint32_t old_good_count = good_count;
					uint32_t old_bad_count = bad_count;
					do
					{
						old_realloc_count = ares_ht[i].htbi[j].realloc_count;
						while((old_realloc_count % 2) == 1) {old_realloc_count = ares_ht[i].htbi[j].realloc_count;}
						
						good_count = old_good_count;
						bad_count = old_bad_count;
						uint32_t bad_flag = IsFlowBad(&(ares_ht[i].htbi[j].mem_addr[k].fv), &good_count, &bad_count);
						if(bad_flag == 1)
						{
							offset = old_offset;
							print_count = old_print_count;
							PrintFlow(app_data, message, &offset, &(ares_ht[i].htbi[j].mem_addr[k]), &print_count);	
						}
					} while( ((ares_ht[i].htbi[j].realloc_count % 2) == 1) || (ares_ht[i].htbi[j].realloc_count != old_realloc_count));
				}//row
			}//column
		}//lcore with hashtable
	}//lcore loop
	//Replace the last comma by white space
	message[offset-2] = ' ';
	//append the JSON list
	offset += sprintf(message+offset, " ]");
	//send the message out
	printf("%s \n", message);
	//append a report summary
	memset(message, 0, CLIENT_BUFFER_SIZE * sizeof(char));
	sprintf(message, "{ \"report\" : { \"Timestamp\" : %lu, \"Good\" : %"PRId64", \"Bad\" : %"PRId64" } }", time(NULL), good_count, bad_count);
	printf("%s \n", message);
	// free message
	free(message);
}

#endif
