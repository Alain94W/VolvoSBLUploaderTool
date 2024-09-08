/*
  AW 04/07/2024
  Header file to use flash programmation and erasing of 9S12X
  Setup for 9S12XDT384
*/

#define FLASH_SECTOR_SIZE 1024    // 1 sector = 1024 bytes
#define FLASH_BLOCK_SIZE  128     // 1 block size is 128Ko, divided into FLASH_SECTOR_SIZE bytes
#define PAGE_SIZE         0x4000  // Size of a program page (384 has 29 * 16Ko Pages)



void initFlash (unsigned char div);
char WriteWordToFlash(unsigned char page, unsigned int address, unsigned int data);
char EraseSectorInFlash(unsigned char page, unsigned int address);
