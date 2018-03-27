# Lab5

## Introduction:
In Lab2-RX we have learned how to use single RX queue to receive the packets. But, single RX queue would not be sufficient in high-volume network. Also, single RX queue would not take the advantage of multicore architecture. In this Lab5, we are going to learn how to use RSS (Receive Side Scaling) to increase the scalability of the DPDK application.

## Tasks:
1. Read the example source code of l3fwd and ip-pipeline to find out how the RSS is used to implement multiple RX queues. You can use *grep -ri “rss”* to quickly find out where RSS is being used.
2. The main.c code of Lab2-RX can only run with a single RX queue, you need to modify the main.c by using what you have learned from the example source code, such that main.c can support multiple RX queues.

## Note:
After you finish Lab2 and Lab3, you should have basic understanding of the DPDK, which means you can easily answer these questions:

1. Which API is used to setup EAL?
1. Which API is used to configure the device (hardware port), and specify the number of RX queues and TX queues etc?
1. Which API is used to setup RX queue, and in order to setup RX queue, which API is used to setup memory pool?
1. Which API is used to setup TX queue?
1. Which API is used to enable the promiscuous mode?
1. Which API is used to start the link?
1. Which API is used to launch the user-defined application for each enabled core?
1. Which API is used to receive the packets from the device?
1. Which API is used to send the packets out to the device?

These questions can help you understand the Big-Picture of any DPDK application.
 
The final goal of Lab5 is to enable and use multiple RX queues to receive the packets. You need to use "-c" EAL option to enable multiple cores in your program, and then you need to control each lcore to work on its own RX queue.
 
First, you need to read the l3fwd example source code and try to answer the above questions again, so in this way, you can easily find out the Big-Picture of the l3fwd example. Once you figure out the Big-Picture, please then focus on the codes used to setup of the RX queues, since this is the main task in lab5. Please find out the differences on how to setup the RX queues between the RX code(Lab2) and l3fwd example, and apply the changes accordingly to the RX code (Lab2), so in this way your modified code can support multiple RX queues.  
 
Second, the developer of the l3fwd example has not yet fully finished the RSS part, so you probably can only see the received packets in only one lcore. Then your next task is going to read and learn the init.c of the example ip-pipeline. You are going to mainly learn how to correctly use "rte_eth_dev_rss_reta_update()" to modify the redirection table.

## Steps
In general, you need to follow the steps in the below to enable multiple RX queues.

* Change the configuration for the PORT

```
struct rte_eth_conf conf;
conf.rxmode={ 
                          .mq_mode = ETH_MQ_RX_RSS,
                          
                          .rx_adv_conf = {
                                .rss_conf = {
                                               .rss_key = NULL,
                                               .rss_key_len = 40,
                                               .rss_hf = ETH_RSS_IPV4,
                                            },
                           },
}
```

* Change the number of RX queues
```
rte_eth_dev_configure(pmd_id, NUM_RX_QUEUES, 1, &conf);
```

* Setup the mempool for each RX queue and configure the RX queue
```
for_each_RX_queue {
    struct rte_mempool * mp;
    mp = rte_pktmbuf_pool_create( name, pool_size, cache_size, priv_size, data_size, socket_id); //name for each pool should be distinct
    rte_eth_rx_queue_setup(pmd_id, queue_id, rx_desc_size, socket_id, &rx_queue_conf, mp);
}
```

* Change the redirection table by the API: 
```
rte_eth_dev_rss_reta_update()
```

* In the subroutine for each core, ```rte_eth_rx_burst()``` needs to use different queue\_id, since we want each core to work on a different RX queue.


## Submissions:
A single report which includes the following

1. Summarize the key steps which are used to enable the multiple RX queues with RSS. 
2. Your modified main.c.

