# EECE-DPDK-Course
For UML EECE DPDK Course

In default, after the DPDK box is booted, the NICs are automatically binded with the igb_uio driver, hence you don't have
the network access.

In order to get network access, type in the terminal

	"sudo systemctl stop dpdk-config"
	"sudo pkill -9 dhclient"
	"sudo dhclient -v"

If you want to bind the NICs with igb_uio driver and start DPDK application

	"sudo systemctl start dpdk-config"

For how to log into the box, you have two options:

	Option1: Use the HDMI cable to connect the HDMI port of the DPDK box to the HDMI port of the monitor which supports HDMI.
	Option2: Connect your Laptop to the DPDK box via the ethernet cable, so that you can ssh log into it.
		1. Insert the USB-RJ45 adapter into the DPDK box USB 2.0 port (the white one).
		2. Power up the DPDK box.
		3. Connect your laptop to the adapter via ethernet cable.
		4. Assign the static IP address 10.0.0.2, and netmask 255.255.255.0 to your laptop.
		5. In the lap, type "ssh test@10.0.0.1", and then enter password "tester".
		6. Now, you can remotely control your DPDK box on your laptop.
![Screenshot](dpdk-box-connection.png)
