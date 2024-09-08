#ifndef __MCHFIILE__
#define __MCHFIILE__
#include "mc9s12xdg128.h"
//#include "mc9s12xdg128.c"
#endif
#include "eeprom.h"

#pragma MESSAGE DISABLE C1860


void initEEPROM (unsigned char div) {
//unsigned char fclkv;
//unsigned long tmp;                       
  if (ECLKDIV_EDIV == 0) {
  /*
    tmp=osckhz;
    if (osckhz >= 12000){
      fclkv = osckhz/8/200 - 1;
      ECLKDIV = ECLKDIV | fclkv | 0x08;
    } else {
      fclkv = osckhz/8/200 - 1;
      ECLKDIV = ECLKDIV | fclkv;
    }
    */
    ECLKDIV = ECLKDIV | div;
  }
  EPROT = 0xFF; // Disable all protections
  ESTAT = 0x80; // Clear CBEIF
  ESTAT = (ESTAT_ACCERR | ESTAT_PVIOL); // Clear errors
  return;
}


char WriteWordToEEPROM(unsigned char page, unsigned int address, unsigned int data) {
unsigned int addr;
  addr = (unsigned int)address;
  ESTAT = (ESTAT_ACCERR | ESTAT_PVIOL); // Clear errors
  EPAGE = page;
  if (ESTAT_CBEIF == 0) return 6; // Command already waiting for execution
  if ((unsigned int)addr & 0x0001) {return 1;} // Odd address not allowed
  if (*(unsigned int*)addr != 0xFFFF) {return 2;} // Destination address not empy
  
  *(unsigned int*)(addr) = data;  // push data at destinaton address
  ECMD = 0x20; // WORD PROG
  ESTAT = 0x80; // Clear CBEIF
  if (ESTAT_PVIOL) return 3; // Violation error
  if (ESTAT_ACCERR) return 4; // Access error
  while (ESTAT_CCIF == 0) {};
  return 0;
}

char EraseSectorInEEPROM(unsigned char page, unsigned int address) {
unsigned int addr;

  addr = (unsigned int)address;
  ESTAT = (ESTAT_ACCERR | ESTAT_PVIOL); // Clear errors
  
  EPAGE=page;

  if ((unsigned int)addr & 0x0001) {return 1;} // Odd address not allowed
  if ((unsigned int)addr % EEPROM_SECTOR_SIZE != 0) {return 5;} // Not the start of the flash sector
  (*(unsigned int *)addr) = 0xFFFF;  // Empty value
  ECMD = 0x40; // ERASE SECTOR
  ESTAT = 0x80; // Clear CBEIF
  if (ESTAT_PVIOL) return 3; // Violation error
  if (ESTAT_ACCERR) return 4; // Access error
  while (ESTAT_CCIF == 0) {};
  return 0;
}