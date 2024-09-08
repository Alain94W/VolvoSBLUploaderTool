#include <hidef.h>
#include <string.h>
#include "CANLib_H.h"

#include "mc9s12xdg128.h" 

#include "flash.h"
#include "eeprom.h"

#pragma MESSAGE DISABLE C4000
#pragma MESSAGE DISABLE C3804
#pragma MESSAGE DISABLE C12056

/*
Secondary Boot Loader for 9S12 controller.
Alain W. 10/08/2024
    
Change th CAN channel to use using the USED_CAN_INTERFACE const.
    
Edit thoses lines in MC9S12generic.prm to change the start address
and the ram space for variables

Function in RAM space here :
RAM_1000 = READ_ONLY  0x3C00 TO 0x3FFF;

// banked FLASH ROM 
Start address :
PAGE_RM = READ_ONLY  0x3000   TO 0x3BFF;
*/    
    
//#define USED_CAN_INTERFACE 0   // For DIM id 0x51   and CEM id 0x40
#define USED_CAN_INTERFACE 4 // For CEM id 0x50

#define MEMSIZ0 (unsigned char volatile *) (0x001C)
#define MEMSIZ1 (unsigned char volatile *) (0x001D)

void main(void) {

unsigned long CAN_DIAG_ID = 0xFFFFE;      // Diag addr
unsigned char CAN_ECU_ID = 0x50;          // CEM Addr
unsigned char ALL_ECU_ID = 0xFF;          // Listen also on all ECU Id
unsigned long CAN_ECU_RESPID = 0x00003;   // CEM Response Addr
unsigned long AddrStart = 0x00;           // AddrStrt
unsigned short len,I = 0;
unsigned int res=0;
unsigned char BufPntr[8];
unsigned long tmp=0;
unsigned char *ptr;
unsigned char cks = 0;
unsigned char configDone=0;
unsigned char divEEP=0;
unsigned char divFLH=0;
unsigned char CAN_INTERFACE = USED_CAN_INTERFACE;
CANRxMessage Recpt;

 
 // Send SBL Alive message
 BufPntr[0]=0xFF;  // Undefined ECU ID, need to be set after SBL is UP
 BufPntr[1]=0xAF;
 BufPntr[2]=0x01;
 BufPntr[3]=PARTIDH;
 BufPntr[4]=PARTIDL;
 BufPntr[5]=*(MEMSIZ0);
 BufPntr[6]=*(MEMSIZ1);
 BufPntr[7]='0';
 WriteCAN(CAN_ECU_RESPID, 8, BufPntr, CAN_INTERFACE, 1);
 
 while(1) 
 { 
 if (DataAvailable(CAN_INTERFACE) == 1) {
  
  // Data received, handle it
  res = ReadCAN(CAN_INTERFACE, &Recpt);
   
  // Check that we'v got 8 bytes DLC, packet from ID FFFFE
  if ((res == 8) && (Recpt.id == CAN_DIAG_ID)){
  
  // Have we configured the SBL yet ?  
  if (configDone == 0) {
    if (Recpt.RxBuff[1] == 0xCC) {
      
      // Configuration, set the ECU_ID, divider for EEPROM and FLASH clock
      CAN_ECU_ID = Recpt.RxBuff[0];
      divEEP = Recpt.RxBuff[2];
      divFLH = Recpt.RxBuff[3];
      initFlash (divFLH);
      initEEPROM (divEEP);
        
      
      // Send SBL Config OK message with new ECU ID, and Flash and EEPROM status (clock divider, protection status)
      BufPntr[0]=CAN_ECU_ID;
      BufPntr[2]=0x02;
      BufPntr[3]=(FCLKDIV_FDIV<<1) | (ECLKDIV_EDIV);
      BufPntr[4]=FCLKDIV;
      BufPntr[5]=ECLKDIV;
      BufPntr[6]=FPROT;
      BufPntr[7]=EPROT;
      WriteCAN(CAN_ECU_RESPID, 8, BufPntr, CAN_INTERFACE, 1);
      configDone = 1;
    } 
    else continue;
  }
  
  // Config is ok, check the ECU ID
  if ((Recpt.RxBuff[0] != CAN_ECU_ID) && (Recpt.RxBuff[0] != ALL_ECU_ID)) continue;
   
   // D0 Command : Read Data from FLASH, EEPROM or REGISTER   
    if (Recpt.RxBuff[1] == 0xD0) {
        char PacketStart = 3;
        char u=PacketStart;
        unsigned char Type=0;
        unsigned char val=0;
        unsigned short packetno=0;
        unsigned long j=0;
        
        // FLASH(0) or EEPROM(1) ? 
        Type = Recpt.RxBuff[2];
        
        // PPAGE to read (0xE0 - 0xFF)
        val=Recpt.RxBuff[3];
        if (Type == 0) {
          if (val>=0xE0)
            PPAGE = val;
          else
            PPAGE=0xFE; // Page par defaut
        } else
        if (Type == 1) {
          if (val>=0xFB)
            EPAGE = val;
          else
            EPAGE=0xFF; // Page par defaut
        }
        
        // Address
        tmp=Recpt.RxBuff[4];
        AddrStart=tmp<<8;
        AddrStart=AddrStart|Recpt.RxBuff[5];
        
        // Len is 16its, allow up to 0xFFFF len
        len = (Recpt.RxBuff[6]<<8)|Recpt.RxBuff[7];
        
        // Point on AddrStart
        ptr=(unsigned char *)AddrStart;
        
        // Format the buffer
        BufPntr[0]=0xA8+5; // Data packet
        BufPntr[1]=0; // Len high part
        BufPntr[2]=0; // Len low part
        I=0;
        cks=0;
        while (I<len) {
          cks += ptr[I];
          BufPntr[u] = ptr[I];
          I++;
          u++;
          if (u==8) {
            u=PacketStart;
            BufPntr[1]=(packetno&0xFF00)>>8;
            BufPntr[2]=(packetno&0x00FF);
            WriteCAN(CAN_ECU_RESPID, 8, BufPntr, CAN_INTERFACE, 1);
            packetno++;
            for(j=0;j<5000;j++){asm("NOP");}; // Sleep to allow the destination CAN BUS to read all frames
          }
        }
        if (u>PacketStart) {
          // Sending last data
          BufPntr[0]=0xA8+(u-PacketStart);
          BufPntr[1]=(packetno&0xFF00)>>8;
          BufPntr[2]=(packetno&0x00FF);
          for (I=u;I<8;I++) {          
            BufPntr[I]=0;
          }
          WriteCAN(CAN_ECU_RESPID, 8, BufPntr, CAN_INTERFACE, 1);
        }
        //break;
      } else
    
     if ((Recpt.RxBuff[1] == 0xA8) || (Recpt.RxBuff[1] == 0xC8)){
      // Exit SBL, reset ecu into normal mode
       BufPntr[0]=CAN_ECU_ID;
       BufPntr[1]=0xA8;
       BufPntr[2]=0x00;
       BufPntr[3]=0x00;
       BufPntr[4]=0x00;
       BufPntr[5]=0x00;
       BufPntr[6]=0x00;
       BufPntr[7]=0x00;
       WriteCAN(CAN_ECU_RESPID, 8, BufPntr, CAN_INTERFACE, 1);
       //_ENABLE_COP(1);
       //(*(volatile unsigned char*)_COP_RST_ADR)=0x47;
       COPCTL=4;
       ARMCOP=0x55;
       ARMCOP=0xAA;
     } else
    
    // B4 : Send previously calculated checksum
     if (Recpt.RxBuff[1] == 0xB4){
       BufPntr[0]=CAN_ECU_ID;
       BufPntr[1]=0xB1;
       BufPntr[2]=cks;
       BufPntr[3]=0x00;
       BufPntr[4]=0x00;
       BufPntr[5]=0x00;
       BufPntr[6]=0x00;
       BufPntr[7]=0x00;
       WriteCAN(CAN_ECU_RESPID, 8, BufPntr, CAN_INTERFACE, 1);
     } else
 
     // F8, DA : ERASE or WRITE datas to FLASH(0), EEPROM(1) or REGISTER(2)
     if ((Recpt.RxBuff[1] == 0xF8) || (Recpt.RxBuff[1] == 0xDA)) {
        
        // F8 : Flash/EEPROM Sector Erase 0x400 bytes (0x04 for EEP)
        // DA : Flash/EEPROM Word Program
        unsigned char Type=0;
        unsigned char page=0;
        unsigned int data = 0;
        char retcode = 0xFF;
                
        // FLASH(0) or EEPROM(1) ? 
        Type = Recpt.RxBuff[2];
        
        // PPAGE to erase (0xE0 - 0xFF)
        page=Recpt.RxBuff[3];
        if (Type == 0) {
          if ( page<0xE0 )
          {
            BufPntr[0]=CAN_ECU_ID;
            BufPntr[1]=Recpt.RxBuff[1]+1;
            BufPntr[2]=0x0A; // Invalid PPAGE
            BufPntr[3]=0x00;
            BufPntr[4]=0x00;
            BufPntr[5]=0x00;
            BufPntr[6]=0x00;
            BufPntr[7]=0x00;
            WriteCAN(CAN_ECU_RESPID, 8, BufPntr, CAN_INTERFACE, 1); 
            continue;
          }
        } else 
        if (Type == 1) {
          if ( page<0xFC )
          {
            BufPntr[0]=CAN_ECU_ID;
            BufPntr[1]=Recpt.RxBuff[1]+1;
            BufPntr[2]=0x0A; // Invalid EPAGE
            BufPntr[3]=0x00;
            BufPntr[4]=0x00;
            BufPntr[5]=0x00;
            BufPntr[6]=0x00;
            BufPntr[7]=0x00;
            WriteCAN(CAN_ECU_RESPID, 8, BufPntr, CAN_INTERFACE, 1); 
            continue;
          }
        } else if (Type != 2) continue;
        
        // Address
        tmp=Recpt.RxBuff[4];
        AddrStart=tmp<<8;
        AddrStart=AddrStart|Recpt.RxBuff[5];
        // Data, for flashing only
        data = (Recpt.RxBuff[6]<<8)|Recpt.RxBuff[7];
        
        
        //Erase command
        if (Recpt.RxBuff[1] == 0xF8) {
            if (Type == 0)
              retcode = EraseSectorInFlash(page, (unsigned int)AddrStart);
            else 
            if (Type == 1)
              retcode = EraseSectorInEEPROM(page, (unsigned int)AddrStart);
          } else
        // Program command
        if (Recpt.RxBuff[1] == 0xDA) {
            if (Type == 0)
              // Flash Write
              retcode = WriteWordToFlash(page, (unsigned int)AddrStart, data);
            else 
            if (Type == 1 )
              // EEPROM Write
              retcode = WriteWordToEEPROM(page, (unsigned int)AddrStart, data);
            else
            if (Type == 2) {
              // Registers Write
              (*(unsigned char*)AddrStart) = (unsigned char)(data & 0xFF);
              retcode = 0;
            }
          } 
          
        // Send result
        BufPntr[0]=CAN_ECU_ID;
        BufPntr[1]=Recpt.RxBuff[1]+1;
        BufPntr[2]=retcode;
        BufPntr[3]=Recpt.RxBuff[3];
        BufPntr[4]=FCLKDIV;
        BufPntr[5]=ECLKDIV;
        BufPntr[6]=0x00;
        BufPntr[7]=0x00;
        WriteCAN(CAN_ECU_RESPID, 8, BufPntr, CAN_INTERFACE, 1);
     }
  }
 }
 };
}

 
 