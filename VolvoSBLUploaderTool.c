// AW 23/06/2024
// Set ECU into bootloader mode then send custom SBL and give menu to interract with it
// To compile : gcc ./VolcanoSBLProg.c -o volcanosblprog
//
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define BLOCK_SIZE 8
#define CAN_DIAG_ID 0x000FFFFEU

const char frames[8][8]={{0xFF,0x86,0x00,0x00,0x00,0x00,0x00,0x00},
                         {0x50,0xBE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
                         {0x50,0x88,0x00,0x00,0x00,0x00,0x00,0x00},
                         {0x50,0xC0,0x00,0x00,0x00,0x00,0x00,0x00},
                         {0x50,0x9C,0x00,0x00,0x10,0x00,0x00,0x00},
                         {0x50,0xB4,0x00,0x00,0x00,0x00,0x00,0x00},
                         {0x50,0xA0,0x00,0x00,0x00,0x00,0x00,0x00},
                         {0x50,0xC8,0x00,0x00,0x00,0x00,0x00,0x00}};


int sendframe (int can_sock, struct can_frame frame)
{
  
	//sprintf(frame.data, "Hello");
	if (write(can_sock, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
		perror("Write");
		return 1;
	}
 return 0;
}

// AW 16/08/2023
// Configuration de la configuration CAN
int setupCANSock(char * canport, struct sockaddr_can *addr)
{
	struct ifreq ifr;
	int s;
	
	if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("Socket");
		return 1;
	}
	
	fcntl(s, F_SETFL, O_NONBLOCK);

	memset(addr, 0, sizeof(addr));

	addr->can_family = AF_CAN;
	
	memset(&ifr.ifr_name, 0, sizeof(canport));
	strcpy(ifr.ifr_name, canport);
	
	ioctl(s, SIOCGIFINDEX, &ifr);

	addr->can_ifindex = ifr.ifr_ifindex;

	const int timestamp_on = 1;
	if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMP,&timestamp_on, sizeof(timestamp_on)) < 0) {
		perror("setsockopt SO_TIMESTAMP");
	}

	const int dropmonitor_on = 1;
  	if (setsockopt(s, SOL_SOCKET, SO_RXQ_OVFL,&dropmonitor_on, sizeof(dropmonitor_on)) < 0) {
		perror("setsockopt SO_RXQ_OVFL not supported by your Linux Kernel");
	}

	if (bind(s, (struct sockaddr *)addr, sizeof(addr)) < 0) {
		perror("Bind");
		return -1;
	}
	return s;
}

// AW 16/08/2023
// Reception et traitement d'u message CAN
int canRead(int s, struct sockaddr *addr,  struct can_frame *frame)
{
	int i;
	struct iovec iov;
	struct cmsghdr *cmsg;
	struct msghdr msg;
	static __u32 dropcnt;
	struct timeval tv;
  int rxcnt=0;
	
	char ctrlmsg[CMSG_SPACE(sizeof(struct timeval)) + CMSG_SPACE(sizeof(__u32))];
	int nbytes;
		
	//nbytes = read(s, &frame, sizeof(struct can_frame));

	iov.iov_base = frame;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = addr;
	msg.msg_namelen = sizeof(struct sockaddr);
	msg.msg_control = &ctrlmsg;
	msg.msg_controllen = sizeof(ctrlmsg);
  	iov.iov_len = sizeof(struct can_frame);
  	msg.msg_flags = 0;
  	nbytes = recvmsg(s, &msg, 0);

	
 	if (nbytes < 0) {
 		return -1;
		perror("Read");
		return 1;
	}
	
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg && (cmsg->cmsg_level == SOL_SOCKET);cmsg = CMSG_NXTHDR(&msg,cmsg)) 
		{
			if (cmsg->cmsg_type == SO_TIMESTAMP) 
				{
					//memcpy(&tv, CMSG_DATA(cmsg), sizeof(tv));
       				memcpy(&tv, CMSG_DATA(cmsg), sizeof(struct timeval));
				} 
			else if (cmsg->cmsg_type == SO_TIMESTAMPING) 
				{
					struct timespec *stamp = (struct timespec *)CMSG_DATA(cmsg);

					/*
					 * stamp[0] is the software timestamp
					 * stamp[1] is deprecated
					 * stamp[2] is the raw hardware timestamp
					 * See chapter 2.1.2 Receive timestamps in
					 * linux/Documentation/networking/timestamping.txt
					 */
					tv.tv_sec = stamp[2].tv_sec;
					tv.tv_usec = stamp[2].tv_nsec/1000;

				} 
			else if (cmsg->cmsg_type == SO_RXQ_OVFL)
	              {
	                memcpy(&dropcnt, CMSG_DATA(cmsg), sizeof(__u32));
	                printf("Overflow !! (%d)\r\n", dropcnt);
	              }
					
			}
	rxcnt++;	
  frame->can_id &= ~CAN_EFF_FLAG;
	printf("%lu.%6lu : 0x%08X [%d] ",tv.tv_sec, tv.tv_usec,frame->can_id, frame->can_dlc);

	for (i = 0; i < frame->can_dlc; i++)
		printf("%02X ",frame->data[i]);

	int d[9];
	for (i=0; i<frame->can_dlc;i++)
		d[i]=frame->data[i];
	for (i=frame->can_dlc;i<8;i++)
		d[i]=0;
		
	printf("\r\n");
	return nbytes;
}


