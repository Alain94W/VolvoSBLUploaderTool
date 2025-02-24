// AW 23/06/2024
// Set ECU into bootloader mode then send custom SBL and give menu to interract with it
// To compile : gcc ./VolcanoSBLProg1.c -o volcanosblprog -ljansson
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

#include <jansson.h>

#define BLOCK_SIZE 8
#define CAN_DIAG_ID 0x000FFFFEU

#define NbFrames 13
const char frames[NbFrames][8]={
                         {0xFF,0x86,0x00,0x00,0x00,0x00,0x00,0x00},    // 0 Prog Mod ON
                         {0x50,0xBE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},    // 1 PIN Unlock
                         {0x50,0x88,0x00,0x00,0x00,0x00,0x00,0x00},    // 2 Get Device Info
                         {0x50,0xC0,0x00,0x00,0x00,0x00,0x00,0x00},    // 3 Start PBL
                         {0x50,0x9C,0x00,0x00,0x10,0x00,0x00,0x00},    // 4 Jump to addresse
                         {0x50,0xB4,0x00,0x00,0x00,0x00,0x00,0x00},    // 5 Get Checksum
                         {0x50,0xA0,0x00,0x00,0x00,0x00,0x00,0x00},    // 6 Response code
                         {0x50,0xC8,0x00,0x00,0x00,0x00,0x00,0x00},    // 7 ECU Reset
                         {0x50,0xD0,0x00,0x00,0x00,0x00,0x00,0x00},    // 8 Dump memory
                         {0x50,0xF8,0x00,0x00,0x00,0x00,0x00,0x00},    // 9 ATTENTION, EFFACAGE
                         {0x50,0xDA,0x00,0x00,0x00,0x00,0x00,0x00},    // 10 Program Flash Word
                         {0xFF,0xC8,0x00,0x00,0x00,0x00,0x00,0x00},    // 11 Prog Mode Off
                         {0x50,0xCC,0x00,0x00,0x00,0x00,0x00,0x00},    // 12 Configuring the SBL
                         };    

typedef struct tsDictionnaryItem tDictionnaryItem;
struct tsDictionnaryItem
{
    int ecu_id;
    int startAddr;
    int osc_speed;
    int useChecksumFrame;
    char sbl_filename[80];
    int partid;
    char deriative[30];
};

tDictionnaryItem DictionnaryItem;

typedef struct tsFlashConfigItem tFlashConfigItem;
struct tsFlashConfigItem
{
    char ppage_start;
    int ppage_size;
    int ppage_quantity;
    int addrStart;
};


typedef struct tsChipConfig tChipConfig;
struct tsChipConfig
{
    int flash_itemCount;
    int flash_itemIndex;
    int eeprom_itemCount;
    int eeprom_itemIndex;
    long int flash_totalSize;
    long int eeprom_totalSize;
    tFlashConfigItem current_flashItem;
    tFlashConfigItem current_eepromItem;
};

tChipConfig ChipConfig;

char debug = 0;


int sendframe (int can_sock, struct can_frame frame)
{
  
	//sprintf(frame.data, "Hello");
	if (write(can_sock, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
		perror("Write");
		return 1;
	}
 return 0;
}

void setFilter(int can_sock, int id, int mask)
{
  if (id == -1)
    {
      //
      setsockopt(can_sock, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);
    }
  else
    {
      struct can_filter rfilter[1];

      rfilter[0].can_id   = id;
      rfilter[0].can_mask = mask;
      
      setsockopt(can_sock, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));
    }
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

	memset(addr, 0, sizeof(*addr));

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
	                if (debug) printf("Overflow !! (%d)\r\n", dropcnt);
	              }
					
			}
	rxcnt++;	
  frame->can_id &= ~CAN_EFF_FLAG;
	if (debug==1)  printf("%lu.%6lu : 0x%08X [%d] ",tv.tv_sec, tv.tv_usec,frame->can_id, frame->can_dlc);

  if (debug==1)
    { 
	    for (i = 0; i < frame->can_dlc; i++)
		    printf("%02X ",frame->data[i]);
    }
/*
	int d[9];
	for (i=0; i<frame->can_dlc;i++)
		d[i]=frame->data[i];
	for (i=frame->can_dlc;i<8;i++)
		d[i]=0;
*/		
	if (debug==1) printf("\r\n");
	return nbytes;
}

struct can_frame TransmitAndReceive(int ExpectedCmd, int ECU_id, int s, struct sockaddr *addr, struct can_frame frame)
{
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;

  frame.data[0] = ECU_id; 
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(1000);};
  if ( (frame.can_dlc != 8) || (frame.data[0] != ECU_id) || (frame.data[1] != ExpectedCmd) ) //(frame.can_id != 3) ||
    {
      printf("Invalid response.\r\n");
      for (int i = 0; i < frame.can_dlc; i++)
		    printf("%02X ",frame.data[i]);
	 printf("\r\n");
      return frame;
    }
   
  printf("Done\r\n");
  return frame;
}


void SelectChipFlashProfile(char *ProfileName, int index)
{
  json_t * jsIn;
  json_error_t error;
  
  char indexchr[10];
  sprintf(indexchr, "%d", index);
  
  printf("Selecting flash index %s\r\n",indexchr);
  
  jsIn = json_load_file(ProfileName, 'r', &error);
  
  json_t *flash_config = json_object_get(jsIn, "flash_config");
  json_t *flash_count  = json_object_get(flash_config, "count");
  ChipConfig.flash_itemCount = json_integer_value(flash_count);
  json_t *flash_item  = json_object_get(flash_config, indexchr);
  
  json_t *flash_pstart = json_object_get(flash_item, "page_start");
  json_t *flash_psize = json_object_get(flash_item, "page_size");
  json_t *flash_pqtt = json_object_get(flash_item, "page_qtt");
  json_t *flash_addrStart = json_object_get(flash_item, "addrstrt");
  
  
  ChipConfig.current_flashItem.ppage_start = (int)strtol(json_string_value(flash_pstart), NULL, 16);
  ChipConfig.current_flashItem.ppage_size = (int)strtol(json_string_value(flash_psize), NULL, 16);
  ChipConfig.current_flashItem.ppage_quantity = json_integer_value(flash_pqtt);
  ChipConfig.current_flashItem.addrStart = (int)strtol(json_string_value(flash_addrStart), NULL, 16);
  
  printf("- Page start : 0x%02X\r\n - Page start addr : %04X\r\n- Page size : 0x%04X\r\n - Page quantity : %d\r\n",ChipConfig.current_flashItem.ppage_start, ChipConfig.current_flashItem.addrStart, ChipConfig.current_flashItem.ppage_size, ChipConfig.current_flashItem.ppage_quantity);
  

  json_decref(jsIn);
}

void SelectChipEEPROMProfile(char *ProfileName, int index)
{
  json_t * jsIn;
  json_error_t error;
  
  int length = snprintf( NULL, 0, "%d", index );
  char* indexchr = malloc( length + 1 );
  snprintf( indexchr, length + 1, "%d", index );
  
  printf("Selecting eeprom index %s\r\n",indexchr);
  
  jsIn = json_load_file(ProfileName, 'r', &error);
  
  json_t *eeprom_config = json_object_get(jsIn, "eeprom_config");
  json_t *eeprom_count  = json_object_get(eeprom_config, "count");
  ChipConfig.eeprom_itemCount = json_integer_value(eeprom_count);
  json_t *eeprom_item  = json_object_get(eeprom_config, indexchr);
  
  json_t *eeprom_pstart = json_object_get(eeprom_item, "page_start");
  json_t *eeprom_psize = json_object_get(eeprom_item, "page_size");
  json_t *eeprom_pqtt = json_object_get(eeprom_item, "page_qtt");
  json_t *eeprom_addrStart = json_object_get(eeprom_item, "addrstrt");
  
  
  ChipConfig.current_eepromItem.ppage_start = (int)strtol(json_string_value(eeprom_pstart), NULL, 16);
  ChipConfig.current_eepromItem.ppage_size = (int)strtol(json_string_value(eeprom_psize), NULL, 16);
  ChipConfig.current_eepromItem.ppage_quantity = json_integer_value(eeprom_pqtt);
  ChipConfig.current_eepromItem.addrStart = (int)strtol(json_string_value(eeprom_addrStart), NULL, 16);
  
  printf("- Page start : 0x%02X\r\n - Page start addr : %04X\r\n- Page size : 0x%04X\r\n - Page quantity : %d\r\n",ChipConfig.current_eepromItem.ppage_start, ChipConfig.current_eepromItem.addrStart, ChipConfig.current_eepromItem.ppage_size, ChipConfig.current_eepromItem.ppage_quantity);
  

  json_decref(jsIn);
  free(indexchr);
}

