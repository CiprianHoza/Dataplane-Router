#include "protocols.h"
#include "queue.h"
#include "lib.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define MIN_LENGTH sizeof(struct ether_hdr)
#define IPv4_TYPE 0x0800
#define ARP_TYPE 0x0806
#define ARP_REPLY 2
#define ARP_REQUEST 1

struct trie_node
{
	struct trie_node *children[2];
	struct route_table_entry *route;
};

struct wrapper
{
	char buffer[MAX_PACKET_LEN];
	int len;
};

struct route_table_entry *rtable;
int rtable_len;

struct arp_table_entry *arptable;
int arptable_len_curr, arptable_len_max;

queue waiting;

int get_mask_len(uint32_t mask)
{
	int count = 0;
	while (mask > 0)
	{
		if ((mask & 1) == 1)
			count++;
		mask = mask >> 1;
	}
	return count;
}

int get_mac_arp(uint32_t next_hop)
{
	int i;
	for (i = 0; i < arptable_len_curr; i++)
		if (next_hop == arptable[i].ip)
			return i;
	return -1;
}

void insert_trie(struct trie_node *root, struct route_table_entry *entry)
{
	struct trie_node *node = root;
	uint32_t ip = ntohl(entry -> prefix);
	int mask_len = get_mask_len(ntohl(entry -> mask));

	for (int i = 31; i >= 32 - mask_len; i--)
	{
		int bit = (ip >> i) & 1;
		if (!node -> children[bit])
			node -> children[bit] = calloc(1, sizeof(struct trie_node));
		node = node -> children[bit];
	}
	node -> route = entry;
}

struct route_table_entry* get_best_route(struct trie_node *root, uint32_t ip_dest)
{
	struct trie_node *node = root;
	struct route_table_entry *last_valid_route = NULL;
	uint32_t ip = ntohl(ip_dest);

	for (int i = 31; i>=0; i--)
	{
		int bit = (ip >> i) & 1;

		if (node -> route != NULL)
		{
			last_valid_route = node -> route;
		}

		if (!node -> children[bit])
			return last_valid_route;
		node = node -> children[bit];
	}

	if (node && node -> route != NULL)
		last_valid_route = node -> route;

	return last_valid_route;
}

void send_icmp_error(char *buf, uint8_t type, uint8_t code, int interface)
{
	struct icmp_hdr *icmp = (struct icmp_hdr*)(buf + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));

	struct ip_hdr old_ip_hdr;
	memcpy(&old_ip_hdr, buf + sizeof(struct ether_hdr), sizeof(struct ip_hdr));

	memcpy((uint8_t*)icmp + sizeof(struct icmp_hdr), &old_ip_hdr, sizeof(struct ip_hdr));
	memcpy((uint8_t*)icmp + sizeof(struct icmp_hdr) + sizeof(struct ip_hdr),
			buf + sizeof(struct ether_hdr) + sizeof(struct ip_hdr), 8 * sizeof(uint8_t));

	icmp -> mtype = type;
	icmp -> mcode = code;
	icmp -> check = 0;
	icmp -> check = htons(checksum((uint16_t*)icmp, sizeof(struct icmp_hdr) + sizeof(struct ip_hdr) + 
													8 * sizeof(uint8_t)));
	
	struct ip_hdr *ip_h = (struct ip_hdr*)(buf + sizeof(struct ether_hdr));
	ip_h -> dest_addr = old_ip_hdr.source_addr;
	uint32_t ip;
	inet_pton(AF_INET, get_interface_ip(interface), &ip);
	ip_h -> source_addr = ip;
	ip_h -> ttl = 64;
	ip_h -> proto = 1;
	ip_h -> tot_len = htons(sizeof(struct ip_hdr) + sizeof(struct icmp_hdr) + sizeof(struct ip_hdr) + 8 * sizeof(uint8_t));
	ip_h -> checksum = 0;
	ip_h -> checksum = htons(checksum((uint16_t*)ip_h, sizeof(struct ip_hdr)));

	struct ether_hdr *eth_hdr = (struct ether_hdr*)buf;
	memcpy(eth_hdr -> ethr_dhost, eth_hdr -> ethr_shost, 6 * sizeof(uint8_t));
	get_interface_mac(interface, eth_hdr -> ethr_shost);

	int len = sizeof(struct ether_hdr) + 2 * sizeof(struct ip_hdr) + sizeof(struct icmp_hdr) + 8 * sizeof(uint8_t);
	send_to_link(len, buf, interface);
}


