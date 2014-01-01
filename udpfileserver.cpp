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

#include "reliableudp.h"

#define MAXCLIENTS 50


//the information we know about each client

struct clientdata {
   int fd; //the requested file's descriptor
   int sock; //the socket 
   connectionState con; // connection state
};

//for conveniance
typedef struct clientdata clientdata;
typedef struct sockaddr_in sockaddr_in;


clientdata clients[MAXCLIENTS]; //we can handle up to 50 clients
int nrClients=0;
fd_set read_fds,write_fds;  //file descriptor sets
int listSock; //listening socket descriptor
int fdmax;
sockaddr_in saddr;
char buffer[MAXPKTSIZE];


int getFileSize(char* fname) {
   int sz = 0;
   FILE* fd = fopen(fname,"r");
   if(fd!=NULL) {
         fseek(fd, 0L, SEEK_END);
         sz = ftell(fd);
   }
  
   fclose(fd);
   
   return sz;
}


int main(int argc,char** argv) {
 
   if (argc < 3) {
      printf("Usage ./server portno buffersize\n");
      exit(0);
   }
 
   const unsigned int bufsize = atoi(argv[2]);  
   char buffer[bufsize]; 
 
   listSock = socket(AF_INET, SOCK_DGRAM, 0);
   if (listSock < 0) {
      perror("Could not create socket");
      exit(1);
   }   
   
   int length = sizeof(sockaddr_in);      
   memset(&saddr,0,length);
   
   saddr.sin_family=AF_INET;
   saddr.sin_addr.s_addr=INADDR_ANY;
   saddr.sin_port=htons(atoi(argv[1]));
   
   if (bind(listSock,(struct sockaddr *)&saddr,length) < 0 ) {
       perror("Could not bind listSock");
       exit(1);
   }
   
   int yes=1;
   if (setsockopt(listSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int) ) == -1) {
        perror("Could not setsockopt:");
        exit(1);
   }
   
   //init connectionstate
   for(int i=0;i<MAXCLIENTS;i++) {
      memset(&(clients[i]),0,sizeof(struct clientdata));
      memset(&(clients[i]).con,0,sizeof(struct connectionState));
   }      

   sockaddr_in caddr;      
   
   FD_ZERO(&read_fds);
   FD_SET(listSock, &read_fds);
   int fdmax = 0;
        
   while (1) {
       FD_ZERO(&read_fds);
       FD_ZERO(&write_fds);
       fdmax = listSock;
       FD_SET(listSock, &read_fds);
       
       for(int i=0;i<nrClients;i++) {
	    if(clients[i].sock > 0 ) {
		FD_SET(clients[i].sock,&write_fds);
		if( clients[i].sock > fdmax ) {
		    fdmax = clients[i].sock;
		}
	    }
       }
       
       if (select(fdmax+1, &read_fds, &write_fds, NULL, NULL) < 0) {
          perror("Select failed:");
          exit(1);
       }
       
       if ( FD_ISSET(listSock, &read_fds) ) {

	  int r = reliableRecv(listSock,buffer,bufsize,(struct sockaddr*) &caddr,&(clients[nrClients].con));
	  printf("Total bytes received: %d bytes\n",r);
	  buffer[r]='\0';
	  printf("Received: %s\n",buffer);
	 	  
//	  printf("New client connecting\n");

	  clients[nrClients].sock = socket(AF_INET,SOCK_DGRAM,0);
	  if(clients[nrClients].sock<0) {
	      perror("Could not create client socket");
	      //exit(1);
	  }
	  
	  int yes = 1;
	  if (setsockopt(clients[nrClients].sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int) ) == -1) {
	      perror("Could not setsockopt:");
	  }
	  

	  /*int caddr_len = sizeof(struct sockaddr_in);
	  if(bind(clients[nrClients].sock,(struct sockaddr*) &caddr,caddr_len) < 0 ) {
	      perror("Could not bind client");
	      exit(1);
	  }*/
	  	 
	  int i = nrClients;	  
	  nrClients++;

	  char filename[256];
	  strcpy(filename,buffer);
	  
	  printf("Filename:%s with length:%d\n",filename,strlen(filename));
	  //We check if it exists
	  int fd = open(filename,O_RDONLY);
	  if(fd<0){
	      //file doesn't exist
	      strcpy(buffer,"NONEXIST\0");                
	      
	      //send response
	      int buflen = strlen(buffer);
	      r = reliableSend(listSock,buffer,buflen,&(clients[i].con));
	      if(r<=0) {
		  printf("Could not send response:%d",r);
		  exit(1);
	      }
	  }
	  else {
		clients[i].fd = fd;
		uint32_t fsize = getFileSize(filename);
		memset(buffer,0,bufsize);
		sprintf(buffer,"%d",fsize);   
		
		//send package count
		int buflen = strlen(buffer);
		r = reliableSend(listSock,buffer,buflen,&(clients[i].con));
		if(r<=0) {
		    printf("Could not send response:%d",r);
		    exit(1);
		}	
	  }
	  
      }//fd_isset
      
      int rd=0;	
      int r;
      //printf("Number of clients:%d\n",nrClients);
      for(int j=0;j<nrClients;j++) {
	//printf("Client socket:%d\n",clients[j].sock);  
	if( FD_ISSET(clients[j].sock,&write_fds ) ) {
	      rd=read(clients[j].fd,buffer,bufsize);	
	      if(rd>0) {		  	      
		  printf("Read buffer\n");
		  
		  r = reliableSend(clients[j].sock,buffer,rd,&(clients[j].con));
		  if(r<=0) {
		      printf("Could not send response:%d",r);
		      exit(1);
		  }	      
	      }
	      else {
		  
	      }
	  }					 
      }
				
	  		  
   }
  
  return 0;
}