/*
  AW 08/07/2024
    Read Chip Profile from file
*/
void ReadChipProfile(char *ProfileName)
{
  json_t * jsIn;
  json_error_t error;
  
  
  jsIn = json_load_file(ProfileName, 'r', &error);
  
  json_t *device = json_object_get(jsIn, "device");
  
  json_t *flash_config = json_object_get(jsIn, "flash_config");
  json_t *eeprom_config = json_object_get(jsIn, "eeprom_config");
  json_t *flash_count  = json_object_get(flash_config, "count");
  json_t *flash_totsize= json_object_get(flash_config, "total_size");
  json_t *eep_count  = json_object_get(eeprom_config, "count");
  json_t *eep_totsize= json_object_get(eeprom_config, "total_size");
  ChipConfig.flash_itemCount = json_integer_value(flash_count);
  ChipConfig.flash_totalSize = json_integer_value(flash_totsize);
  ChipConfig.eeprom_itemCount = json_integer_value(eep_count);
  ChipConfig.eeprom_totalSize = json_integer_value(eep_totsize);
  
  SelectChipFlashProfile(ProfileName, 1);
  SelectChipEEPROMProfile(ProfileName, 1);
  printf("Device %s Config : \r\n - Page start : 0x%02X\r\n - Page size : 0x%04X\r\n - Page quantity : %d\r\n",json_string_value(device), ChipConfig.current_flashItem.ppage_start, ChipConfig.current_flashItem.ppage_size, ChipConfig.current_flashItem.ppage_quantity);
  
  json_decref(jsIn);
}


/*
  AW 10/08/2024
    Get SBL filename and chip info from Dictionnary file
*/
void GetSBLInfoFromHwNo(char *HardwareModel, int ECU_id)
{
  json_t * jsIn;
  json_error_t error;
  
  
  jsIn = json_load_file("Dictionnary.json", 'r', &error);
  json_t *items = json_object_get(jsIn, "profiles");

  size_t index;
  json_t *value;
  json_array_foreach(items, index, value) 
    {
      /* block of code that uses index and value */
      json_t *js_hw_no = json_object_get(value, "ecu_hw");
      const char *hw_no = json_string_value(js_hw_no); 
      json_t *ecu_id    = json_object_get(value, "ecu_id");
      printf("\r\nAnalyzing model %s, looking for %s", hw_no, HardwareModel);
      if ((strcmp(hw_no,HardwareModel) == 0) && (strtol(json_string_value(ecu_id),NULL,16) == ECU_id))
        {
          // Trouv������
          printf(" Found\r\n");
          
          json_t *startAddr       = json_object_get(value, "startAddr");
          json_t *osc_speed       = json_object_get(value, "osc_speed");
          json_t *sbl_filename    = json_object_get(value, "sbl_filename");
          json_t *useChecksumF    = json_object_get(value, "useChecksumFrame");
          json_t *partid          = json_object_get(value, "partid");
          json_t *deriative       = json_object_get(value, "deriative");
          
          int len = strlen(json_string_value(sbl_filename));
         // DictionnaryItem.sbl_filename  = malloc( len + 1 );
          memcpy(DictionnaryItem.sbl_filename, json_string_value(sbl_filename),len+1);
 
          DictionnaryItem.ecu_id            = strtol(json_string_value(ecu_id),NULL,16);
          DictionnaryItem.startAddr         = strtol(json_string_value(startAddr),NULL,16);
          DictionnaryItem.osc_speed         = json_integer_value(osc_speed);
          DictionnaryItem.useChecksumFrame  = json_integer_value(useChecksumF);
          
          DictionnaryItem.partid            = strtol(json_string_value(partid),NULL,16);
          len = strlen(json_string_value(deriative));
          //DictionnaryItem.deriative  = malloc( len + 1 );
          if (len<30)
            memcpy(DictionnaryItem.deriative, json_string_value(deriative),len+1);
          
          
          json_decref(jsIn);
          return;
        }
    }
  json_decref(jsIn);
  return;
}

/*
  AW 01/07/2024
    Send Get Checksum Command over the CAN BUS
    addrStart = Start address of the code sent.
    offset    = size of the sent code (number of bytes)
    s         = CAN socket number
    addr      = pointer on sockaddr structure
    can_frame = CAN frame structure to be send (data, dlc extended etc...)
*/
char GetCheckSum (int ECU_id, int addrStart, int offset, int s, struct sockaddr *addr, struct can_frame frame)
{
  unsigned int len = addrStart+offset;
  char cks=0;
  
  printf("\r\nCalculating checksum @%4X, len %2X...",addrStart,len);
  // Extended CAN ID TODO : Handle non extended 11 bit ID
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  
  memcpy(frame.data,&frames[5],8);
  frame.data[0] = ECU_id,
  frame.data[4] = (len&0xFF00)>>8;
  frame.data[5] = len&0xFF; 
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)addr, &frame)<=0) {usleep(20000);};
  if ( (frame.can_dlc != 8) || (frame.data[0] != ECU_id) || (frame.data[1] != 0xB1) ) //(frame.can_id != 3) ||
    {
      printf("Invalid response.\r\n");
      return 0;
    }
  
  cks =  frame.data[2];
  printf("CKS received %.2X\r\n", cks);
  return cks;
}


/*
  AW 02/07/2024
    Function to calculate the checksum of data sent over the CAN bus
    ckstot    = previous calculated checksum
    cks       = data to compute
    return    = New calculated checksum
*/
char addcks(char ckstot, char cks)
{
  if ((ckstot + cks) > 255)
    return ckstot+cks+1;
   else
     return ckstot+cks; 
}


/*
  AW 01/07/2024
    Send Jump to address command Function over CAN Bus 
    addrStart = Start address is the begining pointer where the code will start to be stored.
    s         = CAN socket number
    addr      = pointer on sockaddr structure
    can_frame = CAN frame structure to be send (data, dlc extended etc...)
*/
char JumpToAddress (int ECU_id, int addrStart, int s, struct sockaddr *addr, struct can_frame frame)
{
  if (debug) printf("Entering Jump address @%4X...",addrStart);
  // ToDo : Handle 11bits CAN ID
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
  frame.can_dlc = 8;
  memcpy(frame.data,&frames[4],8);
  frame.data[0] = ECU_id;
  frame.data[4] = (addrStart & 0xFF00)>>8;
  frame.data[5] = addrStart & 0xFF; 
  
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(10000);};
  if ((frame.can_dlc != 8) || (frame.data[0] != ECU_id) || (frame.data[1] != 0x9C) ) //(frame.can_id != 3) || 
    {
      printf("Invalid response or jump address.\r\n");
      return 0;
    }
   
  if (debug) printf("Done\r\n");
  return 1;
}

/*
  AW 10/07/2024
    Send Erase Flash Sector command Function over CAN Bus 
    Type      = 0=FLASH
    addrStart = Start address including Page number like E08000.
    s         = CAN socket number
    addr      = pointer on sockaddr structure
    can_frame = CAN frame structure to be send (data, dlc extended etc...)
*/
int EraseFlashSector (int ECU_id, char Type, long int addrstart, int s, struct sockaddr *addr, struct can_frame frame)
{
  printf("Erase Flash sector at @%4lX...\r",addrstart);
  // ToDo : Handle 11bits CAN ID
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
  frame.can_dlc = 8;
  memcpy(frame.data,&frames[9],8);
  frame.data[0] = ECU_id;
  frame.data[2] = (Type); // FLASH or EEPROM PAGE setting 
  frame.data[3] = (addrstart & 0xFF0000)>>16;
  frame.data[4] = (addrstart & 0xFF00)>>8;
  frame.data[5] = addrstart & 0xFF;
  
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100);};
  if ((frame.can_dlc != 8) || (frame.data[0] != ECU_id) || (frame.data[1] != 0xF9) ) //(frame.can_id != 3) || 
    {
      printf("Invalid response.\r\n");
      return -1;
    }
   
  if (debug) printf("Done\r\n");
  return frame.data[2]; // Resultat de l'op������������������ration
}

