//
//  main.c
//  Extension
//
//  Created by Dave Hayden on 7/30/14.
//  Copyright (c) 2014 Panic, Inc. All rights reserved.
//

#include <stdlib.h>
#include <assert.h>

#include "pd_api.h"

static int update(void* userdata);
const char* fontpath = "/System/Fonts/Asheville-Sans-14-Bold.pft";
LCDFont* font = NULL;


#ifdef TARGET_PLAYDATE

// implementation for raisePrivsHard() is left as an exercise for the reader for now

static void ResetOneRccRegister(uint32_t addr) {
	uint32_t volatile* r = (uint32_t*)addr;
	*r = ~0;
	*r = 0;
}

static void ResetOneRccRegisterWithValue(uint32_t addr, uint32_t value) {
	uint32_t volatile* r = (uint32_t*)addr;
	*r = value;
	*r = 0;
}

static void TryResetToDfuPart2(void);
static void BootIntoLinux(void);

static void tryResetToDfu() {
	// https://stackoverflow.com/a/35167536
	// https://github.com/micropython/micropython/blob/0986675451edbdcbe31d90ddacf8f6dc3327a4ae/stmhal/modmachine.c#L197-L223
	// https://github.com/STMicroelectronics/STM32CubeF7/blob/f8cefdf02e8ad7fd06bd38a2dd85a55ecdbe92a9/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal.c#L168
	// https://github.com/STMicroelectronics/STM32CubeF7/blob/c7c5ec99c7482ea8bcdbf0a869c930af4547088f/Drivers/CMSIS/Core_A/Include/cmsis_gcc.h#L439
	// set boot mode = 1 (boot)
	// disable interrupts
	__asm volatile ("cpsid i" : : : "memory");

	uint32_t volatile* boot_reason = (uint32_t volatile*)0x40024000;
	*boot_reason = 1;
	// reset control
	__builtin_arm_wsr("control", 0);
	__builtin_arm_isb(0xf);

	// turn off SysTick
	uint32_t volatile* syst_csr = (uint32_t volatile*)0xE000E010;
	*syst_csr = *syst_csr & ~((1 << 0) | (1 << 1));

	// relocate ourself into sram
	//char* target_sram = (void*)0x20000000;
	//__builtin_memcpy(target_sram, &TryResetToDfuPart2, sizeof(TryResetToDfuPart2));
	//__builtin_arm_dsb(0xf);
	//void (*TryResetToDfuPart2_sram)(void) = (void (*)(void))target_sram+1;
	//TryResetToDfuPart2_sram();
	//TryResetToDfuPart2();
	BootIntoLinux();
}

static void BootIntoLinux() {
	// yolo
	void (*entry)(uint32_t should_be_zero, uint32_t machine_type, void* dtb) = (void*)0x60008001;
	entry(0, ~0, (void*)0x60ff0000);
	while (1) {__asm volatile("");}
}

static void TryResetToDfuPart2() {
#if 0
	uint32_t volatile* nvic_aircr = (uint32_t volatile*)0xe000ed0c;
	*nvic_aircr = (*nvic_aircr & 0x700) | 0x05FA0004;
	while (1) {__asm volatile("");}
#endif

	// TODO(zhuowei): reset hardware (RCC, peripherals)
	ResetOneRccRegister(0x40023800);
	ResetOneRccRegister(0x40023804);
	// reset DMA without touching GPIO, otherwise the RAM shuts off?
	ResetOneRccRegisterWithValue(0x40023810, 0xfffff000);
	ResetOneRccRegister(0x40023814);
	// don't touch ahb3 - we're running in sdram; can't reset the memory controller under us!
	// ResetOneRccRegister(0x40023818);
	// the actual bootloader resets the interrupts on the rcc here?
	// ResetOneRccRegister(0x4002380c);
	//__builtin_arm_dsb(0xf);
	//uint32_t volatile* syscfg = (uint32_t volatile*)0x40013800;
	// set to flash
	//*syscfg &= ~1;
	__builtin_arm_isb(0xf);
	__builtin_arm_dsb(0xf);
	
	// point interrupt handler to DFU bootrom
	//uint32_t new_entry = 0x1FF00000;
	// actually the flash rom's fine
	uint32_t new_entry = 0x08000000;
	uint32_t* new_entry_header = (uint32_t*)new_entry;
	// set stack, then jump
	__builtin_arm_wsr("msp", new_entry_header[0]);

	((void (*)(void))new_entry_header[1])();
	while (1) {asm volatile("");}
}

