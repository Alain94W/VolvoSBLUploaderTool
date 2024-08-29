#ifndef __MCHFIILE__
#define __MCHFIILE__
#include "mc9s12dg128.h"
#endif
#include "flash.h"

#pragma MESSAGE DISABLE C1860


void initFlash (unsigned char div) {
//unsigned char fclkv;
//unsigned long tmp;                       
  if (FCLKDIV_FDIV == 0) {
  /*
    tmp=osckhz;
    if (osckhz >= 12000){
      fclkv = osckhz/8/200 - 1;
      FCLKDIV = FCLKDIV | fclkv | 0x08;
    } else {
      fclkv = osckhz/8/200 - 1;
      FCLKDIV = FCLKDIV | fclkv;
    }
    */
    FCLKDIV = FCLKDIV | div;
  }
  FPROT = 0xFF; // Disable all protections
  FSTAT = 0x80; // Clear CBEIF
  FSTAT = (FSTAT_ACCERR | FSTAT_PVIOL); // Clear errors
  return;
}

char WriteWordToFlash(unsigned char page, unsigned int address, unsigned int data) {
unsigned int addr;
  addr = (unsigned int)address;
  FSTAT = (FSTAT_ACCERR | FSTAT_PVIOL); // Clear errors
  PPAGE = page;
  //PPAGE = (address - 0x400000) / PAGE_SIZE; // Set PPAGE
  if (FSTAT_CBEIF == 0) return 6; // Command already waiting for execution
  if ((unsigned int)addr & 0x0001) {return 1;} // Odd address not allowed
  if (*(unsigned int*)addr != 0xFFFF) {return 2;} // Destination address not empy
  
  *(unsigned int*)(addr) = data;  // push data at destinaton address
  FCMD = 0x20; // WORD PROG
  FSTAT = 0x80; // Clear CBEIF
  if (FSTAT_PVIOL) return 3; // Violation error
  if (FSTAT_ACCERR) return 4; // Access error
  while (FSTAT_CCIF == 0) {};
  return 0;
}

char EraseSectorInFlash(unsigned char page, unsigned int address) {
//unsigned int *addr;
unsigned int addr;  

  addr = (unsigned int)address;
  FSTAT = (FSTAT_ACCERR | FSTAT_PVIOL); // Clear errors
  
  PPAGE=page;
  
  //PPAGE = (address - 0x400000) / PAGE_SIZE; // Set PPAGE
  if ((unsigned int)addr & 0x0001) {return 1;} // Odd address not allowed
  if ((unsigned int)addr % FLASH_SECTOR_SIZE != 0) {return 5;} // Not the start of the flash sector
  (*(unsigned int *)addr) = 0xFFFF;  // Empty value
  FCMD = 0x40; // ERASE SECTOR
  FSTAT = 0x80; // Clear CBEIF
  if (FSTAT_PVIOL) return 3; // Violation error
  if (FSTAT_ACCERR) return 4; // Access error
  while (FSTAT_CCIF == 0) {};
  return 0;
}