char ProgramMemoryWord (int ECU_id, char Type,int addrStart, int data, int s, struct sockaddr *addr, struct can_frame frame)
{
  printf("Flashing address @%4X...\r",addrStart);
  // ToDo : Handle 11bits CAN ID
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
  frame.can_dlc = 8;
  memcpy(frame.data,&frames[10],8);
  frame.data[0] = ECU_id;
  frame.data[2] = Type; // FLASH (0), EEPROM (1)
  frame.data[3] = (addrStart & 0xFF0000)>>16;
  frame.data[4] = (addrStart & 0xFF00)>>8;
  frame.data[5] = addrStart & 0xFF; 
  frame.data[6] = (data & 0xFF00)>>8;
  frame.data[7] = data & 0xFF; 
  
  sendframe (s, frame);
  
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100);};
  if ( (frame.can_dlc != 8) || (frame.data[0] != ECU_id) || (frame.data[1] != 0xDB) ) //(frame.can_id != 3) ||
    {
      printf("Invalid response.\r\n");
      return -1;
    } 
  if (debug) printf("Done\r\n");
    
  return frame.data[2];
}

/*
  AW 10/08/2024
    Unlock ECU with PIN Code 
    PIN      = PIN Code
    ECU_Id   = ECU Id to talk to.
    s         = CAN socket number
    addr      = pointer on sockaddr structure
    can_frame = CAN frame structure to be send (data, dlc extended etc...)
*/
char UnlockECU ( char * PIN, int ECU_id, int s, struct sockaddr *addr, struct can_frame frame)
{

  // Unlock software upload, send PIN
  printf("Unlocking...");
  
  memcpy(frame.data,&frames[1],8); 
  frame.data[2] = PIN[0];
  frame.data[3] = PIN[1];
  frame.data[4] = PIN[2];
  frame.data[5] = PIN[3];
  frame.data[6] = PIN[4];
  frame.data[7] = PIN[5];
  
  frame = TransmitAndReceive(0xB9, ECU_id, s, (struct sockaddr *)&addr, frame);
  
  /*
  // Catch answer
  fflush(stdout);
  while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(100000);};
  if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != ECU_id) || (frame.data[1] != 0xB9) || (frame.data[2] != 0) ) 
  */
  if (frame.data[2] != 0)
    {
      printf("Invalid response.\r\n");
      return 0;
    }
  
  printf("Done\r\n");
    
  return frame.data[2];
}

/*
  AW 10/08/2024
    Get ECU Informations 
    ECU_Id    = ECU Id to talk to.
    s         = CAN socket number
    addr      = pointer on sockaddr structure
    can_frame = CAN frame structure to be send (data, dlc extended etc...)
*/
char GetDeviceInfo (int ECU_id, int s, struct sockaddr *addr, struct can_frame frame, char * deviceNo)
{

  // Get hardware info
  printf("Getting hardware no...");
  memcpy(frame.data,&frames[2],8); 
  frame = TransmitAndReceive(0x8E, ECU_id, s, (struct sockaddr *)&addr, frame);
  
  if (  (frame.data[1] != 0x8E) ) // (frame.can_id != 3) || 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
  
  sprintf(deviceNo,"%.2X%.2X%.2X%.2X%.2X%.2X",frame.data[2],frame.data[3],frame.data[4],frame.data[5],frame.data[6],frame.data[7]);
  printf("Hardware no : %.2X%.2X%.2X%.2X%.2X%.2X\r\n",frame.data[2],frame.data[3],frame.data[4],frame.data[5],frame.data[6],frame.data[7]);
    
  return 1;
}

/*
  AW 10/08/2024
    Start Primary Boot Loader Mode
    ECU_Id    = ECU Id to talk to.
    s         = CAN socket number
    addr      = pointer on sockaddr structure
    can_frame = CAN frame structure to be send (data, dlc extended etc...)
*/
char StartPBL (int ECU_id, int s, struct sockaddr *addr, struct can_frame frame)
{

  // Demarre le PBL
  printf("Entering into PBL...");
  memcpy(frame.data,&frames[3],8); 
  frame.data[0] = ECU_id;
  frame = TransmitAndReceive(0xC6, ECU_id, s, (struct sockaddr *)&addr, frame);
  
  // Catch answer
  fflush(stdout);
  if ((frame.can_dlc != 8) || (frame.data[0] != ECU_id) || (frame.data[1] != 0xC6) ) //(frame.can_id != 3) || 
    {
      printf("Invalid response.\r\n");
      return 0;
    }
   
  printf("Done\r\n");
    
  return 1;
}

/*
  AW 10/08/2024
    Start Secondary Boot Loader Mode
    ECU_Id    = ECU Id to talk to.
    s         = CAN socket number
    addr      = pointer on sockaddr structure
    can_frame = CAN frame structure to be send (data, dlc extended etc...)
*/
char StartSBL (int ECU_id, int s, struct sockaddr *addr, struct can_frame frame)
{

  // Demarre le PBL
  printf("Entering into SBL...");
  memcpy(frame.data,&frames[6],8);
  frame.data[0] = ECU_id; 
  frame = TransmitAndReceive(0xA0, ECU_id, s, (struct sockaddr *)&addr, frame);
  if (frame.data[1] != 0xA0)
    {
      printf("Invalid response.\r\n");
      return 0;
    }
   
  printf("Done\r\n");
    
  return 1;
}

/*
  AW 10/08/2024
    Upload Secondary Boot Loader File
    Filenmae  = Filenmae of the boot loader file
    useCheckSum=Do a checksum check after each block sent (not working on devices without PIN code)
    ECU_Id    = ECU Id to talk to.
    s         = CAN socket number
    addr      = pointer on sockaddr structure
    can_frame = CAN frame structure to be send (data, dlc extended etc...)
*/
int UploadSBLData(const char *filename, int useCheckSum, int ECU_id, int s, struct sockaddr *addr, struct can_frame frame)
{
  printf("Sending SBL datas...");
  
  size_t size = 0; 
  char cks=0;
  
  FILE * stream = fopen( filename, "r" );
    if ( stream == NULL ) {
        fprintf( stderr, "Cannot open file for reading\n" );
        exit( -1 );
    }
 
  frame.data[0] = ECU_id;
  frame.data[1] = 0xAE;  
  //int j=2; 
  char * line = NULL;
  size_t leng = 0;
  int addrStart = 0x0000;
  int offset = 0;
  int firstTX = 1;
  int RunAddr = 0;
  int tx_counter=0;
  
  while ( (size = getline(&line, &leng, stream)) != -1) {
        if (debug) printf("Retrieved line of length %ld:\n", size);
        if (debug) printf("%s", line);
        else printf("Sending %d bytes\r",tx_counter);
        
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
        //int type = (int)strtol(hex, NULL, 16);
        
        // Check l'adresse
          {
            if (firstTX == 0)
              {
                // Calculate checksum
                // 00000003   [8]  50 A9 02 00 00 00 00 00 Si out of address range
                if (useCheckSum == 1)
                {
                  char c = GetCheckSum (ECU_id, addrStart, offset, s, (struct sockaddr *)&addr, frame);
                  printf("Expected Checksum : %2X, addrstart : %4X, len : %4X\r\n",cks, addrStart, offset);
                  if (cks != c) 
                    {
                      printf("CHECKSUM ERROR !!\r\n");
                      return 0;
                    }
                }
                cks=0;
              }
            if (linesize ==0) break; // Quit as we reached end of file
            
            if (debug) printf("Setting address @%4X\r\n",curraddr);
            offset=0;
            addrStart = curraddr;
            
            // Defini l'adresse de jump
            JumpToAddress (ECU_id, addrStart, s, (struct sockaddr *)&addr, frame);
            if (firstTX == 1) 
              RunAddr = addrStart; // To return the start address
          }
        
        int cnt=2;
        frame.data[0] = ECU_id;
        frame.data[1] = 0xAE;
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
                if (debug) 
                  printf("->@%4X[%4X] %.2X%.2X%.2X%.2X%.2X%.2X%.2X%.2X (cks=%2X)\r\n",curraddr, offset, frame.data[0],frame.data[1],frame.data[2],frame.data[3],frame.data[4],frame.data[5],frame.data[6],frame.data[7],cks);
                sendframe (s, frame);
                usleep(10000);//5ms
                frame.data[0] = ECU_id;
                frame.data[1] = 0xAE;
                cnt=2;
                firstTX=0; // on a d������������������ja transmis, on clear le flag car checksum ������������������ calculer
                tx_counter += totcnt;
              }
          }
          
          if (cnt >2)
            {
              // Rattrapage
              offset+=cnt-2;
              frame.can_id = CAN_DIAG_ID;
              frame.can_id |= CAN_EFF_FLAG;
            	frame.can_dlc = 8;
              frame.data[0] = ECU_id;
              frame.data[1] = 0xA8+(cnt-2);
              
              for (int i=cnt;i<8;i++)
                  frame.data[i]=0x00;
              
              // Sending datas 
              if (debug) printf("->@%4X[%4X] %.2X%.2X%.2X%.2X%.2X%.2X%.2X%.2X\r\n",curraddr, offset, frame.data[0],frame.data[1],frame.data[2],frame.data[3],frame.data[4],frame.data[5],frame.data[6],frame.data[7]);
              sendframe (s, frame);
              usleep(10000);//1ms
              tx_counter += totcnt; 
            }
  }
  free(line);
  printf("\r\nSent %ld(%.4lX) bytes, ckecksum %i\r\n",size,size, cks+1);
  fclose(stream);
  
  return RunAddr;
}

