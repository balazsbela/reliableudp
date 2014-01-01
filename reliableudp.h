#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <time.h>


//Maximum payload size
#define MAXPKTSIZE 1024
//Number of retries before quiting
#define MAXRETRY 3
//how many seconds to wait for ack before resending a packet
#define TIMEOUT 1
//timeout in microseconds
#define TIMEOUT_MSEC 50000
//time to wait when the clients receiving queue is full in milliseconds
#define RETRYTIMEOUT 500

//every n-th ack packet is dropped
#define LOSS_RATIO 15
#define RANDOM_LOSS 0


struct packet {
  uint32_t sequenceNr;
  uint16_t payloadLen;
  uint16_t ackFlag;
  uint32_t ackValue;
  char payload[MAXPKTSIZE];
};

typedef struct packet packet;


struct connectionState {    
  uint32_t sequenceNr;
  uint32_t expectedSequenceNr;
  unsigned int retries;
  char receiveQueue[5*MAXPKTSIZE];     
  unsigned int queueSize;
  sockaddr addr;
};

typedef struct connectionState connectionState;


unsigned char generateError(int err_ratio, unsigned char israndom) {
   static int count = 1;
   //using pure random 0 generation here. err_ratio doesn't count
   if (israndom) {
     if (count==1) srandom( time(NULL) );
     count++;
     return random() % 2;
   }
 
   int result;
   // constant 1 only if err_ratio=0 and israndom=false
   if (err_ratio == 0 )
     result= 1;
   else
     // generate 0 with a err_ratio percentage
     result = count % err_ratio?1:0;
   count++;
   return result;
}


int packetSend(int sockfd,packet* pckt, connectionState *con) {      
  pckt->sequenceNr = htonl(pckt->sequenceNr);
  pckt->ackFlag = htons(pckt->ackFlag);
  pckt->ackValue = htonl(pckt->ackValue);
  pckt->payloadLen = htons(pckt->payloadLen);
  
  //pckt->payload[pckt->payloadLen] = '\0';
  
  int r = 0;
  if( generateError(LOSS_RATIO,RANDOM_LOSS) ) {    
    r = sendto(sockfd,pckt,sizeof(struct packet),0,(struct sockaddr*) &con->addr,sizeof(struct sockaddr_in));    
    if(r<0) {
      perror("packetSend : sendto failed");
      exit(1);
    }   
  }
 // printf("\nSending to %s on port:%d\n",inet_ntoa(((struct sockaddr_in*)&con->addr)->sin_addr),((struct sockaddr_in*)&con->addr)->sin_port);

  return r;
}

int packetRecv(int sockfd,packet* pckt, struct sockaddr* src_addr,size_t* srclen) {
  *srclen = sizeof(struct sockaddr_in);
  int r = recvfrom(sockfd,pckt,sizeof(struct packet),0,src_addr, (socklen_t*) srclen);
  if(r<=0) {
    perror("packetRecv: recvfrom failed");
    exit(1);
  }
  
  pckt->sequenceNr = ntohl(pckt->sequenceNr);
  pckt->ackFlag = ntohs(pckt->ackFlag);
  pckt->ackValue = ntohl(pckt->ackValue); 
  pckt->payloadLen = ntohs(pckt->payloadLen);
  
  //printf("\nReceived from %s on port:%d\n",inet_ntoa(((struct sockaddr_in*) src_addr)->sin_addr),((struct sockaddr_in*)src_addr)->sin_port);
  
  return r;
}


	
void printPacket(struct packet* pckt) {
    printf("\n--Packet with sequenceNr:%d\n",pckt->sequenceNr);
    printf("--ACKFlag:%d\n",pckt->ackFlag);
    printf("--ACKValue:%d\n",pckt->ackValue);
    printf("--Payload length:%d\n",pckt->payloadLen);
    
    char temp[MAXPKTSIZE];
    strncpy(temp,pckt->payload,pckt->payloadLen);
    temp[pckt->payloadLen] = '\0';
    printf("--Payload:%s\n",temp);
}

