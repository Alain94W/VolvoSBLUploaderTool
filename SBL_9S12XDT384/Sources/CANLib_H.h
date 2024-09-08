/* --------------------------------------------------------- */
/* Librairie CAN pour HC12                                   */
/* Par Alain WEISZ le 15/08/2013 v1.0                        */
/*                                                           */
/* Utilisation :                                             */
/*                                                           */
/*    1 appeler CANInit                                      */
/*    2 Utiliser SendCANMessage(NbByteToSend, Buffer, Chan)  */
/*      pour envoyer un message sur Buffer [NbByteTosend]    */
/*      sur le canal CAN voulue (Chan).                      */
/* --------------------------------------------------------- */




// CAN Default register
 #define CAN0Register (unsigned char volatile *) (0x0140)
 #define CAN1Register (unsigned char volatile *) (0x0180)
 #define CAN4Register (unsigned char volatile *) (0x0280)

 #define CANCTL0  0x00
 #define CANCTL1  0x01
 #define CANBTR0  0x02
 #define CANBTR1  0x03
 #define CANRFLG  0x04
 #define CANRIER  0x05
 #define CANTFLG  0x06
 #define CANTIER  0x07
 #define CANTARQ  0x08
 #define CANTAAK  0x09
 #define CANTBSEL 0x0A
 #define CANIDAC  0x0B
 #define CANTBPR  0x0D
 #define CANRXERR 0x0E
 #define CANTXERR 0x0F
 #define CANIDAR0 0x10
 #define CANIDAR1 0x11
 #define CANIDAR2 0x12
 #define CANIDAR3 0x13
 #define CANIDMR0 0x14
 #define CANIDMR1 0x15
 #define CANIDMR2 0x16
 #define CANIDMR3 0x17
 #define CANIDAR4 0x18
 #define CANIDAR5 0x19
 #define CANIDAR6 0x1A
 #define CANIDAR7 0x1B
 #define CANIDMR4 0x1C
 #define CANIDMR5 0x1D
 #define CANIDMR6 0x1E
 #define CANIDMR7 0x1F
 #define CANRXFG  0x20
 #define CANRXDLR 0x2C
 #define CANTXFG  0x30
  
  
// Structure
typedef struct{
        unsigned long id; 
        unsigned char Channel;
        unsigned char DLC;
        unsigned char RxBuff[8];
        unsigned char Ext;
} CANRxMessage;                            

// definition des fonctions
 void  WriteCAN(unsigned long id,unsigned char NumBytes, unsigned char *BufPntr, unsigned char Channel, unsigned char Ext);
 int   ReadCAN(unsigned char Channel, CANRxMessage *Rx);
 unsigned char DataAvailable(unsigned char Channel); 