executable1 = travelMonitorClient
executable2 = monitorServer

all : travelMonitorClient monitorServer

travelMonitorClient: travelMonitorClient.c extra_lists.c skiplist.c get_string.c bloom_travel.c
	gcc -o travelMonitorClient travelMonitorClient.c extra_lists.c skiplist.c get_string.c bloom_travel.c -g

monitorServer: monitorServer.c get_string.c bloom.c extra_lists.c skiplist.c
	gcc -o monitorServer monitorServer.c get_string.c bloom.c extra_lists.c skiplist.c -lpthread -g
clean:
	rm -f $(executable1) $(executable2) 