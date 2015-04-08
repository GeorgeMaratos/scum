#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include "hw6.h"

#define NOTHING 0
#define ACK_RCV 1

int sequence_number;
int RTT = 0;

int timeval_to_msec(struct timeval *t) { 
	return t->tv_sec*1000+t->tv_usec/1000;
}

void msec_to_timeval(int millis, struct timeval *out_timeval) {
	out_timeval->tv_sec = millis/1000;
	out_timeval->tv_usec = (millis%1000)*1000;
}

int current_msec() {
	struct timeval t;
	gettimeofday(&t,0);
	return timeval_to_msec(&t);
}

int check_sum(char *data,int length) {
  int i,sum = 0;
  for(i=0;i<length;i++)
    sum += data[i];
//  printf("CheckSum:%d\n",sum);
  return sum;
}

int rel_connect(int socket,struct sockaddr_in *toaddr,int addrsize) {
		 connect(socket,(struct sockaddr*)toaddr,addrsize);
}

int rel_rtt(int socket) {
		 return RTT;
}


int wait_for_ack(int seq_num, int socket,void *buf, int len) { //wait for ack is done it just needs the right timeout
/*
naive wait_for_ack (sq_num, socket)
  while !timeout
    packet = receive(packet)
    if seq_num == packet.seq)num
      return 1
  return 0

naive wait_for_ack2
  if(select_for_a_time() == timeout)
    return TIMEOUT:
  else
    if seq_num == packet.seq)num
      return ACK_RCV
    else return wait_for_ack(seq_num,socket)
  
*/

//variables to read the packet
  char packet[MAX_PACKET];
  memset(&packet,0,sizeof(packet));
  struct hw6_hdr* hdr=(struct hw6_hdr*)packet;	

  struct sockaddr_in fromaddr;
  unsigned int addrlen=sizeof(fromaddr);	
  int recv_count;

//variables for timeout
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  fd_set fd;
  FD_ZERO(&fd);
  FD_SET(socket,&fd);

  int retval;

  retval = select(socket+1,&fd,NULL,NULL,&tv);
  if(retval == -1) printf("select error\n");
  if(retval){  
    recv_count = recvfrom(socket, packet, MAX_PACKET, 0, (struct sockaddr*)&fromaddr, &addrlen);
//    printf("%d\n",ntohl(hdr->ack_number));
    if(seq_num == ntohl(hdr->ack_number) && ntohl(hdr->checksum) == check_sum(buf,len))  //need to convert to host order byte
      return ACK_RCV;
    else return NOTHING; 
  }
  else return NOTHING;		
}

void cal_rtt(clock_t a, clock_t b) {
  RTT = (int)((0.875)*(RTT) + (0.275)*(int)(b - a));
//  printf("RTT:%d\n");
}
void rel_send(int sock, void *buf, int len)  
{
/*  
naive sender (socket)
1  packet = make_packet(buffer);
2  send(packet)
3  while (1)
4    if wait_for_ack() == timeout
5      send(packet)
6    if wait_for_ack() == received_ack
7	if check_ack == true
8	  sequence_number++
9	  return
*/


//1
 	// make the packet = header + buf
	char packet[1400];
	struct hw6_hdr *hdr = (struct hw6_hdr*)packet;
	hdr->sequence_number = htonl(sequence_number);
	memcpy(hdr+1,buf,len); //hdr+1 is where the payload starts

	//for timer
	clock_t start, diff;





//2	
	start = clock();
//	printf("Packet from sender:");check_sum(buf,len);
	hdr->checksum = htonl(check_sum(buf,len));
	send(sock, packet, sizeof(struct hw6_hdr)+len, 0);
//3
	while(1) {
	  if(wait_for_ack(sequence_number,sock,buf,len) == NOTHING) {
	    diff = clock() - start;
//	    printf("time%d\n",diff / 100000);
	    if((int)(diff) >= RTT)
	      send(sock, packet, sizeof(struct hw6_hdr)+len, 0);
	  }
	  if(wait_for_ack(sequence_number,sock,buf,len) == ACK_RCV) {
	    sequence_number++;
	    cal_rtt(start,clock());
  	    return;
	  }
	}

}

int rel_socket(int domain, int type, int protocol) {
	sequence_number = 0;
	return socket(domain, type, protocol);
}



int seq_expected = 0;

int rel_recv(int sock, void * buffer, size_t length) {


/* global.ack_expected = 0

naive receiver (socket)
1     packet = receive(packet)
2       send_ack(packet.sequence)
*/



	char packet[MAX_PACKET];
	memset(&packet,0,sizeof(packet));
	struct hw6_hdr* hdr=(struct hw6_hdr*)packet;	

	struct sockaddr_in fromaddr;
	unsigned int addrlen=sizeof(fromaddr);	
	int recv_count = recvfrom(sock, packet, MAX_PACKET, 0, (struct sockaddr*)&fromaddr, &addrlen);		//1

	int seq,check;

	// this is a shortcut to 'connect' a listening server socket to the incoming client.
	// after this, we can use send() instead of sendto(), which makes for easier bookkeeping
	if(connect(sock, (struct sockaddr*)&fromaddr, addrlen)) {
		perror("couldn't connect socket");
	}

//2

	//receiver must send an ack of the segment
	//if expected is received then send the ack and return
	//if sequence is lower than expected, then send last ack and wait for response
	//if sequence is higher than expected, then do the same

     while(1) {
	seq = ntohl(hdr->sequence_number);
	check = ntohl(hdr->checksum);
	if(seq == seq_expected && check == htonl(check_sum(buffer,length))) {
	  hdr->ack_number = htonl(seq);
//	  printf("sending ack:%d\n",ntohl(hdr->ack_number));
	  hdr->checksum = htonl(check_sum(buffer,length));
	  send(sock, packet, sizeof(struct hw6_hdr)+length, 0);   //not sure if length is supposed to be here
	  printf("Packet from receiver:");check_sum(buffer,length);
	  seq_expected++;
	  memcpy(buffer, packet+sizeof(struct hw6_hdr), recv_count-sizeof(struct hw6_hdr));
	  return recv_count - sizeof(struct hw6_hdr);
	}
	else{
	  hdr->ack_number = htonl(seq_expected - 1);
	  hdr->checksum = htonl(check_sum(buffer,length));
//	  printf("sending ack:%d\n",ntohl(hdr->ack_number));
	  send(sock, packet, sizeof(struct hw6_hdr)+length, 0);   //not sure if length is supposed to be here
	  recv_count = recvfrom(sock, packet, MAX_PACKET, 0, (struct sockaddr*)&fromaddr, &addrlen);		//1
	}
     }
//	fprintf(stderr, "Got packet %d\n", ntohl(hdr->sequence_number));

}

int rel_close(int sock) {
	rel_send(sock, 0, 0); // send an empty packet to signify end of file
	close(sock);
}