char GetCheckSum (int addrStart, int offset, int s, struct sockaddr *addr, struct can_frame frame)
{
  unsigned int len = addrStart+offset;
  char cks=0;
  
  printf("\r\nCalculating checksum @%4X, len %2X...",addrStart,len);
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  
  memcpy(frame.data,&frames[5],8);
  frame.data[4] = (len&0xFF00)>>8;
  frame.data[5] = len&0xFF; 
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)addr, &frame)<=0) {usleep(20000);};
  if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != 0x50) || (frame.data[1] != 0xB1) ) 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
  
  cks =  frame.data[2];
  printf("CKS received %.2X\r\n", cks);
  return cks;
}

char addcks(char ckstot, char cks)
{
  if ((ckstot + cks) > 255)
    return ckstot+cks+1;
   else
     return ckstot+cks; 
}

char JumpToAddress (int addrStart, int s, struct sockaddr *addr, struct can_frame frame)
{
  printf("Entering Jump address @%4X...",addrStart);
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
  frame.can_dlc = 8;
  memcpy(frame.data,&frames[4],8);
  frame.data[4] = (addrStart & 0xFF00)>>8;
  frame.data[5] = addrStart & 0xFF; 
  
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100000);};
  if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != 0x50) || (frame.data[1] != 0x9C) ) 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
   
  printf("Done\r\n");
  return 1;
}

