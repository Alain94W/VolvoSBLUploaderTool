#include "CANLib_H.h"
#pragma MESSAGE DISABLE C12056 

//#############################################################################
// CAN Register selector
//
//  Return the corresponding CAN Register from the selected channel
//  0-> CAN0
//  1-> CAN1
//  4-> CAN4 
// AW v1.0 3/07/2024
//
unsigned char volatile * getCANReg(unsigned char Channel) {
  // Selection du canal CAN0 ou CAN1, par defaut, CAN0
 if (Channel == 0) return CAN0Register; else
 if (Channel == 1) return CAN1Register; else
 if (Channel == 4) return CAN4Register; else
  return CAN0Register;
}

//#############################################################################
// Transmition routine
//
// id : CAN ID used to send the message
// NumByte : Number of Byte to send in the message
// BuffPntr : Pointer on the Buffer caontaining the message
// Channel : CAN Channel to use 0,1.
// Ext : Use Extended ID (29 bit) if = 1.
//
// AW v1.0 17/08/2013
//
void WriteCAN(unsigned long id,
              unsigned char NumBytes, 
              unsigned char *BufPntr, 
              unsigned char Channel, 
              unsigned char Ext)
 {
 
 unsigned char NACAN,C;
 unsigned char volatile * CANReg;
 unsigned char RTR = 0;
 unsigned char IDE = Ext;
 unsigned char SRR = 1;

 
 CANReg = getCANReg(Channel);

 // wait for available buffer
 while(! *(CANReg+CANTFLG));
 
 // get the next available CAN buffer
 NACAN=*(CANReg+CANTFLG);
 *(CANReg+CANTBSEL)=NACAN;

 // Set CAN ID
 
 if (Ext == 0) { 
  // Set Standard 11bit ID idr0.id10:id3
  *(CANReg+CANTXFG+0)= (id>>3)&0xFF;
  *(CANReg+CANTXFG+1)= (id<<5)&0xFF;
  *(CANReg+CANTXFG+2)=0x00;
  *(CANReg+CANTXFG+3)=0x00;
 } else {
   // Set Standard 28bit ID
  *(CANReg+CANTXFG+0)= (id>>(24+2))&0xFF ;
  *(CANReg+CANTXFG+1)= ((id>>17)&0x7)|(IDE<<3)|(SRR<<4)|((id>>13)&0xE0);
  *(CANReg+CANTXFG+2)=(id>>9)&0xFF;
  *(CANReg+CANTXFG+3)=((id<<1)&0xFF)|RTR;
 }
 
 // security for max 8 data
 if (NumBytes>8) NumBytes=8;
 
 // Store the data in the CANTX registers
 for (C=0;C<NumBytes;C++)
 *(CANReg+CANTXFG+4+C)=*BufPntr++;

 // Wrtie DLC Register (Number of data to send)
 *(CANReg+CANTXFG+0x0C)=NumBytes;
 
 // Choose the available TXBuffer
 NACAN=*(CANReg+CANTBSEL);
 
 
 *(CANReg+CANTBPR) = 0;
 
 // Transmit data
 *(CANReg+CANTFLG) =NACAN;

 return;
 }


//#############################################################################
// Reception routine
//
// INPUT :
//
// Channel  : CAN Channel used for the reception
// DLC      : Data Lenght Count
// Ext      : Use Extended ID (29 bit) if = 1.
// RxBuff   : Buffer of received data
// Id       : Received CAN Id
//
// OUTPUT : 0 if nothing readed, DLC if a message was handled
//
// AW v1.0 20/08/2013
//
int ReadCAN(unsigned char Channel, CANRxMessage *Rx)
 {
 unsigned char C;
 unsigned char volatile * CANReg;
 unsigned long dt=0;
 char d0,d1,d2,dlc=0;
 
  // Store CAN Channel
  Rx->Channel = Channel;
  
  CANReg = getCANReg(Channel);

  // Check data are present in the RX buffer
  if ((*(CANReg+CANRFLG) & 0x01) == 0x00) {
  // Rien recu.
  Rx->DLC = 0;
  Rx->id = 0;
  return Rx->DLC;
  }
 
  // Recup le nombre de data recues  - DLC
  Rx->DLC = *(CANReg+CANRXDLR) & 0x0F;
  dlc = Rx->DLC;
  
  // Fill the RxBuff
  for (C=0;C<Rx->DLC;C++) {
   unsigned char volatile *regrx = CANReg+CANRXFG+4+C;
   Rx->RxBuff[C]=*(regrx);
  } 
 
  // Get the Id mode (STD/EXT)
  Rx->Ext = (*(CANReg+CANRXFG+1)) & 0b0001000;
  
  if (Rx->Ext == 0){ 
   // Get the Standard 11 bit CAN Id 
   Rx->id = 0;
   Rx->id = (*(CANReg+CANRXFG+0)) << 3; //Rxid = IDR0  (8bit)
   Rx->id = Rx->id + ((*(CANReg+CANRXFG+1) & 0xE0) >> 5);
   *(CANReg+CANRXFG+2)=0x00;
   *(CANReg+CANRXFG+3)=0x00;
  } else {
   // Get the Extended 29 bit CAN Id 
   Rx->id = 0;
   dt = 0;
   d0 = ((*(CANReg+CANRXFG+3)&0xFE) >> 1); // Remove RTR bit
   d1 = ((*(CANReg+CANRXFG+2)&0x01)<<7); // Get Bit 1 from IDR2 and move it to bit 7 of IDR1
   Rx->id = (d0 | d1 )&0xFFFF;
   
   dt =0;
   dt = ((*(CANReg+CANRXFG+2)) >> 1) | ((*(CANReg+CANRXFG+1)&0x01)<<7) ;
   
   Rx->id = Rx->id | (dt<<8); // Vire ID7, récup ID15
   
   dt = 0;
   d0 = ((*(CANReg+CANRXFG+1)&0x07) >> 1);
   d1 = ((*(CANReg+CANRXFG+1)&0xE0) >> 3);
   d2 = ((*(CANReg+CANRXFG+0)&0x03) << 6);
   dt = d0 | d1 | d2;
   Rx->id = Rx->id | (( dt )<< 16); // Vire ID7, récup ID15
   dt = 0;
   d0 = ( (*(CANReg+CANRXFG+0) & 0x1F) >> 3);
   dt = d0;
   dt = dt << 24;
   Rx->id = Rx->id | dt;
  }
  
  *(CANReg+CANCTL0) |= 0x80;        // clear RXFRM flag
  *(CANReg+CANRFLG) |= 1;           // clear rec flag
 
 return dlc;
 }

//#############################################################################
// Verifiction Presence Donnée routine
//
// INPUT :
//
// Channel  : 
// CAN Channel used for the reception
//
// OUTPUT : 
// 0 si pas de donnée dans le buffer, 1 sinon
//
// AW v1.0 11/07/2024
// 
unsigned char DataAvailable(unsigned char Channel) {
 unsigned char volatile * CANReg;
 CANReg = getCANReg(Channel);
  return *(CANReg+CANRFLG) & 0x01;
 }