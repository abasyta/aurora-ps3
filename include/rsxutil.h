/*
 * rsxutil.h - screen/buffer helpers for Aurora Coverflow PS3
 * Derived from the PSL1GHT cairo sample (GPLv3, (C) Youness Alaoui (KaKaRoTo))
 */

#ifndef __RSXUTIL_H__
#define __RSXUTIL_H__

#include <ppu-types.h>
#include <rsx/rsx.h>

#define CB_SIZE		0x100000
#define HOST_SIZE	(32*1024*1024)

typedef struct
{
	int height;
	int width;
	int id;
	uint32_t *ptr;
	/* internal */
	uint32_t offset;
} rsxBuffer;

/* Block the PPU thread until the previous flip operation has finished. */
void waitFlip(void);

/* Flip a buffer onto the screen. Returns TRUE on success. */
int flip(gcmContextData *context, s32 buffer);

/* Create a buffer to draw into and assign it to @id. Returns NULL on error */
int makeBuffer(rsxBuffer *buffer, u16 width, u16 height, int id);

/* Get current screen resolution. Returns TRUE on success. */
int getResolution(u16 *width, u16 *height);

/* Initialize the RSX properly. Returns NULL on error. */
gcmContextData *initScreen(void *host_addr, u32 size);

/* Sets the target buffer to render to. */
void setRenderTarget(gcmContextData *context, rsxBuffer *buffer);

#endif /* __RSXUTIL_H__ */