int WarningAnswer()
{
  int choice=0;
  while ((choice!=1) && (choice !=9))
    {
      printf("\r\n\r\n /!\\ CAUTION /!\\ BEFORE DOING THIS BE SURE TO HAVE A BACKUP, THE DATA WILL BE ERASED FIRST FROM THE CHIP\r\n");
      printf("Are you sure to continue 1->Yes, 9->Cancel ?");
      scanf("%d", &choice);
    }
  return choice;
}

// AW08/07/2024
// Dump any page selected from EEPROM(1) or FLASH(0)
// return data readed in buffout
// return checksum ok (1) or not ok (0) as function result
char DumpByPage(char Type, long int addrstart, int len, int s, int ECU_id, struct sockaddr *addr, struct can_frame frame, char *buffout)
{ 

  unsigned char buff[len];
  
  // Execute Read command
  printf("Start reading Flash...\r\n");
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  memcpy(frame.data,&frames[8],8); 
  frame.data[0] = ECU_id;
  frame.data[2] = (Type); // FLASH or EEPROM PAGE setting
  //frame.data[2] = (addrstart & 0xFF000000)>>24;
  frame.data[3] = (addrstart & 0xFF0000)>>16;
  frame.data[4] = (addrstart & 0xFF00)>>8;
  frame.data[5] = addrstart & 0xFF;
  frame.data[6] = (len & 0xFF00)>>8;
  frame.data[7] = len & 0xFF;
  
  sendframe (s, frame);
  
  int x=0;
  char cks = 0;
  while (x<len)
    {
      // Catch answer
      fflush(stdout);
      while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(10);};
      if ((frame.can_dlc != 8) || ((frame.data[0]&0xA0) != 0xA0) ) //(frame.can_id != 3) || 
        {
          printf("Invalid response.\r\n");
          return 0;
        }
        
      char rxlen = frame.data[0]-0xA8;
      int rxPacketNo = (frame.data[1]<<8)|(frame.data[2]);
      if (debug==1) printf("RXlen : %d\r\n",rxlen);
      for (unsigned char k=3;k<(3+rxlen);k++)
        {
          buff[((rxPacketNo)*5)+(k-3)]= frame.data[k];
          x++;
          cks += frame.data[k];
          if (debug==1) printf("%02X",frame.data[k]);
        }
      if (debug==1) printf("x\\len : %04X\\%04X\r\n",x,len);
      if (debug==0) printf(" <- RX %d/%d\r", x,len); // Progression
    }
  if (debug==1) printf("Done \r\n"); else printf("\r\n");
   if (debug==1)
  for (x=0;x<len;x++)
    {
      printf("%02X ",buff[x]);
      if (((x+1) % (16)==0) && (x>0)) printf("\r\n");
    }
  if (debug==1) printf("\r\n Expected cks = %02X\r\n", cks);
  char c = GetCheckSum (ECU_id, addrstart, len, s, (struct sockaddr *)&addr, frame);
  if (debug==1) printf(" Received CKS = %02X\r\n",c);
  if (cks == c)
    {
      memcpy(buffout,buff,len);
      return 1;
    }
  else
    return 0;
} 


