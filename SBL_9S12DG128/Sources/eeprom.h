/*
  AW 04/07/2024
  Header file to use flash programmation and erasing of 9S12
  Setup for 9S12DT
*/

#define EEPROM_SECTOR_SIZE 4    // 1 sector = 4 bytes
#define EPAGE_SIZE         0x400// Size of a program page

void initEEPROM (unsigned char div );
char WriteWordToEEPROM(unsigned char page, unsigned int address, unsigned int data);
char EraseSectorInEEPROM(unsigned char page, unsigned int address);
