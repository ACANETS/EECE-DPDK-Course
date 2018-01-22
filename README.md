# EECE-DPDK-Course
For UML EECE DPDK Course
In default, after the DPDK box is booted, the NICs are automatically binded with the igb_uio driver, hence you don't have
the network access.
In order to get network access, type in the terminal
	sudo systemctl stop dpdk-config
	sudo pkill -9 dhclient
	sudo dhclient -v

If you want to bind the NICs with igb_uio driver and start DPDK application
	sudo systemctl start dpdk-config