int SBLMenu (int ECU_id, int s, struct sockaddr *addr, struct can_frame frame)
{
  int exout = 0;
  while (exout == 0)
    {
      printf("\r\n====== SBL Menu ======\r\n");
      printf("1  - Dump Flash by Program Page and address to a file\r\n");
      printf("2  - Dump EEPROM by EEPROM Page and address to a file\r\n");
      printf("3  - Dump ALL the Flash to a file\r\n");
      printf("4  - Dump ALL the EEPROM to a file\r\n");
      printf("5  - Erase and Program Flash Page from file\r\n");
      printf("6  - Erase Flash sectors\r\n");
      printf("7  - Erase and Program EEPROM Page from file\r\n");
      printf("8  - Erase and Program EEPROM Words to Page\r\n");
      printf("9  - Erase and Program FULL FLASH from file\r\n");
      printf("10 - Erase and Program FULL EEPROM from file\r\n");
      printf("11 - Read register\r\n");
      printf("12 - Write register\r\n");
      printf("19 - Exit SBL Menu\r\n");
      printf("\r\nYour choice is ? ");
      int MenuSelected=0;
      scanf("%d", &MenuSelected);
      
      if (MenuSelected == 19)
        {
          exout=1;
          //break;
          return 0;
        }
      if ((MenuSelected == 1) || (MenuSelected == 2) ) 
        {
          //
          // Read Flash and store to file
          printf("Address start (hexa, ex: E08000): ");
          long int addrstart =0;
          scanf("%lX", &addrstart);
          printf("Addr is : %06lX\r\n",addrstart);
          printf("Number of byte to read (hexa, ex 4000): ");
          int len =0;
          scanf("%X", &len);
          printf("Reading size : %04X\r\n",len); 
          
          char buff[len];
          
          // Dump the data to the buff
          char c = DumpByPage(MenuSelected-1, addrstart, len, s, ECU_id, addr, frame, buff);
          
        if (c==1)
            {
              char filename[25] ={0,};
              sprintf(filename, "%s_%04lX_%02X.bin","DUMP",addrstart,len);
              printf("Writing to file [%s]...\r\n",filename);
              FILE *write_ptr;
              write_ptr = fopen(filename,"wb");  // w for write, b for binary
              fwrite(buff,len,1,write_ptr); // write 10 bytes from our buffer
              fclose(write_ptr);
            }
        }
        if (MenuSelected == 3)
          {
            /*
            // Load config file for the device
            char filename[25];
            printf("Enter the local device filename to use : ");
            scanf("%s", filename);
            printf("File is : %s\r\n",filename);
            */
            printf("Using deriative file %s\r\n",DictionnaryItem.deriative);
            
            if (access(DictionnaryItem.deriative, F_OK) == 0) 
              {
                  // file exists
                  ReadChipProfile(DictionnaryItem.deriative);
                  unsigned long int totalsize = ChipConfig.flash_totalSize;
                  
                  printf("total size : %ld Ko\r\n", (totalsize/1024));
                  
                  char buffTotal[totalsize];
                  int pagenumber = 0;
                  for (int k=1;k<=ChipConfig.flash_itemCount;k++)
                    {
                      // Update ChipConfig with current item k
                      SelectChipFlashProfile(DictionnaryItem.deriative, k);
                       for (int i=0; i<ChipConfig.current_flashItem.ppage_quantity;i++)
                        {
                          // Pour chaque page
                          long int addrstart =((ChipConfig.current_flashItem.ppage_start+i)*65536)+ChipConfig.current_flashItem.addrStart;
                          int len = ChipConfig.current_flashItem.ppage_size;
                          
                          
                          printf("%02i - Dumping address %06lX, of len %04X\r\n", pagenumber, addrstart, len);
                          char buff[len];
                          char c = DumpByPage(0, addrstart, len, s, ECU_id, addr, frame, buff);
                          if (c==1)
                            {
                              // Transfert le buf temp dans le buff final
                              memcpy(&buffTotal[(pagenumber*len)], &buff, len);
                            } 
                          pagenumber++;
                        }
                    }
                    
                  // Writing to output file
                  char filename[25] ={0,};
                  sprintf(filename, "FULLFLASH.bin");
                  printf("Writing to file [%s]...\r\n",filename);
                  FILE *write_ptr;
                  write_ptr = fopen(filename,"wb");  // w for write, b for binary
                  fwrite(buffTotal,totalsize,1,write_ptr); // write 10 bytes from our buffer
                  fclose(write_ptr);
                  
              } else {
                 printf("The file isn't present in local folder.\r\n");
              }
          }
        if (MenuSelected == 4)
          {
            /*
            // Load config file for the device
            char filename[25];
            printf("Enter the local device filename to use : ");
            scanf("%s", filename);
            printf("File is : %s\r\n",filename);
            */
            printf("Using deriative file %s\r\n",DictionnaryItem.deriative);
            
            if (access(DictionnaryItem.deriative, F_OK) == 0) 
              {
                  // file exists
                  ReadChipProfile(DictionnaryItem.deriative);
                  unsigned long int totalsize = ChipConfig.eeprom_totalSize;
                  
                  printf("total size : %ld Ko\r\n", (totalsize/1024));
                  
                  char buffTotal[totalsize];
                  int pagenumber = 0;
                  for (int k=1;k<=ChipConfig.eeprom_itemCount;k++)
                    {
                      // Update ChipConfig with current item k
                      SelectChipEEPROMProfile(DictionnaryItem.deriative, k);
                       for (int i=0; i<ChipConfig.current_eepromItem.ppage_quantity;i++)
                        {
                          // Pour chaque page
                          long int addrstart =((ChipConfig.current_eepromItem.ppage_start+i)*65536)+ChipConfig.current_eepromItem.addrStart;
                          int len = ChipConfig.current_eepromItem.ppage_size;
                          
                          
                          printf("%02i - Dumping address %06lX, of len %04X\r\n", pagenumber, addrstart, len);
                          char buff[len];
                          char c = DumpByPage(1, addrstart, len, s, ECU_id, addr, frame, buff);
                          if (c==1)
                            {
                              // Transfert le buf temp dans le buff final
                              memcpy(&buffTotal[(pagenumber*len)], &buff, len);
                            } 
                          pagenumber++;
                        }
                    }
                    
                  // Writing to output file
                  char filename[25] ={0,};
                  sprintf(filename, "FULLEEPROM.bin");
                  printf("Writing to file [%s]...\r\n",filename);
                  FILE *write_ptr;
                  write_ptr = fopen(filename,"wb");  // w for write, b for binary
                  fwrite(buffTotal,totalsize,1,write_ptr); 
                  fclose(write_ptr);
                  
              } else {
                 printf("The file isn't present in local folder.\r\n");
              }
          }
          else if (MenuSelected == 5)
            {
              // Erase and program flash page frome file
              int choice = WarningAnswer();
              if (choice == 9) continue;
              printf("Enter the adresse from the sector to be erased, sector size is 0x400 bytes (ex : E08000) :");
              long int addrstart =0;
              scanf("%lX", &addrstart);
              printf("Addr is : %06lX\r\n",addrstart);
              
              
              
              char filename[250];
              printf("Enter the local FLASH filename to use : ");
              scanf("%s", filename);
              printf("File is : %s\r\n",filename);
              if (access(filename, F_OK) == 0) 
                {
                  FILE *read_ptr;
                  read_ptr = fopen(filename,"rb");  // w for write, b for binary
                  
                  // Get file size
                  fseek(read_ptr, 0L, SEEK_END);
                  size_t sz = ftell(read_ptr);
                  rewind(read_ptr);

                  // calculate number of sector to erase
                  int sectors = sz/0x400;
                  
                  printf("Will now erase %d sectors of size %04X\r\n",sectors,0x400);
                  for (int i=0;i<sectors;i++)
                    {
                      int eraseres =  EraseFlashSector (ECU_id, 0, addrstart+(i*0x400), s, addr, frame);
                      //int eraseres= 0;
                      printf("Erase Result of sector %06lX is %02X\r",addrstart+(i*0x400),eraseres);
                      if (eraseres != 0) printf("\r\n");
                    }
                  
                  printf("\r\n");
                  
                  long int addrfinal = (addrstart+sz)-1;
                  while ( addrstart < addrfinal ) // ! feof( read_ptr ) 
                    {
                      char d1= fgetc( read_ptr );
                      char d0= fgetc( read_ptr );
                      int word = (d1<<8)|d0;
                      
                      char retcode = ProgramMemoryWord (ECU_id, 0, addrstart, word, s, addr, frame);
                      printf(" Flashing file %s [%04lX/%04lX:%04X] %02X\r",filename, addrstart,addrfinal,word,retcode);
                      if (retcode != 0) 
                        {
                          printf("\r\n Flash failed !!! \r\n");
                          if (retcode == 2) printf("Flash array of destination is not empty, it must be erased first.\r\n");
                          if (retcode == 1) printf("Flash address of destination is not Even.\r\n");
                          if (retcode == 6) printf("A command is already waiting to be done.\r\n");
                          if (retcode == 3) printf("PVIOL bit is set.\r\n");
                          if (retcode == 4) printf("ACCESS bit is set.\r\n");
                          if (retcode == 0x0A) printf("Wrong FLASH Page Number\r\n");
                          break;
                        }
                      addrstart = addrstart+2;
                      
                    }
                  printf("\r\n Finished\r\n");
                  
                  fclose(read_ptr);
                  
                }
              }
          else if (MenuSelected == 6)
                {
                  // Flash sector erase
                  int choice = WarningAnswer();
                  if (choice == 9) continue;
                  printf("Enter the adresse from the sector to be erased, sector size is 0x400 bytes (ex : E08000) :");
                  long int addrstart =0;
                  scanf("%lX", &addrstart);
                  printf("Addr is : %06lX\r\n",addrstart);
                  printf("Enter the number of sector to be erased, sector size is 0x400 bytes (ex : 4) :");
                  int nbsector =0;
                  scanf("%X", &nbsector);
                  printf("number of sector : %02X\r\n",nbsector);
                  
                  printf("Will now erase %d sectors of size %04X\r\n",nbsector,0x400);
                  for (int i=0;i<nbsector;i++)
                    {
                      int eraseres =  EraseFlashSector (ECU_id, 0, addrstart+(i*0x400), s, addr, frame);
                      //int eraseres= 0;
                      printf("Erase Result of sector %06lX is %02X\r",addrstart+(i*0x400),eraseres);
                      if (eraseres != 0) printf("\r\n");
                    }
                  printf("\r\n");
                }
              
          else if (MenuSelected == 7)
            {
              // Erase and program eeprom
              int choice = WarningAnswer();
              if (choice == 9) continue;
              printf("Enter the adresse from the sector to be erased, sector size is 0x04 bytes (ex : ) :");
              long int addrstart =0;
              scanf("%lX", &addrstart);
              printf("Addr is : %06lX\r\n",addrstart);
              
              
              
              char filename[250];
              printf("Enter the local EEPROM filename to use : ");
              scanf("%s", filename);
              printf("File is : %s\r\n",filename);
              if (access(filename, F_OK) == 0) 
                {
                  FILE *read_ptr;
                  read_ptr = fopen(filename,"rb");  // w for write, b for binary
                  
                  // Get file size
                  fseek(read_ptr, 0L, SEEK_END);
                  size_t sz = ftell(read_ptr);
                  rewind(read_ptr);

                  // calculate number of sector to erase
                  int sectors = sz/0x04;
                  
                  printf("Will now erase %d sectors of size %04X\r\n",sectors,0x04);
                  for (int i=0;i<sectors;i++)
                    {
                      int eraseres =  EraseFlashSector (ECU_id, 1, addrstart+(i*0x04), s, addr, frame);
                      //int eraseres= 0;
                      printf("Erase Result of sector %06lX is %02X\r",addrstart+(i*0x04),eraseres);
                    }
                  
                  long int addrfinal = (addrstart+sz)-1;
                  while ( addrstart < addrfinal ) // ! feof( read_ptr ) 
                    {
                      char d1= fgetc( read_ptr );
                      char d0= fgetc( read_ptr );
                      int word = (d1<<8)|d0;
                      
                      char retcode = ProgramMemoryWord (ECU_id, 1, addrstart, word, s, addr, frame);
                      printf(" Programming file %s [%04lX/%04lX:%04X] %02X\r",filename, addrstart,addrfinal,word,retcode);
                      if (retcode != 0) 
                        {
                          printf("\r\n EEPROM program failed !!! \r\n");
                          if (retcode == 2) printf("EEPROM array of destination is not empty, it must be erased first.\r\n");
                          if (retcode == 1) printf("EEPROM address of destination is not Even.\r\n");
                          if (retcode == 6) printf("A command is already waiting to be done.\r\n");
                          if (retcode == 3) printf("PVIOL bit is set.\r\n");
                          if (retcode == 4) printf("ACCESS bit is set.\r\n");
                          if (retcode == 0x0B) printf("Wrong EEPROM Page Number\r\n");
                          break;
                        }
                      addrstart = addrstart+2;
                      
                    }
                  printf("\r\n Finished\r\n");
                  
                  fclose(read_ptr);
                  
                }
            }    
          else if (MenuSelected == 8)
            {
              // Erase and program Word eeprom
              int choice = WarningAnswer();
              if (choice == 9) continue;
              printf("Enter the adresse from the sector to be erased, sector size is 0x04 bytes (ex : FD0800) :");
              long int addrstart =0;
              scanf("%lX", &addrstart);
              printf("Addr is : %06lX\r\n",addrstart);
              
              char next=0;
              int quadro = 4;
              while ( next == 0 )
                {
                  printf("Enter the Word to be programmed :");
                  int word =0;
                  scanf("%X", &word);
                  printf("Word is : %04X\r\n\r\n",word);
                  
                  if (quadro == 4)
                    {
                      printf("Will now erase %d sectors of size %04X\r\n",1,0x04);
                      int eraseres =  EraseFlashSector (ECU_id, 1, addrstart, s, addr, frame);
                      printf("Erase Result of sector %06lX is %02X\r",addrstart,eraseres);
                      quadro=2;
                    }
                  else quadro = 4;
                  
                  char retcode = ProgramMemoryWord (ECU_id, 1, addrstart, word, s, addr, frame);
                  printf(" Programming word %04X to @%04lX, res :  %02X\r",word,addrstart,retcode);
                  if (retcode != 0) 
                    {
                      printf("\r\n EEPROM program failed !!! \r\n");
                      if (retcode == 2) printf("EEPROM array of destination is not empty, it must be erased first.\r\n");
                      if (retcode == 1) printf("EEPROM address of destination is not Even.\r\n");
                      if (retcode == 6) printf("A command is already waiting to be done.\r\n");
                      if (retcode == 3) printf("PVIOL bit is set.\r\n");
                      if (retcode == 4) printf("ACCESS bit is set.\r\n");
                      if (retcode == 0x0B) printf("Wrong EEPROM Page Number\r\n");
                      break;
                    }
                  addrstart = addrstart+2;
                  printf("Next addresse is @%04lX\r\n",addrstart);
                  printf("Continue to next address ? 0->Yes, 9->Cancel default is 0 : ");
                  scanf("%c", &next);
                  if (next == 9) continue;
                }
              printf("\r\n Finished\r\n");
            }
          
          else if (MenuSelected == 9)
            {
              // Erase and program FULL flash
              int choice = WarningAnswer();
              if (choice == 9) continue;
              
              /*
              // Ask for deriative file to use
              char filename[25];
              printf("Enter the local device filename to use : ");
              scanf("%s", filename);
              printf("File is : %s\r\n",filename);
              */
              printf("Deriative file is : %s\r\n",DictionnaryItem.deriative);
              
              // Ask for flash binary file to write
              char flashfilename[250];
              printf("Enter the local flash filename to use : ");
              scanf("%s", flashfilename);
              printf("File is : %s\r\n",flashfilename);
              
              // Check flash filename
              if (access(flashfilename, F_OK) != 0) 
                {
                  // Wrong file
                  printf("The file isn't present in local folder.\r\n");
                  continue;
                }
              
              // Open flash file
              FILE *read_ptr;
              read_ptr = fopen(flashfilename,"rb");  // w for write, b for binary
              
              // Get flash file size
              fseek(read_ptr, 0L, SEEK_END);
              size_t sz = ftell(read_ptr);
              rewind(read_ptr);
              

              // Check deriative file name and then ...
              char retcode = 0;
              int eraseres=0;
              if (access(DictionnaryItem.deriative, F_OK) == 0) 
                {
                    // file exists
                    ReadChipProfile(DictionnaryItem.deriative);
                    unsigned long int totalsize = ChipConfig.flash_totalSize;
                    
                    printf("Device Flash size : %ld Ko\r\n", (totalsize/1024));
                    printf("Flash file loaded : %ld Ko\r\n", (sz/1024));
                    
                    if (sz != totalsize)
                      {
                        printf("Flash file loaded as not the required size for the device\r\n");
                        continue;
                      }
                    
                    int pagenumber = 0;
                    for (int k=1;k<=ChipConfig.flash_itemCount;k++)
                      {
                        // Update ChipConfig with current item k
                        SelectChipFlashProfile(DictionnaryItem.deriative, k);
                         for (int i=0; i<ChipConfig.current_flashItem.ppage_quantity;i++)
                          {
                            // Get number of sectors to erase for the page
                            int sectors = ChipConfig.current_flashItem.ppage_size / 0x400;
                            
                            // Get page start address
                            long int addrstart =((ChipConfig.current_flashItem.ppage_start+i)*65536)+ChipConfig.current_flashItem.addrStart;
                            int len = ChipConfig.current_flashItem.ppage_size;
                            
                            // Erase
                            printf("Will now erase %d sectors of size %04X starting at @%06lX\r\n",sectors,0x400, addrstart);
                            
                            len=0;
                            for (int i=0;i<sectors;i++)
                              {
                                eraseres =  EraseFlashSector (ECU_id, 0, addrstart+(i*0x400), s, addr, frame);
                                //int eraseres= 0;
                                printf("Erase Result of sector %06lX is %02X\r",addrstart+(i*0x400),eraseres);
                                if (eraseres != 0) 
                                  {
                                    printf("Error while erasing the device at address %06lX, the device is in unstable state, you still have hand on it while SBL is running.\r\n",addrstart+(i*0x400));
                                    break;
                                  }
                                len = len + 0x400;
                              }
                            printf("\r\n");
                            if (eraseres != 0) 
                              {
                                ProgramMemoryWord (ECU_id, 2, 0x106, 0x00, s, addr, frame);
                                ProgramMemoryWord (ECU_id, 2, 0x105, 0xE0, s, addr, frame);
                              }//break;
                            
                            // Flash Page
                            long int addrfinal = (addrstart+len);
                            while ( addrstart < addrfinal ) // ! feof( read_ptr ) 
                              {
                                char d1= fgetc( read_ptr );
                                char d0= fgetc( read_ptr );
                                int word = (d1<<8)|d0;
                                
                                retcode = ProgramMemoryWord (ECU_id, 0, addrstart, word, s, addr, frame);
                                printf(" Flashing file %s [%04lX/%04lX:%04X] %02X\r",DictionnaryItem.deriative, addrstart,addrfinal,word,retcode);
                                if (retcode != 0) 
                                  {
                                    printf("\r\n Flash failed !!! \r\n");
                                    if (retcode == 2) printf("Flash array of destination is not empty, it must be erased first.\r\n");
                                    if (retcode == 1) printf("Flash address of destination is not Even.\r\n");
                                    if (retcode == 6) printf("A command is already waiting to be done.\r\n");
                                    if (retcode == 3) printf("PVIOL bit is set.\r\n");
                                    if (retcode == 4) printf("ACCESS bit is set.\r\n");
                                    if (retcode == 0x0A) printf("Wrong FLASH Page Number\r\n");
                                    break;
                                  }
                                addrstart = addrstart+2;
                              }
                            
                            printf("%02i - Flashed %06lX, of len %04X\r\n", pagenumber, addrstart, len);
                            
                            pagenumber++;
                          }
                         if (eraseres != 0) break;
                         if (retcode != 0) break;
                      }
                      
                  printf("\r\n Finished\r\n");
                  fclose(read_ptr);
                  
                } else {
                   printf("The file isn't present in local folder.\r\n");
                }
          }
      else if (MenuSelected == 10)
            {
              // Erase and program FULL EEPROM
              int choice = WarningAnswer();
              if (choice == 9) continue;
              
              /*
              // Ask for deriative file to use
              char filename[25];
              printf("Enter the local DEVICE filename to use (ex:9S12XDT384.cfg): ");
              scanf("%s", filename);
              printf("File is : %s\r\n",filename);
              */
              printf("Deriative file is : %s\r\n",DictionnaryItem.deriative);
              
              // Ask for flash binary file to write
              char flashfilename[250];
              printf("Enter the local EEPROM filename to use : ");
              scanf("%s", flashfilename);
              printf("File is : %s\r\n",flashfilename);
              
              // Check EEPROM filename
              if (access(flashfilename, F_OK) != 0) 
                {
                  // Wrong file
                  printf("The file isn't present in local folder.\r\n");
                  continue;
                }
              
              // Open flash file
              FILE *read_ptr;
              read_ptr = fopen(flashfilename,"rb");  // w for write, b for binary
              
              // Get flash file size
              fseek(read_ptr, 0L, SEEK_END);
              size_t sz = ftell(read_ptr);
              rewind(read_ptr);
              

              // Check deriative file name and then ...
              char retcode = 0;
              int eraseres=0;
              if (access(DictionnaryItem.deriative, F_OK) == 0) 
                {
                    // file exists
                    ReadChipProfile(DictionnaryItem.deriative);
                    unsigned long int totalsize = ChipConfig.eeprom_totalSize;
                    
                    printf("Device EEPROM size : %ld Ko\r\n", (totalsize/1024));
                    printf("EEPROM file loaded : %ld Ko\r\n", (sz/1024));
                    
                    if (sz != totalsize)
                      {
                        printf("EEPROM file loaded doesn't have the required size for the device\r\n");
                        continue;
                      }
                    
                    int pagenumber = 0;
                    for (int k=1;k<=ChipConfig.eeprom_itemCount;k++)
                      {
                        // Update ChipConfig with current item k
                        SelectChipEEPROMProfile(DictionnaryItem.deriative, k);
                         for (int i=0; i<ChipConfig.current_eepromItem.ppage_quantity;i++)
                          {
                            // Get number of sectors to erase for the page
                            int sectors = ChipConfig.current_eepromItem.ppage_size / 0x04;
                            
                            // Get page start address
                            long int addrstart =((ChipConfig.current_eepromItem.ppage_start+i)*65536)+ChipConfig.current_eepromItem.addrStart;
                            int len = ChipConfig.current_eepromItem.ppage_size;
                            
                            // Erase
                            printf("Will now erase %d sectors of size %04X starting at @%06lX\r\n",sectors,0x04, addrstart);
                            
                            for (int i=0;i<sectors;i++)
                              {
                                eraseres =  EraseFlashSector (ECU_id, 1, addrstart+(i*0x04), s, addr, frame);
                                //int eraseres= 0;
                                printf("Erase Result of sector %06lX is %02X\r",addrstart+(i*0x04),eraseres);
                                if (eraseres != 0) 
                                  {
                                    printf("\r\n%02X Error while erasing the device at address %06lX, the device is in unstable state, you still have hand on it while SBL is running.\r\n",eraseres,addrstart+(i*0x400));
                                    if (retcode == 2) printf("EEPROM array of destination is not empty, it must be erased first.\r\n");
                                    if (retcode == 1) printf("EEPROM address of destination is not Even.\r\n");
                                    if (retcode == 6) printf("A command is already waiting to be done.\r\n");
                                    if (retcode == 3) printf("PVIOL bit is set.\r\n");
                                    if (retcode == 4) printf("ACCESS bit is set.\r\n");
                                    if (retcode == 0x0B) printf("Wrong EEPROM Page Number\r\n");
                                    break;
                                  }
                              }
                            if (eraseres != 0) break;
                            
                            // Flash Page
                            long int addrfinal = (addrstart+(len));
                            while ( addrstart < addrfinal ) // ! feof( read_ptr ) 
                              {
                                char d1= fgetc( read_ptr );
                                char d0= fgetc( read_ptr );
                                int word = (d1<<8)|d0;
                                
                                retcode = ProgramMemoryWord (ECU_id, 1, addrstart, word, s, addr, frame);
                                printf(" Writing EEPROM file %s [%04lX/%04lX:%04X] %02X\r",DictionnaryItem.deriative, addrstart,addrfinal-1,word,retcode);
                                if (retcode != 0) 
                                  {
                                    printf("\r\n Write failed !!! \r\n");
                                    if (retcode == 2) printf("EEPROM array of destination is not empty, it must be erased first.\r\n");
                                    if (retcode == 1) printf("EEPROM address of destination is not Even.\r\n");
                                    if (retcode == 6) printf("A command is already waiting to be done.\r\n");
                                    if (retcode == 3) printf("PVIOL bit is set.\r\n");
                                    if (retcode == 4) printf("ACCESS bit is set.\r\n");
                                    if (retcode == 0x0B) printf("Wrong EEPROM Page Number\r\n");
                                    break;
                                  }
                                addrstart = addrstart+2;
                              }
                            
                            printf("\r\n%02i - Written %06lX, of len %04X\r\n", pagenumber, addrstart-1, len);
                            
                            pagenumber++;
                          }
                         if (eraseres != 0) break;
                         if (retcode != 0) break;
                      }
                      
                  printf("\r\n Finished\r\n");
                  fclose(read_ptr);
                  
                } else {
                   printf("The file isn't present in local folder.\r\n");
                }
          }
      else if (MenuSelected == 11)
        {
            //
          printf("Register address (hexa, ex: 30): ");
          long int addrstart =0;
          scanf("%lX", &addrstart);
          printf("Register Addr is : %04lX\r\n",addrstart);
          printf("Number of byte to read (hexa, ex 4): ");
          int len =0;
          scanf("%X", &len);
          printf("Reading size : %04X\r\n",len); 
          
          char buff[len];
          
          // Dump the data to the buff
          char c = DumpByPage(2, addrstart, len, s, ECU_id, addr, frame, buff);
          
          if (c==1)
            {
              printf("@%04lX : ", addrstart);
              for (int i=0;i<len;i++)
                {
                  printf("%02X ", buff[i]);
                }
              printf("\r\n\r\n");
            }
          else
            {
              printf("Nothing to read\r\n");
            }
        }
      else if (MenuSelected == 12)
        {
            //
          printf("Register address (hexa, ex: 30): ");
          long int addrstart =0;
          scanf("%lX", &addrstart);
          printf("Register Addr is : %04lX\r\n",addrstart);
          printf("Data to write (hexa, ex 4055): ");
          int len =255;
          char hextext[len];
          scanf("%s", hextext);
          
          len = strlen(hextext)/2; 
          char buff[len+1];
          int p=0;
          for (int o=0;o<=len;o=o+2)
            {
              char hex[3];
              hex[0]=hextext[o];
              hex[1]=hextext[o+1];
              hex[2]='\0';
              buff[p] = (int)strtol(hex, NULL, 16);
              printf("-%02X-\r\n",buff[p]);
              p++;
            }
          buff[len]='\0';
          
          printf("Hex data to send is : ");
          for (int o=0;o<len;o++)
          {
            printf(" Writing %02X to @%04lX : ", buff[o], addrstart);
            char retcode = ProgramMemoryWord (ECU_id, 2, addrstart, buff[o], s, addr, frame);
            printf("%02X\r\n",retcode);
            if (retcode != 0) 
              {
                printf("\r\n Writing failed !!! \r\n");
                break;
              }
            addrstart++;
          }
          printf("\r\n");
          
        }
    }
  return 1;
}


