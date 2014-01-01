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


typedef struct sockaddr_in sockaddr_in;
int sock, r;
socklen_t length;
struct sockaddr_in server, from;
struct hostent *hp;
char filename[256];
char buffer[MAXPKTSIZE];

connectionState con;

int main(int argc, char *argv[])
{
   if (argc < 5) { 
      printf("Usage: ./client server port filename buffersize\n");
      exit(1);
   }
   
   const unsigned int bufsize = atoi(argv[4]);
   char buffer[bufsize];
   
   if(strlen(argv[3]) < 256) {
      strncpy(filename,argv[3],strlen(argv[3]));
   }
   
   sock = socket(AF_INET, SOCK_DGRAM, 0);
   if (sock < 0) {
      perror("Could not create socket");
      exit(1);
   }

   server.sin_family = AF_INET;
   hp = gethostbyname(argv[1]);
   if (hp==0){
      perror("Could not resolve host");
      exit(1);
   }

   memcpy((char *)&server.sin_addr,(char *)hp->h_addr,hp->h_length);   
   server.sin_port = htons(atoi(argv[2]));   
   length=sizeof(struct sockaddr_in);
          
   //Send requested filename
   
   if(strlen(filename) > bufsize) {
      printf("Buffer too small to contain filename\n");
      return -1;
   }
   
   memset(buffer,0,bufsize);
   strncpy(buffer,filename,strlen(filename));   
   printf("Filename:%s\n",buffer);
   
   memset(&con,0,sizeof(struct connectionState));
   memcpy(&con.addr,&server,sizeof(sockaddr_in));
   size_t fileLen = strlen(buffer);   
   
   //printf("Con addr is: %s on port:%d\n",inet_ntoa(((struct sockaddr_in*)&con.addr)->sin_addr),((struct sockaddr_in*)&con.addr)->sin_port);

   if(reliableSend(sock,buffer,fileLen,&con) <= 0 ) {
      perror("Could not send filename");
      exit(1);
   }
  
   //printf("Con addr is: %s on port:%d\n",inet_ntoa(((struct sockaddr_in*)&con.addr)->sin_addr),((struct sockaddr_in*)&con.addr)->sin_port);

   //get back expected nr of packets
   int r = reliableRecv(sock,buffer,bufsize,(struct sockaddr*) &server,&con);
   if( r <= 0 ) {
      printf("Could not receive filesize\n");
      exit(1);
   }
   
   buffer[r] = '\0';
   printf("Size of file: %s \n",buffer);
   uint32_t fSize = atoi(buffer);
   
   if(fSize == 0) {
      printf("File does not exist!\n");
      exit(0);
   }


   //create the file 
   int nr=0;
   char file[256];
   sprintf(file,filename);
   int fd = open(file, O_CREAT | O_EXCL | O_WRONLY , 777 );
   while  ( fd < 0 ){
     nr++;
     sprintf(file,"%s%d",filename,nr);
     fd = open(file, O_CREAT | O_EXCL | O_WRONLY , 777 );
   }
   printf("New file name: %s\n",file);      
   
   
   uint32_t totalRecv = 0;
   uint32_t recvBytes=0;
   while(totalRecv < fSize) {	  
      recvBytes=reliableRecv(sock,buffer,bufsize,(struct sockaddr*) &server,&con);
      if( recvBytes < 0 ) {
	  printf("reliableRecv:%d\n",recvBytes);
	  exit(1);
      }
      
      if ( write(fd,buffer,recvBytes) < 0) {
             perror("Could not write to file");
      }
      
      
      totalRecv += recvBytes;
   }
      
   close(fd);
   return 0;
}