void printConnectionState(struct connectionState* con) {
    printf("\n----Connection State ---- ");
    printf("\n===SequenceNr:%d",con->sequenceNr);
    printf("\n===ExpectedSequenceNr:%d",con->expectedSequenceNr);
    printf("\n===Retries:%d",con->retries);
    printf("\n===Receive Queue Size:%d",con->queueSize);
    printf("\n");
}

void printRecvQueue(struct connectionState* con) {
    printf("\n==Receive queue size:%d",con->queueSize);
    printf("\n==Receive queue:%s\n",con->receiveQueue);
}

int reliableSend(int sockfd, char* buffer, size_t bufflen, connectionState *con) {
    //determine number of packets and split data
   // int nrP = bufflen / MAXPKTSIZE;
   // int lastSize = bufflen % MAXPKTSIZE;
      
    printf("\nreliableSend called\n");
   
    printConnectionState(con);
    
    size_t sentBytes = 0;
    int lastSent = 0;
    int currentPayloadSize = 0;
    int lastPayloadSize=0;
    struct packet recvpckt;
    struct packet pckt,tempPack;
    memset(&pckt,0,sizeof(struct packet));
    memset(&recvpckt,0,sizeof(struct packet));
    
    con -> retries = 0;
  
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sockfd,&read_fds);
    
    //timeout 
    struct timeval t;	 
	    
    while ( (sentBytes < bufflen) && ( con -> retries < MAXRETRY ) ) {
	
	//complete packet header
	printf("Remaining data:%d\n",bufflen-sentBytes);
	currentPayloadSize = ( (bufflen-sentBytes) > MAXPKTSIZE ) ? MAXPKTSIZE : (bufflen - sentBytes);
	memset(&pckt,0,sizeof(struct packet));
	pckt.sequenceNr = con->sequenceNr;
	pckt.ackFlag = 0;
	pckt.ackValue = 0;
	pckt.payloadLen = currentPayloadSize;
	//printf("Next part of the buffer:%s\n",buffer+sentBytes);
	printf("Current payload size:%d sentBytes:%d\n",currentPayloadSize,sentBytes);	
	memcpy(pckt.payload,buffer+sentBytes,currentPayloadSize);
	lastPayloadSize = currentPayloadSize;
	//pckt.payload[pckt.payloadLen] = '\0';
	
	
	int ret=0;
	int respReceived = 0;
	//retry until response packet is received
	while( (ret < MAXRETRY) && (!respReceived) ) {
	  printf("Try number:%d",ret);
	  printf("\nSending packet:");
	  printPacket(&pckt);
	  
	  memcpy(&tempPack,&pckt,sizeof(struct packet));
	  lastSent = packetSend(sockfd,&tempPack,con);
	 // printf("Sent packet of %d bytes\n",lastSent);
	  if(lastSent<0) {
	    perror("Unable to send packet");	    
	  }
	  if(lastSent == 0) {
	      printf("PACKET LOST\n");
	  }
	  
	  
	  int resume = 1;
	  
	  while(resume) {
	    
	      resume=0;
	      //wait for activity on the socket, response from the client
	      memset(&t,0,sizeof(timeval));
	      //t.tv_sec = TIMEOUT;	
	      t.tv_usec = TIMEOUT_MSEC;
	      FD_ZERO(&read_fds);
	      FD_SET(sockfd,&read_fds);
	      
	      printf("Waiting for ACK\n");
	      if(resume) printf("Resumed select:\n");
	      
	      int r = select(sockfd+1, &read_fds , NULL , NULL , &t );
	      if ( r< 0 ) {	      
		  perror("Select failed\n");
		  exit(1);
	      }
	      
	      if(r == 0) {
		  printf("Timed out\n");
	      }
	      
	      if ( FD_ISSET(sockfd, &read_fds) ) {
		  struct sockaddr_in sourceaddr;
		  size_t addrsize = sizeof(struct sockaddr_in);	      
		  
		  int r=packetRecv(sockfd, &recvpckt , (struct sockaddr*) &sourceaddr,&addrsize);
		  
		  if(r < 0 ) {
		    perror( "Recv error\n" );
		    exit(1);
		  } 
		  		  
		  
		  //printf("Received response of size:%d\n",r);
		  printf("Received response packet:");
		  printPacket(&recvpckt);
		  respReceived = 1;
		  
		  
		  if(!recvpckt.ackFlag) {
		      //We received a packet while waiting for an ACK.	    
		      //if we received the right packet and we have space in the queue for it
		      printf("We received another packet while waiting for an ACK\n");
		      if ( (recvpckt.sequenceNr == con->expectedSequenceNr) && ( (5*MAXPKTSIZE - con->queueSize) > recvpckt.payloadLen ) ) {
			  printf("We received the right packet and the queue has space for it\n");
			  strncat(con->receiveQueue,recvpckt.payload,recvpckt.payloadLen);			    
			  con->queueSize += recvpckt.payloadLen;
			  
			  //expect the next packet and request it
			  con->expectedSequenceNr++;
			  pckt.ackFlag = 1;
			  pckt.ackValue = con->expectedSequenceNr;
			  pckt.payloadLen = 0;
						  
			  //send ack
			  int rt=packetSend(sockfd,&pckt,con);
			  if(rt < 0 ) {
			      perror("PacketSend failed");
			  }
			  if(rt == 0) {
			      printf("PACKET LOST\n");
			  }
			  
		      }
		      else //the queue is full
		      if (recvpckt.sequenceNr == con->expectedSequenceNr) {
			  printf("The queue is full\n");
			  //re-request packet since we don't have space to store it
			  pckt.ackFlag = 1;
			  pckt.ackValue = con->expectedSequenceNr;
			  pckt.payloadLen = 0;
			  int rt=packetSend(sockfd,&pckt,con);
			  if(rt < 0 ) {
			    perror("PacketSend failed");
			  }
			  if(rt == 0) {
			      printf("PACKET LOST!\n");
			  }
		      }
		      //we need to resume waiting for an ACK
		      resume = 1;		      
		  }
    	   		
				      
	      }//fd is set
	  }//while resume
	    
	  ret++;
	}
	
	//check for acknowledgement
	
	if(!respReceived) {
	  printf("NO ACK RECEIVED\n");
	  return -1;
	}
	
	//check ack values
	if(recvpckt.ackFlag) {
	    printf("Current sequence number is:%d . ACKValue:%d\n",con->sequenceNr,recvpckt.ackValue);
	    //it confirmed the right packet
	    if(recvpckt.ackValue == con->sequenceNr + 1) {
		//all went well, package confirmed
		printf("ACK was ok, package confirmed\n");		
		
		//we cand now send the next one
		//exactly as requested since ackVal = seqNr + 1
		con->sequenceNr++;
		
		printf("Current Sequence Number:%d . Expected Sequence Number: %d \n",con->sequenceNr,con->expectedSequenceNr);
		/*printf("sentBytes:%d\n",sentBytes);
		printf("Confirmed payload size:%d\n",lastPayloadSize);*/

		sentBytes += lastPayloadSize;
	    }
	    else 
	    if(recvpckt.ackValue == con->sequenceNr) {
	      //the client's receive queue is full, wait and resend
	      printf("Client's receive queue is full\n");
	      usleep(RETRYTIMEOUT);
	      lastSent = 0;
	    }
	    else //remote party out of sync	      
	    if(recvpckt.ackValue != con->sequenceNr+1) {
	       printf("Remote party out of sync, retrying\n");
	       usleep(RETRYTIMEOUT*1000);
	       con->retries++;
	       lastSent = 0;
	    }
	
	}
	/*else {
	    //We received a packet while waiting for an ACK.	    
	    //if we received the right packet and we have space in the queue for it
	    
	    if ( (recvpckt.sequenceNr == con->expectedSequenceNr) && ( (5*MAXPKTSIZE - con->queueSize) > recvpckt.payloadLen ) ) {
		strncat(con->receiveQueue,recvpckt.payload,recvpckt.payloadLen);
		
		//expect the next packet and request it
		con->expectedSequenceNr++;
		pckt.ackFlag = 1;
		pckt.ackValue = con->expectedSequenceNr;
		pckt.payloadLen = 0;
		
		printf("We received another packet while waiting for an ACK\n");
		//send ack
		if(packetSend(sockfd,&pckt,con) < 0 ) {
		    perror("PacketSend failed");
		}
		
	    }
	    else //the queue is full
	    if (recvpckt.sequenceNr == con->expectedSequenceNr) {
		printf("The queue is full\n");
		//re-request packet since we don't have space to store it
		pckt.ackFlag = 1;
		pckt.ackValue = con->expectedSequenceNr;
		if(packetSend(sockfd,&pckt,con) < 0 ) {
		  perror("PacketSend failed");
		}
		  
	    }
	    
	    //we need to resume waiting for an ACK
	    
	    
	}*/
		
	//sentBytes += lastSent;
	//sentBytes += pckt.payloadLen;
  }
    
  if( con->retries == MAXRETRY) {
    perror("Out of sync\n");
    return -1;
  }
    
  printf("\nreliableSend ended, transferred:%d\n\n",sentBytes);

  return sentBytes;
}