// Yes I know I can just call an OS call, but EH
static void rebootToBootMode() {
	__builtin_arm_dsb(0xf);
	// https://github.com/STMicroelectronics/STM32CubeF7/blob/c7c5ec99c7482ea8bcdbf0a869c930af4547088f/Drivers/CMSIS/Core/Include/core_cm7.h#L2147
	// 0x0801678a in the firmware
	uint32_t volatile* boot_reason = (uint32_t volatile*)0x40024000;
	// 0: regular boot
	// 1: Disk, BOOT
	// 2: Disk, DATA
	// 3: no splash return to launcher
	// 0xda: Disk, SYSTEM
	*boot_reason = 0xda;
	uint32_t volatile* nvic_aircr = (uint32_t volatile*)0xe000ed0c;
	*nvic_aircr = (*nvic_aircr & 0x700) | 0x05FA0004;
	while (1) {__asm volatile("");}
}

static int ReturnFortyTwo() {
	return 42;
}

static void ReadLinuxImage(PlaydateAPI* pd) {
	{
	void* targetAddr = (void*)0x60008000;
		SDFile* file = pd->file->open("Image", kFileRead | kFileReadData);
		if (!file) {
			pd->system->error("Can't open Image!");
			return;
		}
		pd->file->seek(file, 0, SEEK_END);
		int len = pd->file->tell(file);
		pd->file->seek(file, 0, SEEK_SET);
		pd->file->read(file, targetAddr, len);
		pd->file->close(file);
	}
	{
	void* targetAddr = (void*)0x60ff0000;
#define DTB_NAME "stm32f746-disco.dtb"
		SDFile* file = pd->file->open(DTB_NAME, kFileRead | kFileReadData);
		if (!file) {
			pd->system->error("Can't open " DTB_NAME "!");
			return;
		}
		pd->file->seek(file, 0, SEEK_END);
		int len = pd->file->tell(file);
		pd->file->seek(file, 0, SEEK_SET);
		pd->file->read(file, targetAddr, len);
		pd->file->close(file);
	}
	uint32_t initrd_length = 0;
	{
	void* targetAddr = (void*)0x60f00000;
#define INITRD_NAME "rootfs.cpio.gz"
		SDFile* file = pd->file->open(INITRD_NAME, kFileRead | kFileReadData);
		if (!file) {
			pd->system->error("Can't open " INITRD_NAME "!");
			return;
		}
		pd->file->seek(file, 0, SEEK_END);
		int len = pd->file->tell(file);
		if (len > 0xf0000) {
			pd->system->error("Initrd too large; needs to be < 0xf0000!");
			return;
		}
		pd->file->seek(file, 0, SEEK_SET);
		pd->file->read(file, targetAddr, len);
		pd->file->close(file);
		initrd_length = len;
	}
	for (uint32_t* a = (uint32_t*)0x60ff0000; a < (uint32_t*)(0x60ff0000 + 0x10000); a++) {
		uint32_t v = __builtin_bswap32(*a);
		if (v == 0xdeadbeee) {
			*a = __builtin_bswap32(0x60f00000);
		} else if (v == 0xdeadbeef) {
			*a = __builtin_bswap32(0x60f00000 + initrd_length);
			break;
		}
	}
}
#endif

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI* pd, PDSystemEvent event, uint32_t arg)
{
	(void)arg; // arg is currently only used for event = kEventKeyPressed

	if ( event == kEventInit )
	{
		const char* err;
		font = pd->graphics->loadFont(fontpath, &err);
		
		if ( font == NULL )
			pd->system->error("%s:%i Couldn't load font %s: %s", __FILE__, __LINE__, fontpath, err);

		// Note: If you set an update callback in the kEventInit handler, the system assumes the game is pure C and doesn't run any Lua code in the game
		pd->system->setUpdateCallback(update, pd);
#ifdef TARGET_PLAYDATE
		ReadLinuxImage(pd);
		raisePrivsHard();
		//tryResetToDfu();
	// caches off
	// https://github.com/STMicroelectronics/STM32CubeF7/blob/c7c5ec99c7482ea8bcdbf0a869c930af4547088f/Drivers/CMSIS/Core/Include/core_cm7.h#L2248
	__builtin_arm_dsb(0xf);
	__builtin_arm_isb(0xf);
	uint32_t volatile* ccr = (uint32_t volatile*)0xE000ED14;
	// disable icache and dcache
	*ccr = *ccr & ~((1 << 16) | (1 << 17));
	uint32_t volatile* iciallu = (uint32_t volatile*)0xE000EF50;
	*iciallu = 0;

	__builtin_arm_dsb(0xf);
	__builtin_arm_isb(0xf);

	uint32_t volatile* mpu_ctrl = (uint32_t volatile*)0xE000ED94;
	uint32_t saved_mpu_ctrl = *mpu_ctrl;
	*mpu_ctrl = 0;
	__builtin_arm_dsb(0xf);
	__builtin_arm_isb(0xf);


	tryResetToDfu();
#if 0
	char* target_sram = (void*)0x20000000;
	int (*TryResetToDfuPart2_sram)(void) = (int (*)(void))target_sram+1;
	int returned = TryResetToDfuPart2_sram();
#endif


#if 0

	

	uint32_t volatile* mpu_ctrl = (uint32_t volatile*)0xE000ED94;
	uint32_t saved_mpu_ctrl = *mpu_ctrl;
	//*mpu_ctrl = 0;
	__builtin_arm_dsb(0xf);
	__builtin_arm_isb(0xf);

		// poke a byte into dtcm and see what happens
	char* target_sram = (void*)0x20000000;
	// TODO(zhuowei): size
	__builtin_memcpy(target_sram, &ReturnFortyTwo, 0x4);
	__builtin_arm_dsb(0xf);
	__builtin_arm_isb(0xf);
	int (*TryResetToDfuPart2_sram)(void) = (int (*)(void))target_sram+1;
	int returned = TryResetToDfuPart2_sram();
	*mpu_ctrl = saved_mpu_ctrl;
	__builtin_arm_dsb(0xf);
	__builtin_arm_isb(0xf);
	if (returned != 42)
			pd->system->error("not 42");
#endif


#endif
#if 0
		SDFile* file = pd->file->open("sram.bin", kFileWrite);
		//pd->file->write(file, (void*)0x8000000, 0x100000);
		pd->file->write(file, (void*)0x20000000, 0x50000);
		pd->file->close(file);
#endif
	}
	
	return 0;
}


#define TEXT_WIDTH 86
#define TEXT_HEIGHT 16

int x = (400-TEXT_WIDTH)/2;
int y = (240-TEXT_HEIGHT)/2;
int dx = 1;
int dy = 2;

static int update(void* userdata)
{
	PlaydateAPI* pd = userdata;
	
	pd->graphics->clear(kColorWhite);
	pd->graphics->setFont(font);
	pd->graphics->drawText("Hello World!", strlen("Hello World!"), kASCIIEncoding, x, y);

	x += dx;
	y += dy;
	
	if ( x < 0 || x > LCD_COLUMNS - TEXT_WIDTH )
		dx = -dx;
	
	if ( y < 0 || y > LCD_ROWS - TEXT_HEIGHT )
		dy = -dy;
        
	pd->system->drawFPS(0,0);

	return 1;
}

