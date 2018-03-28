# Lab3
## Introduction:In Lab2-RX, we learned how to use DPDK to receive packets. And in Lab2-TX, we learned how to use DPDK to send packets. In this Lab3, we are going to combine both RX and TX into the same code. 
For RX and TX function, they have RX queue and TX queue respectively.  And they are called hardware queues, since they are directly related to the hardware device. But knowing hardware queue is not enough, sometimes our application needs to use software queue to transfer data between threads. In this Lab3, we are going to learn how to use ring buffer to design such software queue.## Tasks:Develop a DPDK code which has both RX and TX functionality. This code needs to run with 2 and only 2 threads (This is controlled by the “-c” EAL option). Thread-0 uses RX function to receive the packets from port A and push received packets onto a ring buffer R. Thread-1 pull packets out of the ring buffer R and send obtained packets out through the same port A. Your code should have a similar structure like the below

```You only use rte_eal_mp_remote_launch() once in the main(), 
never use it twice or more. There is a simple way to let master
core and slave core to do different things separately. int app_thread(void *arg){  uint32_t lcore_id = rte_lcore_id();  uint32_t master_core_id = rte_get_master_lcore();  if(lcore_id == master_core_id){    //do RX and enqueue  }  else {    //do dequeue and TX  } }
 Then in the main(), you can call
rte_eal_mp_remote_launch(app_thread, NULL, CALL_MASTER) 
to launch your application.
```The testbench program is under the test_echo folder, which is used to generate the input packets for your code and print out how many packets have been echoed back.
## Submissions:A single report which includes the following
1. List all the DPDK ring buffer APIs you used in your code. Explain how you use these APIs to accomplish the tasks. The available APIs can be found at http://dpdk.org/doc/api/rte__ring_8h.html2. Attach your whole code file in the report. 	