int main(int argc, char *argv[])
{
	char buf[MAX_PACKET_LEN];

	// Do not modify this line
	init(argv + 2, argc - 2);

	struct trie_node *trie = calloc(1, sizeof(struct trie_node));

	rtable = (struct route_table_entry*)malloc(80000 * sizeof(struct route_table_entry));
	arptable = (struct arp_table_entry*)malloc(50 * sizeof(struct arp_table_entry));

	rtable_len = read_rtable(argv[1], rtable);
	arptable_len_curr = 0;
	arptable_len_max = 50;

	for (int i = 0; i < rtable_len; i++)
		insert_trie(trie, &rtable[i]);
	
	waiting = create_queue();

	while (1) {

		size_t interface;
		size_t len;

		uint8_t mac_to[6], broadcast[6];

		for (int i = 0; i < 6; i++)
			broadcast[i] = 255; //FF:FF:FF:FF:FF:FF

		interface = recv_from_any_link(buf, &len);
		DIE(interface < 0, "recv_from_any_links");

		if (len < MIN_LENGTH)
			continue;
		
		struct ether_hdr *eth_hdr = (struct ether_hdr*)buf;

		get_interface_mac(interface, mac_to);
		if (memcmp(mac_to, eth_hdr -> ethr_dhost, 6 * sizeof(uint8_t)) != 0 &&
			memcmp(broadcast, eth_hdr -> ethr_dhost, 6 * sizeof(uint8_t)) != 0)
			continue;

		if (ntohs(eth_hdr -> ethr_type) == IPv4_TYPE)
		{
			struct ip_hdr *ip_hhdr = (struct ip_hdr*)(buf + sizeof(struct ether_hdr));

			uint32_t dest = ntohl(ip_hhdr -> dest_addr);
			uint8_t for_me = 0;

			for (int i = 0; i < ROUTER_NUM_INTERFACES; i++)
			{
				uint32_t ip_interface;
				inet_pton(AF_INET, get_interface_ip(i), &ip_interface);
				ip_interface = ntohl(ip_interface);

				if (dest == ip_interface)
				{
					for_me = 1;
					break;
				}
			}

			if (for_me)
			{
				if (ip_hhdr -> proto == 1)
				{
					struct icmp_hdr *icmp = (struct icmp_hdr*)(buf + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));
					if (icmp -> mtype == 8)
					{
						icmp -> mtype = 0;
						icmp -> mcode = 0;

						icmp -> check = 0;
						icmp -> check = htons(checksum((uint16_t*)icmp, len - sizeof(struct ether_hdr) - sizeof(struct ip_hdr)));

						uint32_t aux = ip_hhdr -> source_addr;
						ip_hhdr -> source_addr = ip_hhdr -> dest_addr;
						ip_hhdr -> dest_addr = aux;
						ip_hhdr -> ttl = 64;
						ip_hhdr -> checksum = 0;
						ip_hhdr -> checksum = htons(checksum((uint16_t*)ip_hhdr, sizeof(struct ip_hdr)));

						uint8_t aux_mac[6];
						memcpy(aux_mac, eth_hdr -> ethr_shost, 6 * sizeof(uint8_t));
						memcpy(eth_hdr -> ethr_shost, eth_hdr -> ethr_dhost, 6 * sizeof(uint8_t));
						memcpy(eth_hdr -> ethr_dhost, aux_mac, 6 * sizeof(uint8_t));

						send_to_link(len, buf, interface);
					}
				}
			}
			else
			{
				uint16_t sum = ntohs(ip_hhdr -> checksum);
				ip_hhdr -> checksum = 0;
				ip_hhdr -> checksum = checksum((uint16_t*)ip_hhdr, sizeof(struct ip_hdr));

				if (sum != ip_hhdr -> checksum)
					continue;
				
				if (ip_hhdr -> ttl <= 1)
				{
					send_icmp_error(buf, 11, 0, interface);
					continue;
				}
				else
					ip_hhdr -> ttl--;
				
				struct route_table_entry* best_route = get_best_route(trie, ip_hhdr -> dest_addr);

				if (best_route == NULL)
				{
					send_icmp_error(buf, 3, 0, interface);
					continue;
				}
				
				ip_hhdr -> checksum = 0;
				ip_hhdr -> checksum = htons(checksum((uint16_t*)ip_hhdr, sizeof(struct ip_hdr)));

				uint8_t src_mac[6];
				int index = get_mac_arp(best_route -> next_hop);
				get_interface_mac(best_route -> interface, src_mac);

				memcpy(eth_hdr -> ethr_shost, src_mac, 6 * sizeof(uint8_t));

				if (index != -1)
				{
					memcpy(eth_hdr -> ethr_dhost, arptable[index].mac, 6 * sizeof(uint8_t));
					send_to_link(len, buf, best_route -> interface);
				}
				else
				{
					struct wrapper *wp = (struct wrapper *)malloc(sizeof(struct wrapper));
					wp -> len = len;
					memcpy(wp -> buffer, buf, len);

					queue_enq(waiting, wp);

					char arp_packet[sizeof(struct ether_hdr) + sizeof(struct arp_hdr)];
					struct ether_hdr *eth_req = (struct ether_hdr *)arp_packet;
					struct arp_hdr *arp_req = (struct arp_hdr *)(arp_packet + sizeof(struct ether_hdr));

					get_interface_mac(best_route -> interface, eth_req -> ethr_shost);

					memcpy(eth_req -> ethr_dhost, broadcast, 6 * sizeof(uint8_t));
					eth_req -> ethr_type = htons(ARP_TYPE);

					arp_req -> hw_type = htons(1);
					arp_req -> proto_type = htons(IPv4_TYPE);
					arp_req -> hw_len = 6;
					arp_req -> proto_len = 4;
					arp_req -> opcode = htons(ARP_REQUEST);

					get_interface_mac(best_route -> interface, arp_req -> shwa);

					uint32_t ip;
					inet_pton(AF_INET, get_interface_ip(best_route -> interface), &ip);
					arp_req -> sprotoa = ip;

					memset(arp_req -> thwa, 0x00, 6 * sizeof(uint8_t));
					arp_req -> tprotoa = best_route -> next_hop;

					send_to_link(sizeof(arp_packet), arp_packet, best_route -> interface);
				}
			}
		}
		else if (ntohs(eth_hdr -> ethr_type) == ARP_TYPE)
		{
			struct arp_hdr* arp_hhdr = (struct arp_hdr*)(buf + sizeof(struct ether_hdr));

			if (arp_hhdr -> opcode == htons(ARP_REPLY))
			{
				arptable[arptable_len_curr].ip = arp_hhdr -> sprotoa;
				memcpy(arptable[arptable_len_curr].mac, arp_hhdr -> shwa, 6 * sizeof(uint8_t));
				arptable_len_curr++;

				if (arptable_len_curr == arptable_len_max)
				{
					arptable = (struct arp_table_entry*)realloc(arptable, (arptable_len_curr + 50) * sizeof(struct arp_table_entry));
					arptable_len_max += 50;
				}

				queue aux = create_queue();

				while (!queue_empty(waiting))
				{
					struct wrapper *packet = queue_deq(waiting);

					struct ip_hdr *ip_hhdr = (struct ip_hdr*)(packet -> buffer + sizeof(struct ether_hdr));
					struct route_table_entry *route = get_best_route(trie, ip_hhdr -> dest_addr);

					if (route -> next_hop == arp_hhdr -> sprotoa)
					{
						struct ether_hdr *eth_h = (struct ether_hdr*)packet -> buffer;

						memcpy(eth_h -> ethr_dhost, arp_hhdr -> shwa, 6 * sizeof(uint8_t));
						send_to_link(packet -> len, packet -> buffer, route -> interface);
						free(packet);
					}
					else
						queue_enq(aux, packet);
				}
				waiting = aux;
			}
			else if (arp_hhdr -> opcode == htons(ARP_REQUEST))
			{
				uint32_t ip;
				inet_pton(AF_INET, get_interface_ip(interface), &ip);

				if (arp_hhdr -> tprotoa == ip)
				{
					memcpy(eth_hdr -> ethr_dhost, eth_hdr -> ethr_shost, 6 * sizeof(uint8_t));
					get_interface_mac(interface, eth_hdr -> ethr_shost);

					arp_hhdr -> opcode = htons(ARP_REPLY);
					memcpy(arp_hhdr -> thwa, arp_hhdr -> shwa, 6 * sizeof(uint8_t));
					arp_hhdr -> tprotoa = arp_hhdr -> sprotoa;

					get_interface_mac(interface, arp_hhdr -> shwa);
					arp_hhdr -> sprotoa = ip;

					send_to_link(sizeof(struct ether_hdr) + sizeof(struct arp_hdr), buf, interface);
				}
			}
		}	

	}
}

