# VolvoSBLUploaderTool
Volvo 9S12(X) Secondary Boot Loader Uploader Tool

This software was developed for testing and editing Volvo's car ECU firmware. It was mainly made for P1 Platform (S40, V50, C30, S/C70).
The Secondary Boot Loader and Uploader tool were tested on both on bench and in car.

This suit of software is not attended to be used for hacking or to be used to hurt someone, it must be used with a lot of care as it can brick your car and transform it into a very beautiful flower pot.
If you make the choice of using it, you take all responsibility to all that can append too you or your car, I will not be responsible any kind of usage made of it. Before doing anything, always perform backup, use a battery charger that will be able to power up the car to prevent the battery to drop out and kick your ECU/car.
It is made to be used by experienced people who clearly understand how a SBL/PBL works. 

If you want to use it for commercial use, please name the author of this project. In any case you will be awesome if you want to make a donation :

https://www.paypal.com/donate/?business=SMJHF6RVYC25J&no_recurring=0&currency_code=EUR

The software was developed on a Raspberry Pi 4 equipped with a dual CAN Board.
This git will not describe where to find parts or how to setup it as they are tone of tutorial on the web to do this.

# Secondary Boot Loader

The secondary boot loader are designed to be use on mcirocontroller : 9S12 and 9S12X.
They are different version precompilled ready to be used for different configurations : using CAN0 or CAN4 channel, starting code in RAM at address 1400 or 3000.
For this reason the following convention in naming was used : SBL_XXXXX_YYYY_ZZZZZ.hex where:
* XXXXX = is the MCU Family 9S12 or 9S12X
* YYYY  = is the CAN channel used CAN4, CAN0
* ZZZZZ = is the RAM Address Location for the code.

The files are in intel hex code, they will be sent over the CAN using a specific protocol described by Robert A. Hilton (thanks to this guy).
If you want to add your own edited SBL files, always think about to remove the lines corresponding to the isr and start vectors as they will probably overwrite the one present in the MCU and them brick your ECU at reboot.

A Dictionary.json file is used to automatically select the good SBL corresponding to one ECU ( for now : CEM, DIM).

If you made some new SBL or add ECU to this existing list, please share it here with us !

# Compile Uploader Tool

To compile the uploader tool, you will need to download a free library called lib jansson, this project use the version 2.10.
then use the simple following command:

```
gcc -ljansson -o volvosbluploader ./VolvoSBLUploaderTool.c
```

Run the program:

```
./volvosbluploader
```

Easy use hint:
```
ECU SBL Uploader Tool by Alain WEISZ

Enter CAN device name to use for HS (ex: can0,can1...), note that the device must be already up and set to the correct baud rate : can0
Opening devices : can0

Enter CAN device name to use for LS (ex: can0,can1...), note that the device must be already up and set to the correct baud rate : can1
Opening devices : can1

Is the ECU on HS(0) or LS(1) CAN Bus ? : 0
Using High Speed CAN

Enter ECU ID (ex: 50,40... hexa value expected): 50
ECU id used : 50

====== SBL Uploader Menu ======
0 - Unlock with PIN code ex (FFFFFFFFFFFF)
1 - Enter Programmation mode
2 - Upload the SBL
3 - Run into SBL Mode
4 - Get ECU Infos
5 - Exit Programmation Mode
6 - Exit Software

Your choice is ? 

```

First put all ECU into programming mode : Option 1, the complete car will be switched off except the ECM (if key was in position I or II).
Some ECU require a PIN Code, for example the CEM only on High Speed CAN, then use Option 0 and enter your PIN code.
After unlocking (if needed), use option 2 to upload the SBL to the selected ECU.
if everything goes well, your will enter a the SBL menu.

```
Entering into SBL...Done
Done
Waiting for SBL to be Alive 

Done, SBL Ready, Chip id 0xC411 Memsize0: C0, Memsize1: 00
 Configuring the SBL, ECU id : FF...
 SBL Up Set and Running
 EEPROM PROT :FF, EEPROM DIVIDER IS SET:00, EEPROM CLKDIV: 84
 FLASH PROT :FF, FLASH DIVIDER IS SET:00, FLASH CLKDIV: 84.



====== SBL Menu ======
1  - Dump Flash by Program Page and address to a file
2  - Dump EEPROM by EEPROM Page and address to a file
3  - Dump all the Flash to a file
4  - Dump all the EEPROM to a file
5  - Erase and Program Flash Page from file
6  - Erase Flash sectors
7  - Erase and Program EEPROM Page from file
8  - Erase and Program EEPROM Words to Page
9  - Erase and Program FULL FLASH from file
10 - Erase and Program Full EEPROM from file
11 - Read register
12 - Write register
19 - Exit SBL Menu

Your choice is ? 

```

Some information about the chip will be displayed like the part id, eeprom/flash protection and the memsize. This will guide you on what deriative you will have to use if you want to full dump or full write Flash/EEPROM.
You will have to check the existing derivative files of to look for the part id in the device datasheet to ensure you use the good derivative file.
Some derivative file are already present here for CEM and DIM.
The best practice still to open the ECU and check directly on the MCU.

Some MCU have protection enabled, when flashing the full ship using this tool, an error will append when trying to erase the protected sector, the software will then write the flash until the last erased sector.
For security reason this software is not doing a mass erase on the chip. You can do it by yourself when playing with the Option 11/12 Read/Write register.

The SBL is working with Page access mode for dumping and programming, the derivative file will help with this to perform the full dump/write of the EEPROM and Flash.

In any case, if you have a fail to write data or you quit the uploader tool, you still have hand on the MCU as long as the power remain and you don't leave the programming mode, the SBL code is running in RAM, don't power off, unplug the ECU if you have a trouble or the only way to give it back to life will be to use a BDM or a programmer.

PS : I'm looking for a dump of CEM that has TPMS activated and working, If you have this and would like to share it with me you will be awesome.

# Dice
I'm not a Windows user so I don't have a version of this tool using windows and it is not working with Dice. If you made your own software based on it and using Dice, I would be happy to use it too !