/*
  AW 10/08/2023
    MAIN CODE
*/
int main(int argc, char **argv)
{
	int s,sls, shs; 
	struct sockaddr_can addr;
  struct sockaddr_can addrls;
  struct sockaddr_can addrhs;
	//struct ifreq ifr;
	struct can_frame frame;
  char PIN[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}; 
  int ECU_id=0x50;
 
  //long int RunAddress = 0x1400;
  long int RunAddress = 0x3000;

	printf("\r\n\r\nECU SBL Uploader Tool by Alain WEISZ\r\n");

  printf("\r\nEnter CAN device name to use for HS (ex: can0,can1...), note that the device must be already up and set to the correct baud rate : ");
  char candev[5];
  scanf("%s", candev);
  printf("Opening device : %s\r\n",candev);
  shs = setupCANSock(candev, &addrhs);
  
  printf("\r\nEnter CAN device name to use for LS (ex: can0,can1...), note that the device must be already up and set to the correct baud rate : ");
  char candevls[5];
  scanf("%s", candevls);
  printf("Opening device : %s\r\n",candevls);
  sls = setupCANSock(candevls, &addrls);
  
  printf("\r\nIs the ECU on HS(0) or LS(1) CAN Bus ? : ");
  int choicedev;
  scanf("%d", &choicedev);
  if (choicedev == 0)
    {
      printf("Using High Speed CAN\r\n");
      addr = addrhs;
      s=shs;
    }
  else
    {
      printf("Using Low Speed CAN\r\n");
      addr = addrls;
      s=sls;
    }
  
  printf("\r\nEnter ECU ID (ex: 50,40... hexa value expected): ");
  scanf("%X", &ECU_id);
  printf("ECU id used : %02X\r\n",ECU_id);
  /*
  frame.can_id = CAN_DIAG_ID;
  frame.can_id |= CAN_EFF_FLAG;
	frame.can_dlc = 8;
  memset(frame.data,0,8); 
  */
  
  setFilter(shs, 0x000FFFFE, 0x000FFFFF);
  setFilter(shs, 0x00400000, 0x00F00000);
  setFilter(shs, 0x00000003, 0x0000FFFF);
  while (canRead(shs, (struct sockaddr *)&addr, &frame)>0) {usleep(10);};
  
  while (1)
    {
      printf("\r\n====== SBL Uploader Menu ======\r\n");
      
      printf("0 - Enter Programmation mode\r\n");
      printf("1 - Unlock with PIN code ex (FFFFFFFFFFFF)\r\n");
      printf("2 - Upload the SBL\r\n");
      printf("3 - Run into SBL Mode\r\n");
      printf("4 - Get ECU Infos\r\n");
      printf("5 - Exit Programmation Mode\r\n");
      printf("6 - Exit Software\r\n");
      printf("\r\nYour choice is ? ");
      
      int MenuSelected=0;
      scanf("%d", &MenuSelected);
          
      if (MenuSelected == 3)
        {
          SBLMenu(ECU_id, s, (struct sockaddr *)&addr, frame);
        } 
        
      if (MenuSelected == 1)
        {
          // Enter pin Code
          // Setup CAN ID
          frame.can_id = CAN_DIAG_ID;
          frame.can_id |= CAN_EFF_FLAG;
        	frame.can_dlc = 8;
          printf("PIN : ");
          char pin[12];
          scanf("%s", pin);
          printf("PIN is : %s\r\n",pin);
          int j=0;
          for (int i=0;i<6;i++)
            {
              char hex[5];
              hex[0]=pin[j];
              hex[1]=pin[j+1];
              hex[2]='\0';
              int data = (int)strtol(hex, NULL, 16);
              PIN[i]=data;
              j=j+2;
              if (debug) printf("%02X \r\n",PIN[i]);
            }
          char res = UnlockECU (PIN, ECU_id, s, (struct sockaddr *)&addr, frame);
          if (res == 0) 
            printf("Success\r\n");
          else
            printf("Fail to unlock\r\n");
        } 
          
      if (MenuSelected == 5)
        {
          printf("Exiting Programmig mode\r\n");
          // Setup CAN ID
          frame.can_id = CAN_DIAG_ID;
          frame.can_id |= CAN_EFF_FLAG;
        	frame.can_dlc = 8;
          memcpy(frame.data,&frames[11],8); 
          sendframe (shs, frame);
          sendframe (sls, frame);
          
          //break;
        }
      
      if (MenuSelected == 6)
        {
          printf("Exiting Software\r\n");
          break;
        }
          
      if (MenuSelected == 0)
        {
          // Mode Prog ON
          printf("Entering Programmig mode\r\n");
          // Setup CAN ID
          frame.can_id = CAN_DIAG_ID;
          frame.can_id |= CAN_EFF_FLAG;
        	frame.can_dlc = 8;
          memcpy(frame.data,&frames[0],8); 
          int t=0;
          
          while (t<1000)
            {
              sendframe (shs, frame);
              sendframe (sls, frame);
              usleep(5000);
              //while (canRead(s, (struct sockaddr *)&addr, &frame)>0) {usleep(10);};
              t++;
            }
            
          sleep(5);
          
          int silent = 0;
          
          printf("Waiting for silence ...\r\n");
          fflush(stdout);
          while ((canRead(s, (struct sockaddr *)&addr, &frame)>0) || (silent<100)) {usleep(1000);silent++;};
        }
        
      if (MenuSelected == 4)
        {
          // Device Infos
          char deviceNo[13];
          printf("\r\n");
          if (GetDeviceInfo (ECU_id, s, (struct sockaddr *)&addr, frame,   deviceNo) == 1)
            {
              printf("Device is : %s",deviceNo);
              GetSBLInfoFromHwNo(deviceNo, ECU_id);
              printf("Found SBL File to use : %s\r\n", DictionnaryItem.sbl_filename);
              printf("Start address : %04X\r\n", DictionnaryItem.startAddr);
              printf("ECU Id : %02X\r\n", DictionnaryItem.ecu_id);
              printf("Oscillator speed : %d KHz\r\n", DictionnaryItem.osc_speed);
              printf("Expected partid : 0x%04X, deriative file %s \r\n\r\n", DictionnaryItem.partid, DictionnaryItem.deriative);
              
            }
          else
            printf("No informations found about this ECU\r\n\r\n");
        }
        
      if (MenuSelected == 2)
        {
          // 0 - Get info about SBL to use from dictionnary file
          char deviceNo[13];
          int useCheckSum = 0;
          if (GetDeviceInfo (ECU_id, s, (struct sockaddr *)&addr, frame,   deviceNo) == 1)
            {
              printf("Device is : %s",deviceNo);
              GetSBLInfoFromHwNo(deviceNo, ECU_id);
              printf("Found SBL File to use : %s\r\n", DictionnaryItem.sbl_filename);
              printf("Start address : %04X\r\n", DictionnaryItem.startAddr);
              printf("ECU Id : %02X\r\n", DictionnaryItem.ecu_id);
              printf("Oscillator speed : %d KHz\r\n", DictionnaryItem.osc_speed);
              printf("Expected partid : 0x%04X, deriative file %s \r\n\r\n", DictionnaryItem.partid, DictionnaryItem.deriative);
              RunAddress = DictionnaryItem.startAddr;
              useCheckSum = DictionnaryItem.useChecksumFrame;
            }
          else
            printf("No SBL File found\r\n");
          
          
          // 1 - Start Primary Boot Loader Mode
          if (StartPBL (ECU_id, s, (struct sockaddr *)&addr, frame) == 0)
            {
              printf("\r\nFAILED TO ENTER PBL MODE\r\n");
              exit(1);
            }
          
          // 2 - Set Jump Address
          JumpToAddress (ECU_id, RunAddress, s, (struct sockaddr *)&addr, frame);
          sleep(1);
          
          // 3 - Sending SBL File
          
          const char * filename = DictionnaryItem.sbl_filename;
          //const char * filename = "./SBL31327215.hex"; RunAddress = 0x1400;// CEM HS
          //const char * filename = "./SBL31327215_40.hex"; RunAddress = 0x1400;// CEM LS
          //const char * filename = "./SBL30669185.hex"; RunAddress = 0x3000;// DIM
          
          if (UploadSBLData(filename, useCheckSum, ECU_id, s, (struct sockaddr *)&addr, frame) == 0) 
            {
              printf("\r\nUPLOAD FAILED\r\n");
              exit(1);
            }  
          
          // 4 - Sart SBL
          if (JumpToAddress (ECU_id, RunAddress, s, (struct sockaddr *)&addr, frame) == 0) 
            {
              printf("\r\nFAILED TO JUMP TO START ADDRESS\r\n");
              exit(1);
            }
          if (StartSBL(ECU_id, s, (struct sockaddr *)&addr, frame) == 0) 
            {
              printf("\r\nFAILED TO START SBL\r\n");
              exit(1);
            }
          // Waiting for SBL Alive message
          printf("Waiting for SBL to be Alive \r\n");
          // Catch answer
          fflush(stdout);
          while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(10000);};
          if ( (frame.can_dlc != 8) || (frame.data[0] != 0xFF) || (frame.data[1] != 0xAF) ) //(frame.can_id != 3) ||
            {
              printf("Invalid response.\r\n");
              return 0;
            }
           
          printf("\r\nDone, SBL Ready, Chip id 0x%02X%02X Memsize0: %02X, Memsize1: %02X\r\n", frame.data[3], frame.data[4], frame.data[5], frame.data[6]);
          
          // Configure SBL
          printf(" Configuring the SBL, ECU id : %02X...\r\n", frame.data[0]);
          frame.can_id = CAN_DIAG_ID;
          frame.can_id |= CAN_EFF_FLAG;
        	frame.can_dlc = 8;
          memcpy(frame.data,&frames[12],8); 
          
          // Configuration
          frame.data[0] = ECU_id; // ECU_ID
          frame.data[2] = 0x04; // ECLKDIV
          frame.data[3] = 0x04; // FCLKDIV
          
          sendframe (s, frame);
          fflush(stdout);
          while (canRead(s, (struct sockaddr *)&addr, &frame)<=0) {usleep(10000);};
          if ((frame.can_id != 3) || (frame.can_dlc != 8) || (frame.data[0] != ECU_id) || (frame.data[1] != 0xAF) || (frame.data[2] != 0x02)) 
            {
              printf("Invalid response.\r\n");
              return 0;
            }
          
          printf(" SBL Up Set and Running\r\n EEPROM PROT :%02X, EEPROM DIVIDER IS SET:%02X, EEPROM CLKDIV: %02X\r\n FLASH PROT :%02X, FLASH DIVIDER IS SET:%02X, FLASH CLKDIV: %02X.\r\n\r\n",frame.data[7], frame.data[3]&0x01, frame.data[5], frame.data[6], (frame.data[3]&0x02)>>1, frame.data[4]);
          
          SBLMenu(ECU_id, s, (struct sockaddr *)&addr, frame);
        }
    }
    

  if (close(shs) < 0) {
		perror("Close");
		return 1;
	}
 
 if (close(sls) < 0) {
		perror("Close");
		return 1;
	}
 
	return 0;
}