int main(int argc, char **argv)
{
	int s; 
	struct sockaddr_can addr;
	struct ifreq ifr;
	struct can_frame frame;

	printf("ECU SBL Uploader Tool by Alain WEISZ\r\n");

  /*
	if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("Socket");
		return 1;
	}

	strcpy(ifr.ifr_name, "can0" );
	ioctl(s, SIOCGIFINDEX, &ifr);

	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("Bind");
		return 1;
	}
*/

  s = setupCANSock("can0", &addr);
  /*
  // Reset CEM
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  printf("Resetting ECU...");
  memcpy(frame.data,&frames[7],8); 
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100000);};
  if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != 0x50) || (frame.data[1] != 0xC8) || (frame.data[2] != 0) ) 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
  
  printf("Done\r\n");
  */
  // send

  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  
  // Mode Prog ON
  printf("Entering Programmig mode\r\n");
  memcpy(frame.data,&frames[0],8); 
  sendframe (s, frame);
  sendframe (s, frame);
  sendframe (s, frame);
  sendframe (s, frame);
  sleep(1);
  
  int silent = 0;
  
  printf("Waiting for silence ...\r\n");
  fflush(stdout);
  while ((canRead(s, (struct sockaddr *)&addr, &frame)>0) || (silent<100)) {usleep(100000);silent++;};
  
  // Unlock software upload, send PIN
  printf("Unlocking...");
  memcpy(frame.data,&frames[1],8); 
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100000);};
  if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != 0x50) || (frame.data[1] != 0xB9) || (frame.data[2] != 0) ) 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
  
  printf("Done\r\n");
  
  // Get hardware info
  printf("Getting hardware no...");
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  memcpy(frame.data,&frames[2],8); 
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100000);};
  if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != 0x50) || (frame.data[1] != 0x8E) ) 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
  
  printf("Hardware no : %.2X%.2X%.2X%.2X%.2X%.2X\r\n",frame.data[2],frame.data[3],frame.data[4],frame.data[5],frame.data[6],frame.data[7]);
  
  // Demarre le PBL
  printf("Entering into PBL...");
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  memcpy(frame.data,&frames[3],8); 
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100000);};
  if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != 0x50) || (frame.data[1] != 0xC6) ) 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
   
  printf("Done\r\n");
   
  // Defini l'adresse de jump
  printf("Entering Jump address...");
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  memcpy(frame.data,&frames[4],8); 
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100000);};
  if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != 0x50) || (frame.data[1] != 0x9C) ) 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
   
  printf("Done\r\n");
  sleep(2);
  
  
  // Envoi les donn��es
  printf("Sending SBL datas...");
  
  const char * filename = "./SBL31327215.hex";
  size_t size = 0; 
  int j=0;
  char cks=0;
  
  FILE * stream = fopen( filename, "r" );
    if ( stream == NULL ) {
        fprintf( stderr, "Cannot open file for reading\n" );
        exit( -1 );
    }
 
  frame.data[0]=0x50;
  frame.data[1]=0xAE;  
  j=2; 
  char * line = NULL;
  size_t leng = 0;
  int addrStart = 0x0000;
  int offset = 0;
  int firstTX = 1;
  
  while ( (size = getline(&line, &leng, stream)) != -1) {
        printf("Retrieved line of length %ld:\n", size);
        printf("%s", line);
        if (line[0] != ':') continue; // Skip line not begining by :
        char hex[5];
        hex[0]=line[1];
        hex[1]=line[2];
        hex[2]='\0';
        int linesize = (int)strtol(hex, NULL, 16);
        hex[0]=line[3];
        hex[1]=line[4];
        hex[2]=line[5];
        hex[3]=line[6];
        hex[4]='\0';
        int curraddr = (int)strtol(hex, NULL, 16);
        hex[0]=line[7];
        hex[1]=line[8];
        hex[2]='\0';
        int type = (int)strtol(hex, NULL, 16);
        
        // Check l'adresse
        if ((addrStart+offset != curraddr) || (linesize == 0) )
          {
            if (firstTX == 0)
              {
                // Calculate checksum
                sleep(1);
                /*
                unsigned int len = addrStart+offset;
                printf("\r\nCalculating checksum @%4X, len %2X...",addrStart,len);
                frame.can_id = CAN_DIAG_ID;
                frame.can_id |= CAN_EFF_FLAG;
              	frame.can_dlc = 8;
                
                memcpy(frame.data,&frames[5],8);
                frame.data[4] = (len&0xFF00)>>8;
                frame.data[5] = len&0xFF; 
                sendframe (s, frame);
                
                // Catch answer
                fflush(stdout);
                while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(20000);};
                if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != 0x50) || (frame.data[1] != 0xB1) ) 
                  {
                    printf("Invalid response.\r\n");
                    return 0;
                  }
                */
                char c = GetCheckSum (addrStart, offset, s, (struct sockaddr *)&addr, frame);
                printf("Expected Checksum : %2X, addrstart : %4X, len : %4X\r\n",cks, addrStart, offset);
                if (cks != c) 
                  {
                    printf("CHECKSUM ERROR !!\r\n");
                    return 1;
                  }
                cks=0;
              }
            if (linesize ==0) break; // Quit as we reached end of file
            
            printf("Setting address @%4X\r\n",curraddr);
            offset=0;
            addrStart = curraddr;
            
            // Defini l'adresse de jump
            JumpToAddress (addrStart, s, (struct sockaddr *)&addr, frame);
          }
        
        int cnt=2;
        frame.data[0]=0x50;
        frame.data[1]=0xAE;
        long int totcnt = 0;
        for (int i=0;i<(linesize*2);i=i+2)
          {
            
            totcnt++;
            
            hex[0]=line[9+i];
            hex[1]=line[10+i];
            hex[2]='\0';
            //printf("%s\r\n",hex);
            int dat = (int)strtol(hex, NULL, 16);
        
            frame.data[cnt] = dat;
            cks=addcks(cks,dat);
            cnt++;
            if ((cnt == 8) || (totcnt == 0x400))
              {
                offset+=cnt-2; 
                
                // Envoi la frame
                frame.can_id = CAN_DIAG_ID;
                frame.can_id |= CAN_EFF_FLAG;
              	frame.can_dlc = 8;
                
                // Sending datas 
                printf("->@%4X[%4X] %.2X%.2X%.2X%.2X%.2X%.2X%.2X%.2X (cks=%2X)\r\n",curraddr, offset, frame.data[0],frame.data[1],frame.data[2],frame.data[3],frame.data[4],frame.data[5],frame.data[6],frame.data[7],cks);
                sendframe (s, frame);
                usleep(200000);//100ms
                frame.data[0]=0x50;
                frame.data[1]=0xAE;
                cnt=2;
                firstTX=0; // on a déja transmis, on clear le flag car checksum à calculer
              }
          }
          
          if (cnt >2)
            {
              // Rattrapage
              offset+=cnt-2;
              frame.can_id = CAN_DIAG_ID;
              frame.can_id |= CAN_EFF_FLAG;
            	frame.can_dlc = 8;
              frame.data[0]=0x50;
              frame.data[1]=0xA8+(cnt-2);
              
              for (int i=cnt;i<8;i++)
                  frame.data[i]=0x00;
              
              // Sending datas 
              //printf("->@%4X %.2X%.2X%.2X%.2X%.2X%.2X%.2X%.2X\r\n",curraddr, frame.data[0],frame.data[1],frame.data[2],frame.data[3],frame.data[4],frame.data[5],frame.data[6],frame.data[7]);
              printf("->@%4X[%4X] %.2X%.2X%.2X%.2X%.2X%.2X%.2X%.2X\r\n",curraddr, offset, frame.data[0],frame.data[1],frame.data[2],frame.data[3],frame.data[4],frame.data[5],frame.data[6],frame.data[7]);
              sendframe (s, frame);
              usleep(100000);//10ms
              
            }
  }
  
  
  /*
  while ( ! feof( stream ) ) {
        fflush(stdout);
        size++;
        char hex[3];
        hex[0]=fgetc( stream );
        hex[1]=fgetc( stream );
        hex[2]='\0';
        int number = (int)strtol(hex, NULL, 16);
        frame.data[j]=number;
        cks += frame.data[j];
        j++;
        if (j==8)
          {
            frame.can_id = CAN_DIAG_ID;
            frame.can_id |= CAN_EFF_FLAG;
          	frame.can_dlc = 8;
            
            // Sending datas 
            printf("->%.2X%.2X%.2X%.2X%.2X%.2X%.2X%.2X\r\n",frame.data[0],frame.data[1],frame.data[2],frame.data[3],frame.data[4],frame.data[5],frame.data[6],frame.data[7]);
            sendframe (s, frame);
            usleep(100000);//100ms
            frame.data[0]=0x50;
            frame.data[1]=0xAE;
            j=2;
          }
    }
  if (j>2)
    {
      size = size + (j-1);
      for (int i=j;i<8;i++) frame.data[i]=0; // Clear unused bytes
      frame.can_id = CAN_DIAG_ID;
      frame.can_id |= CAN_EFF_FLAG;
    	frame.can_dlc = 8;
      
      // Sending datas 
      printf("->%.2X%.2X%.2X%.2X%.2X%.2X%.2X%.2X\r\n",frame.data[0],frame.data[1],frame.data[2],frame.data[3],frame.data[4],frame.data[5],frame.data[6],frame.data[7]);
      sendframe (s, frame);
    }
    */
  printf("Sent %ld(%.4lX) bytes, ckecksum %i\r\n",size,size, cks+1);
  fclose(stream);


   // Defini l'adresse de jump
  printf("Entering Jump address...");
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  memcpy(frame.data,&frames[4],8); 
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100000);};
  if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != 0x50) || (frame.data[1] != 0x9C) ) 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
   
  printf("Done\r\n");

  
   // Execute le SBL
  printf("Starting SBL...");
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  memcpy(frame.data,&frames[6],8); 
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100000);};
  if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != 0x50) || (frame.data[1] != 0xA0) ) 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
   
  printf("Done\r\n");
  sleep(2);
  
  
  exit(0);
  
  // Demande de checksum
  printf("Calculating checksum...");
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  unsigned int len = 0x1000+size;
  memcpy(frame.data,&frames[5],8);
  frame.data[3] = (len&0xFF00)>>8;
  frame.data[4] = len&0xFF; 
  sendframe (s, frame);
  
   // Catch answer
   fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100000);};
  if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != 0x50) || (frame.data[1] != 0xB1) ) 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
   
  printf("Done received %.2X, expected %.2X\r\n",frame.data[2], cks);
  
	if (close(s) < 0) {
		perror("Close");
		return 1;
	}

	return 0;
}