int reliableRecv(int sockfd, char* buffer, size_t bufflen,struct sockaddr* srcaddr, connectionState* con) { 
  
    printf("\nreliableRecv called\n");
    printConnectionState(con);

    //printRecvQueue(con);
    int receivedBytes = 0;
    int lastRecvBytes = 0;
    int totalBytes = 0;
    struct packet recvpckt;
    //int minsize;
    
    if(bufflen == 0) {
      return -1;
    }
    
    //if receive queue is not empty
    if(con->queueSize > 0) {     
                 
      printf("Receive queue not empty. BufferSize: %d QueueSize: %d\n",bufflen,con->queueSize);
      
      //we have more space in the buffer than in the queue
      if(bufflen >= con->queueSize) {
	  printf("We dump the whole queue into the buffer\n");
	  
	  //empty the contents of the queue into the buffer
	  strncpy(buffer,con->receiveQueue,con->queueSize);
	  
	  buffer[con->queueSize] = '\0';
	  printf("Buffer:%s\n",buffer);	 
	  
	  totalBytes = con->queueSize;	  
	  con->receiveQueue[0] = '\0';
	  con->queueSize = 0;
	  
      }
      else {
	  //we have more data in the queue than space in the buffer
	  //copy as much as fits into the buffer
	  
	  printf("Queue has more data than the space in the buffer:\n");	  
	  
	  strncpy(buffer,con->receiveQueue,bufflen);
	  buffer[bufflen] = '\0';
	  totalBytes = bufflen;
	  
	  printf("Buffer:%s\n",buffer);
	  
	  //and remove the rest
	  char tempBuffer[5*MAXPKTSIZE];
	  strncpy(tempBuffer,con->receiveQueue+totalBytes, con->queueSize-bufflen);
	  strncpy(con->receiveQueue,tempBuffer,con->queueSize-bufflen);
	  
	  con->queueSize -= bufflen ;
	  con->receiveQueue[con->queueSize]='\0';
	  printf("RecvQueue:%s",con->receiveQueue);
	  
      }
      
      printf("After dumping buffers: Buflen:%d queueSize:%d\n",bufflen,con->queueSize);	   	

      
    }
    else { //the queue is empty
	   //receive the next packet
	
	printf("Receive queue empty.BufferSize: %d\n", bufflen);
	printf("Expecting packet.\n");
	
	size_t addrsize = sizeof(struct sockaddr_in);
	lastRecvBytes = packetRecv(sockfd,&recvpckt,srcaddr,&addrsize);
	if(lastRecvBytes <= 0) {
	  perror("reliableRecv: failed to receive packet");
	  return -1;
	}
	
	printf("\nReceived packet:");
	//printf("from %s on port:%d\n",inet_ntoa(((struct sockaddr_in*)srcaddr)->sin_addr),((struct sockaddr_in*)srcaddr)->sin_port);
	printPacket(&recvpckt);	
	memcpy(&con->addr,(struct sockaddr_in*)srcaddr,sizeof(sockaddr_in));	
	
	receivedBytes += recvpckt.payloadLen;
	
	printf("Expected sequence number was:%d and received %d\n",con->expectedSequenceNr,recvpckt.sequenceNr);
	
	if(recvpckt.sequenceNr == con->expectedSequenceNr) {
	    //copy the data into receiveQueue	    	    
	    
	    strncat(con->receiveQueue,recvpckt.payload,recvpckt.payloadLen);
	    con->queueSize += recvpckt.payloadLen;
	    
	    //printf("receiveQueue:\n");
	    //printRecvQueue(con);
	    
	    
	    //Expect next packet
	    con->expectedSequenceNr++;
	   
	    struct packet ackPacket;
	    memset(&ackPacket,0,sizeof(struct packet));
	    ackPacket.ackFlag = 1;
	    ackPacket.ackValue = con->expectedSequenceNr;
	    ackPacket.payloadLen = 0;
	  
	    printf("\nSending ACK Packet:");
	    printf(" to %s on port:%d\n",inet_ntoa(((struct sockaddr_in*)&con->addr)->sin_addr),((struct sockaddr_in*)&con->addr)->sin_port);

	    printPacket(&ackPacket);
	    
	    int rt=packetSend(sockfd,&ackPacket,con);
	    if(rt<0) {
	      perror("reliableRecv: failed to send ack packet");
	      return -1;
	    }	 
	    if(rt == 0) {
		printf("PACKET LOST!\n");
	    }
	    
	    //dump the receiveQueue into the buffer
	    
	    printf("Buflen:%d queueSize:%d\n",bufflen,con->queueSize);	   	    
	    
	    //we have more data in the queue than we can dump into the buffer
	    if(con->queueSize > bufflen) { 	
		printf("We have more data in the queue than space in the buffer\n");
				
		//copy as much as we can to the buffer
		strncpy(buffer,con->receiveQueue,bufflen);
		totalBytes = bufflen;	
		buffer[bufflen] = '\0';
		
		printf("Buffer:%s\n",buffer);		
		
		//remove the rest
		char tempBuffer[5*MAXPKTSIZE];
		strncpy(tempBuffer,con->receiveQueue+bufflen, con->queueSize-bufflen);
		strncpy(con->receiveQueue,tempBuffer,con->queueSize-bufflen);		
		con->queueSize -= bufflen ;
		con->receiveQueue[con->queueSize]='\0';
			
	    }
	    else {
		//we can dump the whole queue into the buffer
		strncpy(buffer,con->receiveQueue,con->queueSize);		
		totalBytes = con->queueSize;	
		buffer[con->queueSize] = '\0';
		con->receiveQueue[0] = '\0';
		con->queueSize = 0;	    
	    }
	    
	     //printf("After dumping buffers: Buflen:%d queueSize:%d\n",bufflen,con->queueSize);	   	
	}
	else { //not the right packet
	    
		struct packet ackPacket;
		memset(&ackPacket,0,sizeof(struct packet));
		ackPacket.ackFlag = 1;
		ackPacket.ackValue = con->expectedSequenceNr;
		ackPacket.payloadLen = 0;				
		
		printf("\nSent ACK packet rerequesting the same packet:");
		printPacket(&ackPacket);
		
		int rt = packetSend(sockfd,&ackPacket,con);
		if(rt<0) {
		    perror("reliableRecv: failed to send ack packet");
		    return -1;
		}
		if(rt==0) {
		  printf("PACKET LOST\n");
		}
		
		//drop packet	   
		
	}
	    
    }
    
  //printf("\nreliableRecv ended,transferred:%d\n",receivedBytes);
  printf("\nreliableRecv ended,transferred:%d\n",totalBytes);
  return totalBytes;
}


