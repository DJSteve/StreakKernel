/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "yportenv.h"
#include "yaffs_trace.h"

#include "yaffsinterface.h"
#include "yaffs_guts.h"
#include "yaffs_tagsvalidity.h"
#include "yaffs_getblockinfo.h"

#include "yaffs_tagscompat.h"

#include "yaffs_nand.h"

#include "yaffs_yaffs1.h"
#include "yaffs_yaffs2.h"
#include "yaffs_bitmap.h"
#include "yaffs_verify.h"

#include "yaffs_nand.h"
#include "yaffs_packedtags2.h"

#include "yaffs_nameval.h"
#include "yaffs_allocator.h"


#define YAFFS_GC_GOOD_ENOUGH 2
#define YAFFS_GC_PASSIVE_THRESHOLD 4

#include "yaffs_ecc.h"



/* Robustification (if it ever comes about...) */
static void yaffs_RetireBlock(yaffs_Device *dev, int blockInNAND);
static void yaffs_HandleWriteChunkError(yaffs_Device *dev, int chunkInNAND,
		int erasedOk);
static void yaffs_HandleWriteChunkOk(yaffs_Device *dev, int chunkInNAND,
				const __u8 *data,
				const yaffs_ExtendedTags *tags);
static void yaffs_HandleUpdateChunk(yaffs_Device *dev, int chunkInNAND,
				const yaffs_ExtendedTags *tags);

/* Other local prototypes */
static void yaffs_UpdateParent(yaffs_Object *obj);
static int yaffs_UnlinkObject(yaffs_Object *obj);
static int yaffs_ObjectHasCachedWriteData(yaffs_Object *obj);

static int yaffs_WriteNewChunkWithTagsToNAND(yaffs_Device *dev,
					const __u8 *buffer,
					yaffs_ExtendedTags *tags,
					int useReserve);


static yaffs_Object *yaffs_CreateNewObject(yaffs_Device *dev, int number,
					yaffs_ObjectType type);


static int yaffs_ApplyXMod(yaffs_Object *obj, char *buffer, yaffs_XAttrMod *xmod);

static void yaffs_RemoveObjectFromDirectory(yaffs_Object *obj);
static int yaffs_CheckStructures(void);
static int yaffs_DoGenericObjectDeletion(yaffs_Object *in);

static int yaffs_CheckChunkErased(struct yaffs_DeviceStruct *dev,
				int chunkInNAND);

static int yaffs_UnlinkWorker(yaffs_Object *obj);

static int yaffs_TagsMatch(const yaffs_ExtendedTags *tags, int objectId,
			int chunkInObject);

static int yaffs_AllocateChunk(yaffs_Device *dev, int useReserve,
				yaffs_BlockInfo **blockUsedPtr);

static void yaffs_CheckObjectDetailsLoaded(yaffs_Object *in);

static void yaffs_InvalidateWholeChunkCache(yaffs_Object *in);
static void yaffs_InvalidateChunkCache(yaffs_Object *object, int chunkId);

static int yaffs_FindChunkInFile(yaffs_Object *in, int chunkInInode,
				yaffs_ExtendedTags *tags);

static int yaffs_VerifyChunkWritten(yaffs_Device *dev,
					int chunkInNAND,
					const __u8 *data,
					yaffs_ExtendedTags *tags);


static void yaffs_LoadNameFromObjectHeader(yaffs_Device *dev,YCHAR *name, const YCHAR *ohName, int bufferSize);
static void yaffs_LoadObjectHeaderFromName(yaffs_Device *dev,YCHAR *ohName, const YCHAR *name);


/* Function to calculate chunk and offset */

static void yaffs_AddrToChunk(yaffs_Device *dev, loff_t addr, int *chunkOut,
		__u32 *offsetOut)
{
	int chunk;
	__u32 offset;

	chunk  = (__u32)(addr >> dev->chunkShift);

	if (dev->chunkDiv == 1) {
		/* easy power of 2 case */
		offset = (__u32)(addr & dev->chunkMask);
	} else {
		/* Non power-of-2 case */

		loff_t chunkBase;

		chunk /= dev->chunkDiv;

		chunkBase = ((loff_t)chunk) * dev->nDataBytesPerChunk;
		offset = (__u32)(addr - chunkBase);
	}

	*chunkOut = chunk;
	*offsetOut = offset;
}

/* Function to return the number of shifts for a power of 2 greater than or
 * equal to the given number
 * Note we don't try to cater for all possible numbers and this does not have to
 * be hellishly efficient.
 */

static __u32 ShiftsGE(__u32 x)
{
	int extraBits;
	int nShifts;

	nShifts = extraBits = 0;

	while (x > 1) {
		if (x & 1)
			extraBits++;
		x >>= 1;
		nShifts++;
	}

	if (extraBits)
		nShifts++;

	return nShifts;
}

/* Function to return the number of shifts to get a 1 in bit 0
 */

static __u32 Shifts(__u32 x)
{
	__u32 nShifts;

	nShifts =  0;

	if (!x)
		return 0;

	while (!(x&1)) {
		x >>= 1;
		nShifts++;
	}

	return nShifts;
}



/*
 * Temporary buffer manipulations.
 */

static int yaffs_InitialiseTempBuffers(yaffs_Device *dev)
{
	int i;
	__u8 *buf = (__u8 *)1;

	memset(dev->tempBuffer, 0, sizeof(dev->tempBuffer));

	for (i = 0; buf && i < YAFFS_N_TEMP_BUFFERS; i++) {
		dev->tempBuffer[i].line = 0;	/* not in use */
		dev->tempBuffer[i].buffer = buf =
		    YMALLOC_DMA(dev->param.totalBytesPerChunk);
	}

	return buf ? YAFFS_OK : YAFFS_FAIL;
}

__u8 *yaffs_GetTempBuffer(yaffs_Device *dev, int lineNo)
{
	int i, j;

	dev->tempInUse++;
	if (dev->tempInUse > dev->maxTemp)
		dev->maxTemp = dev->tempInUse;

	for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++) {
		if (dev->tempBuffer[i].line == 0) {
			dev->tempBuffer[i].line = lineNo;
			if ((i + 1) > dev->maxTemp) {
				dev->maxTemp = i + 1;
				for (j = 0; j <= i; j++)
					dev->tempBuffer[j].maxLine =
					    dev->tempBuffer[j].line;
			}

			return dev->tempBuffer[i].buffer;
		}
	}

	T(YAFFS_TRACE_BUFFERS,
	  (TSTR("Out of temp buffers at line %d, other held by lines:"),
	   lineNo));
	for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++)
		T(YAFFS_TRACE_BUFFERS, (TSTR(" %d "), dev->tempBuffer[i].line));

	T(YAFFS_TRACE_BUFFERS, (TSTR(" " TENDSTR)));

	/*
	 * If we got here then we have to allocate an unmanaged one
	 * This is not good.
	 */

	dev->unmanagedTempAllocations++;
	return YMALLOC(dev->nDataBytesPerChunk);

}

void yaffs_ReleaseTempBuffer(yaffs_Device *dev, __u8 *buffer,
				    int lineNo)
{
	int i;

	dev->tempInUse--;

	for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++) {
		if (dev->tempBuffer[i].buffer == buffer) {
			dev->tempBuffer[i].line = 0;
			return;
		}
	}

	if (buffer) {
		/* assume it is an unmanaged one. */
		T(YAFFS_TRACE_BUFFERS,
		  (TSTR("Releasing unmanaged temp buffer in line %d" TENDSTR),
		   lineNo));
		YFREE(buffer);
		dev->unmanagedTempDeallocations++;
	}

}

/*
 * Determine if we have a managed buffer.
 */
int yaffs_IsManagedTempBuffer(yaffs_Device *dev, const __u8 *buffer)
{
	int i;

	for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++) {
		if (dev->tempBuffer[i].buffer == buffer)
			return 1;
	}

	for (i = 0; i < dev->param.nShortOpCaches; i++) {
		if (dev->srCache[i].data == buffer)
			return 1;
	}

	if (buffer == dev->checkpointBuffer)
		return 1;

	T(YAFFS_TRACE_ALWAYS,
		(TSTR("yaffs: unmaged buffer detected.\n" TENDSTR)));
	return 0;
}

/*
 * Verification code
 */






static Y_INLINE int yaffs_HashFunction(int n)
{
	n = abs(n);
	return n % YAFFS_NOBJECT_BUCKETS;
}



yaffs_Object *yaffs_Root(yaffs_Device *dev)
{
	return dev->rootDir;
}

yaffs_Object *yaffs_LostNFound(yaffs_Device *dev)
{
	return dev->lostNFoundDir;
}


/*
 *  Erased NAND checking functions
 */

int yaffs_CheckFF(__u8 *buffer, int nBytes)
{
	
	while (nBytes--) {
		if (*buffer != 0xFF)
			return 0;
		buffer++;
	}
	return 1;
}

static int yaffs_CheckChunkErased(struct yaffs_DeviceStruct *dev,
				int chunkInNAND)
{
	int retval = YAFFS_OK;
	__u8 *data = yaffs_GetTempBuffer(dev, __LINE__);
	yaffs_ExtendedTags tags;
	int result;

	result = yaffs_ReadChunkWithTagsFromNAND(dev, chunkInNAND, data, &tags);

	if (tags.eccResult > YAFFS_ECC_RESULT_NO_ERROR)
		retval = YAFFS_FAIL;

	if (!yaffs_CheckFF(data, dev->nDataBytesPerChunk) || tags.chunkUsed) {
		T(YAFFS_TRACE_NANDACCESS,
		  (TSTR("Chunk %d not erased" TENDSTR), chunkInNAND));
		retval = YAFFS_FAIL;
	}

	yaffs_ReleaseTempBuffer(dev, data, __LINE__);

	return retval;

}


static int yaffs_VerifyChunkWritten(yaffs_Device *dev,
					int chunkInNAND,
					const __u8 *data,
					yaffs_ExtendedTags *tags)
{
	int retval = YAFFS_OK;
	yaffs_ExtendedTags tempTags;
	__u8 *buffer = yaffs_GetTempBuffer(dev,__LINE__);
	int result;
	
	result = yaffs_ReadChunkWithTagsFromNAND(dev,chunkInNAND,buffer,&tempTags);
	if(memcmp(buffer,data,dev->nDataBytesPerChunk) ||
		tempTags.objectId != tags->objectId ||
		tempTags.chunkId  != tags->chunkId ||
		tempTags.byteCount != tags->byteCount)
		retval = YAFFS_FAIL;

	yaffs_ReleaseTempBuffer(dev, buffer, __LINE__);

	return retval;
}

static int yaffs_WriteNewChunkWithTagsToNAND(struct yaffs_DeviceStruct *dev,
					const __u8 *data,
					yaffs_ExtendedTags *tags,
					int useReserve)
{
	int attempts = 0;
	int writeOk = 0;
	int chunk;

	yaffs2_InvalidateCheckpoint(dev);

	do {
		yaffs_BlockInfo *bi = 0;
		int erasedOk = 0;

		chunk = yaffs_AllocateChunk(dev, useReserve, &bi);
		if (chunk < 0) {
			
			break;
		}

		

		
		attempts++;

		if(dev->param.alwaysCheckErased)
			bi->skipErasedCheck = 0;

		if (!bi->skipErasedCheck) {
			erasedOk = yaffs_CheckChunkErased(dev, chunk);
			if (erasedOk != YAFFS_OK) {
				T(YAFFS_TRACE_ERROR,
				(TSTR("**>> yaffs chunk %d was not erased"
				TENDSTR), chunk));

				
				 yaffs_DeleteChunk(dev,chunk,1,__LINE__);
				 yaffs_SkipRestOfBlock(dev);
				continue;
			}
		}

		writeOk = yaffs_WriteChunkWithTagsToNAND(dev, chunk,
				data, tags);

		if(!bi->skipErasedCheck)
			writeOk = yaffs_VerifyChunkWritten(dev, chunk, data, tags);

		if (writeOk != YAFFS_OK) {
			
			yaffs_HandleWriteChunkError(dev, chunk, erasedOk);
			continue;
		}

		bi->skipErasedCheck = 1;

		
		yaffs_HandleWriteChunkOk(dev, chunk, data, tags);

	} while (writeOk != YAFFS_OK &&
		(yaffs_wr_attempts <= 0 || attempts <= yaffs_wr_attempts));

	if (!writeOk)
		chunk = -1;

	if (attempts > 1) {
		T(YAFFS_TRACE_ERROR,
			(TSTR("**>> yaffs write required %d attempts" TENDSTR),
			attempts));

		dev->nRetriedWrites += (attempts - 1);
	}

	return chunk;
}


 


static void yaffs_RetireBlock(yaffs_Device *dev, int blockInNAND)
{
	yaffs_BlockInfo *bi = yaffs_GetBlockInfo(dev, blockInNAND);

	yaffs2_InvalidateCheckpoint(dev);
	
	yaffs2_ClearOldestDirtySequence(dev,bi);

	if (yaffs_MarkBlockBad(dev, blockInNAND) != YAFFS_OK) {
		if (yaffs_EraseBlockInNAND(dev, blockInNAND) != YAFFS_OK) {
			T(YAFFS_TRACE_ALWAYS, (TSTR(
				"yaffs: Failed to mark bad and erase block %d"
				TENDSTR), blockInNAND));
		} else {
			yaffs_ExtendedTags tags;
			int chunkId = blockInNAND * dev->param.nChunksPerBlock;

			__u8 *buffer = yaffs_GetTempBuffer(dev, __LINE__);

			memset(buffer, 0xff, dev->nDataBytesPerChunk);
			yaffs_InitialiseTags(&tags);
			tags.sequenceNumber = YAFFS_SEQUENCE_BAD_BLOCK;
			if (dev->param.writeChunkWithTagsToNAND(dev, chunkId -
				dev->chunkOffset, buffer, &tags) != YAFFS_OK)
				T(YAFFS_TRACE_ALWAYS, (TSTR("yaffs: Failed to "
					TCONT("write bad block marker to block %d")
					TENDSTR), blockInNAND));

			yaffs_ReleaseTempBuffer(dev, buffer, __LINE__);
		}
	}

	bi->blockState = YAFFS_BLOCK_STATE_DEAD;
	bi->gcPrioritise = 0;
	bi->needsRetiring = 0;

	dev->nRetiredBlocks++;
}



static void yaffs_HandleWriteChunkOk(yaffs_Device *dev, int chunkInNAND,
				const __u8 *data,
				const yaffs_ExtendedTags *tags)
{
	dev=dev;
	chunkInNAND=chunkInNAND;
	data=data;
	tags=tags;
}

static void yaffs_HandleUpdateChunk(yaffs_Device *dev, int chunkInNAND,
				const yaffs_ExtendedTags *tags)
{
	dev=dev;
	chunkInNAND=chunkInNAND;
	tags=tags;
}

void yaffs_HandleChunkError(yaffs_Device *dev, yaffs_BlockInfo *bi)
{
	if (!bi->gcPrioritise) {
		bi->gcPrioritise = 1;
		dev->hasPendingPrioritisedGCs = 1;
		bi->chunkErrorStrikes++;

		if (bi->chunkErrorStrikes > 3) {
			bi->needsRetiring = 1; 
			T(YAFFS_TRACE_ALWAYS, (TSTR("yaffs: Block struck out" TENDSTR)));

		}
	}
}

static void yaffs_HandleWriteChunkError(yaffs_Device *dev, int chunkInNAND,
		int erasedOk)
{
	int blockInNAND = chunkInNAND / dev->param.nChunksPerBlock;
	yaffs_BlockInfo *bi = yaffs_GetBlockInfo(dev, blockInNAND);

	yaffs_HandleChunkError(dev, bi);

	if (erasedOk) {
		
		bi->needsRetiring = 1;
		T(YAFFS_TRACE_ERROR | YAFFS_TRACE_BAD_BLOCKS,
		  (TSTR("**>> Block %d needs retiring" TENDSTR), blockInNAND));
	}

	
	yaffs_DeleteChunk(dev, chunkInNAND, 1, __LINE__);
	yaffs_SkipRestOfBlock(dev);
}




static __u16 yaffs_CalcNameSum(const YCHAR *name)
{
	__u16 sum = 0;
	__u16 i = 1;

	const YUCHAR *bname = (const YUCHAR *) name;
	if (bname) {
		while ((*bname) && (i < (YAFFS_MAX_NAME_LENGTH/2))) {

#ifdef CONFIG_YAFFS_CASE_INSENSITIVE
			sum += yaffs_toupper(*bname) * i;
#else
			sum += (*bname) * i;
#endif
			i++;
			bname++;
		}
	}
	return sum;
}

void yaffs_SetObjectName(yaffs_Object *obj, const YCHAR *name)
{
#ifdef CONFIG_YAFFS_SHORT_NAMES_IN_RAM
	memset(obj->shortName, 0, sizeof(YCHAR) * (YAFFS_SHORT_NAME_LENGTH+1));
	if (name && yaffs_strnlen(name,YAFFS_SHORT_NAME_LENGTH+1) <= YAFFS_SHORT_NAME_LENGTH)
		yaffs_strcpy(obj->shortName, name);
	else
		obj->shortName[0] = _Y('\0');
#endif
	obj->sum = yaffs_CalcNameSum(name);
}

void yaffs_SetObjectNameFromOH(yaffs_Object *obj, const yaffs_ObjectHeader *oh)
{
#ifdef CONFIG_YAFFS_AUTO_UNICODE
	YCHAR tmpName[YAFFS_MAX_NAME_LENGTH+1];
	memset(tmpName,0,sizeof(tmpName));
	yaffs_LoadNameFromObjectHeader(obj->myDev,tmpName,oh->name,YAFFS_MAX_NAME_LENGTH+1);
	yaffs_SetObjectName(obj,tmpName);
#else
	yaffs_SetObjectName(obj,oh->name);
#endif
}




yaffs_Tnode *yaffs_GetTnode(yaffs_Device *dev)
{
	yaffs_Tnode *tn = yaffs_AllocateRawTnode(dev);
	if (tn){
		memset(tn, 0, dev->tnodeSize);
		dev->nTnodes++;
	}

	dev->nCheckpointBlocksRequired = 0; 

	return tn;
}


static void yaffs_FreeTnode(yaffs_Device *dev, yaffs_Tnode *tn)
{
	yaffs_FreeRawTnode(dev,tn);
	dev->nTnodes--;
	dev->nCheckpointBlocksRequired = 0; 
}

static void yaffs_DeinitialiseTnodesAndObjects(yaffs_Device *dev)
{
	yaffs_DeinitialiseRawTnodesAndObjects(dev);
	dev->nObjects = 0;
	dev->nTnodes = 0;
}


void yaffs_LoadLevel0Tnode(yaffs_Device *dev, yaffs_Tnode *tn, unsigned pos,
		unsigned val)
{
	__u32 *map = (__u32 *)tn;
	__u32 bitInMap;
	__u32 bitInWord;
	__u32 wordInMap;
	__u32 mask;

	pos &= YAFFS_TNODES_LEVEL0_MASK;
	val >>= dev->chunkGroupBits;

	bitInMap = pos * dev->tnodeWidth;
	wordInMap = bitInMap / 32;
	bitInWord = bitInMap & (32 - 1);

	mask = dev->tnodeMask << bitInWord;

	map[wordInMap] &= ~mask;
	map[wordInMap] |= (mask & (val << bitInWord));

	if (dev->tnodeWidth > (32 - bitInWord)) {
		bitInWord = (32 - bitInWord);
		wordInMap++;;
		mask = dev->tnodeMask >> ( bitInWord);
		map[wordInMap] &= ~mask;
		map[wordInMap] |= (mask & (val >> bitInWord));
	}
}

__u32 yaffs_GetChunkGroupBase(yaffs_Device *dev, yaffs_Tnode *tn,
		unsigned pos)
{
	__u32 *map = (__u32 *)tn;
	__u32 bitInMap;
	__u32 bitInWord;
	__u32 wordInMap;
	__u32 val;

	pos &= YAFFS_TNODES_LEVEL0_MASK;

	bitInMap = pos * dev->tnodeWidth;
	wordInMap = bitInMap / 32;
	bitInWord = bitInMap & (32 - 1);

	val = map[wordInMap] >> bitInWord;

	if	(dev->tnodeWidth > (32 - bitInWord)) {
		bitInWord = (32 - bitInWord);
		wordInMap++;;
		val |= (map[wordInMap] << bitInWord);
	}

	val &= dev->tnodeMask;
	val <<= dev->chunkGroupBits;

	return val;
}






yaffs_Tnode *yaffs_FindLevel0Tnode(yaffs_Device *dev,
					yaffs_FileStructure *fStruct,
					__u32 chunkId)
{
	yaffs_Tnode *tn = fStruct->top;
	__u32 i;
	int requiredTallness;
	int level = fStruct->topLevel;

	dev=dev;

	
	if (level < 0 || level > YAFFS_TNODES_MAX_LEVEL)
		return NULL;

	if (chunkId > YAFFS_MAX_CHUNK_ID)
		return NULL;

	

	i = chunkId >> YAFFS_TNODES_LEVEL0_BITS;
	requiredTallness = 0;
	while (i) {
		i >>= YAFFS_TNODES_INTERNAL_BITS;
		requiredTallness++;
	}

	if (requiredTallness > fStruct->topLevel)
		return NULL; 

	
	while (level > 0 && tn) {
		tn = tn->internal[(chunkId >>
			(YAFFS_TNODES_LEVEL0_BITS +
				(level - 1) *
				YAFFS_TNODES_INTERNAL_BITS)) &
			YAFFS_TNODES_INTERNAL_MASK];
		level--;
	}

	return tn;
}

/* AddOrFindLevel0Tnode finds the level 0 tnode if it exists, otherwise first expands the tree.
 * This happens in two steps:
 *  1. If the tree isn't tall enough, then make it taller.
 *  2. Scan down the tree towards the level 0 tnode adding tnodes if required.
 *
 * Used when modifying the tree.
 *
 *  If the tn argument is NULL, then a fresh tnode will be added otherwise the specified tn will
 *  be plugged into the ttree.
 */

yaffs_Tnode *yaffs_AddOrFindLevel0Tnode(yaffs_Device *dev,
					yaffs_FileStructure *fStruct,
					__u32 chunkId,
					yaffs_Tnode *passedTn)
{
	int requiredTallness;
	int i;
	int l;
	yaffs_Tnode *tn;

	__u32 x;


	
	if (fStruct->topLevel < 0 || fStruct->topLevel > YAFFS_TNODES_MAX_LEVEL)
		return NULL;

	if (chunkId > YAFFS_MAX_CHUNK_ID)
		return NULL;

	

	x = chunkId >> YAFFS_TNODES_LEVEL0_BITS;
	requiredTallness = 0;
	while (x) {
		x >>= YAFFS_TNODES_INTERNAL_BITS;
		requiredTallness++;
	}


	if (requiredTallness > fStruct->topLevel) {
		
		for (i = fStruct->topLevel; i < requiredTallness; i++) {

			tn = yaffs_GetTnode(dev);

			if (tn) {
				tn->internal[0] = fStruct->top;
				fStruct->top = tn;
				fStruct->topLevel++;
			} else {
				T(YAFFS_TRACE_ERROR,
					(TSTR("yaffs: no more tnodes" TENDSTR)));
				return NULL;
			}
		}
	}

	

	l = fStruct->topLevel;
	tn = fStruct->top;

	if (l > 0) {
		while (l > 0 && tn) {
			x = (chunkId >>
			     (YAFFS_TNODES_LEVEL0_BITS +
			      (l - 1) * YAFFS_TNODES_INTERNAL_BITS)) &
			    YAFFS_TNODES_INTERNAL_MASK;


			if ((l > 1) && !tn->internal[x]) {
				
				tn->internal[x] = yaffs_GetTnode(dev);
				if(!tn->internal[x])
					return NULL;
			} else if (l == 1) {
				
				if (passedTn) {
					
					if (tn->internal[x])
						yaffs_FreeTnode(dev, tn->internal[x]);
					tn->internal[x] = passedTn;

				} else if (!tn->internal[x]) {
					
					tn->internal[x] = yaffs_GetTnode(dev);
					if(!tn->internal[x])
						return NULL;
				}
			}

			tn = tn->internal[x];
			l--;
		}
	} else {
		
		if (passedTn) {
			memcpy(tn, passedTn, (dev->tnodeWidth * YAFFS_NTNODES_LEVEL0)/8);
			yaffs_FreeTnode(dev, passedTn);
		}
	}

	return tn;
}

static int yaffs_FindChunkInGroup(yaffs_Device *dev, int theChunk,
				yaffs_ExtendedTags *tags, int objectId,
				int chunkInInode)
{
	int j;

	for (j = 0; theChunk && j < dev->chunkGroupSize; j++) {
		if (yaffs_CheckChunkBit(dev, theChunk / dev->param.nChunksPerBlock,
				theChunk % dev->param.nChunksPerBlock)) {
			
			if(dev->chunkGroupSize == 1)
				return theChunk;
			else {
				yaffs_ReadChunkWithTagsFromNAND(dev, theChunk, NULL,
								tags);
				if (yaffs_TagsMatch(tags, objectId, chunkInInode)) {
					
					return theChunk;
				}
			}
		}
		theChunk++;
	}
	return -1;
}

#if 0



static int yaffs_DeleteWorker(yaffs_Object *in, yaffs_Tnode *tn, __u32 level,
			      int chunkOffset, int *limit)
{
	int i;
	int chunkInInode;
	int theChunk;
	yaffs_ExtendedTags tags;
	int foundChunk;
	yaffs_Device *dev = in->myDev;

	int allDone = 1;

	if (tn) {
		if (level > 0) {
			for (i = YAFFS_NTNODES_INTERNAL - 1; allDone && i >= 0;
			     i--) {
				if (tn->internal[i]) {
					if (limit && (*limit) < 0) {
						allDone = 0;
					} else {
						allDone =
							yaffs_DeleteWorker(in,
								tn->
								internal
								[i],
								level -
								1,
								(chunkOffset
									<<
									YAFFS_TNODES_INTERNAL_BITS)
								+ i,
								limit);
					}
					if (allDone) {
						yaffs_FreeTnode(dev,
								tn->
								internal[i]);
						tn->internal[i] = NULL;
					}
				}
			}
			return (allDone) ? 1 : 0;
		} else if (level == 0) {
			int hitLimit = 0;

			for (i = YAFFS_NTNODES_LEVEL0 - 1; i >= 0 && !hitLimit;
					i--) {
				theChunk = yaffs_GetChunkGroupBase(dev, tn, i);
				if (theChunk) {

					chunkInInode = (chunkOffset <<
						YAFFS_TNODES_LEVEL0_BITS) + i;

					foundChunk =
						yaffs_FindChunkInGroup(dev,
								theChunk,
								&tags,
								in->objectId,
								chunkInInode);

					if (foundChunk > 0) {
						yaffs_DeleteChunk(dev,
								  foundChunk, 1,
								  __LINE__);
						in->nDataChunks--;
						if (limit) {
							*limit = *limit - 1;
							if (*limit <= 0)
								hitLimit = 1;
						}

					}

					yaffs_LoadLevel0Tnode(dev, tn, i, 0);
				}

			}
			return (i < 0) ? 1 : 0;

		}

	}

	return 1;

}

#endif

static void yaffs_SoftDeleteChunk(yaffs_Device *dev, int chunk)
{
	yaffs_BlockInfo *theBlock;
	unsigned blockNo;

	T(YAFFS_TRACE_DELETION, (TSTR("soft delete chunk %d" TENDSTR), chunk));

	blockNo =  chunk / dev->param.nChunksPerBlock;
	theBlock = yaffs_GetBlockInfo(dev, blockNo);
	if (theBlock) {
		theBlock->softDeletions++;
		dev->nFreeChunks++;
		yaffs2_UpdateOldestDirtySequence(dev, blockNo, theBlock);
	}
}

/* SoftDeleteWorker scans backwards through the tnode tree and soft deletes all the chunks in the file.
 * All soft deleting does is increment the block's softdelete count and pulls the chunk out
 * of the tnode.
 * Thus, essentially this is the same as DeleteWorker except that the chunks are soft deleted.
 */

static int yaffs_SoftDeleteWorker(yaffs_Object *in, yaffs_Tnode *tn,
				  __u32 level, int chunkOffset)
{
	int i;
	int theChunk;
	int allDone = 1;
	yaffs_Device *dev = in->myDev;

	if (tn) {
		if (level > 0) {

			for (i = YAFFS_NTNODES_INTERNAL - 1; allDone && i >= 0;
			     i--) {
				if (tn->internal[i]) {
					allDone =
					    yaffs_SoftDeleteWorker(in,
								   tn->
								   internal[i],
								   level - 1,
								   (chunkOffset
								    <<
								    YAFFS_TNODES_INTERNAL_BITS)
								   + i);
					if (allDone) {
						yaffs_FreeTnode(dev,
								tn->
								internal[i]);
						tn->internal[i] = NULL;
					} else {
						
					}
				}
			}
			return (allDone) ? 1 : 0;
		} else if (level == 0) {

			for (i = YAFFS_NTNODES_LEVEL0 - 1; i >= 0; i--) {
				theChunk = yaffs_GetChunkGroupBase(dev, tn, i);
				if (theChunk) {
					/* Note this does not find the real chunk, only the chunk group.
					 * We make an assumption that a chunk group is not larger than
					 * a block.
					 */
					yaffs_SoftDeleteChunk(dev, theChunk);
					yaffs_LoadLevel0Tnode(dev, tn, i, 0);
				}

			}
			return 1;

		}

	}

	return 1;

}

static void yaffs_SoftDeleteFile(yaffs_Object *obj)
{
	if (obj->deleted &&
	    obj->variantType == YAFFS_OBJECT_TYPE_FILE && !obj->softDeleted) {
		if (obj->nDataChunks <= 0) {
			
			yaffs_FreeTnode(obj->myDev,
					obj->variant.fileVariant.top);
			obj->variant.fileVariant.top = NULL;
			T(YAFFS_TRACE_TRACING,
			  (TSTR("yaffs: Deleting empty file %d" TENDSTR),
			   obj->objectId));
			yaffs_DoGenericObjectDeletion(obj);
		} else {
			yaffs_SoftDeleteWorker(obj,
					       obj->variant.fileVariant.top,
					       obj->variant.fileVariant.
					       topLevel, 0);
			obj->softDeleted = 1;
		}
	}
}



static yaffs_Tnode *yaffs_PruneWorker(yaffs_Device *dev, yaffs_Tnode *tn,
				__u32 level, int del0)
{
	int i;
	int hasData;

	if (tn) {
		hasData = 0;

		if(level > 0){
			for (i = 0; i < YAFFS_NTNODES_INTERNAL; i++) {
				if (tn->internal[i]) {
					tn->internal[i] =
						yaffs_PruneWorker(dev, tn->internal[i],
							level - 1,
							(i == 0) ? del0 : 1);
				}

				if (tn->internal[i])
					hasData++;
			}
		} else {
			int tnodeSize_u32 = dev->tnodeSize/sizeof(__u32);
			__u32 *map = (__u32 *)tn;

                        for(i = 0; !hasData && i < tnodeSize_u32; i++){
                                if(map[i])
                                        hasData++;
                        }
                }

		if (hasData == 0 && del0) {
			

			yaffs_FreeTnode(dev, tn);
			tn = NULL;
		}

	}

	return tn;

}

static int yaffs_PruneFileStructure(yaffs_Device *dev,
				yaffs_FileStructure *fStruct)
{
	int i;
	int hasData;
	int done = 0;
	yaffs_Tnode *tn;

	if (fStruct->topLevel > 0) {
		fStruct->top =
		    yaffs_PruneWorker(dev, fStruct->top, fStruct->topLevel, 0);

		

		while (fStruct->topLevel && !done) {
			tn = fStruct->top;

			hasData = 0;
			for (i = 1; i < YAFFS_NTNODES_INTERNAL; i++) {
				if (tn->internal[i])
					hasData++;
			}

			if (!hasData) {
				fStruct->top = tn->internal[0];
				fStruct->topLevel--;
				yaffs_FreeTnode(dev, tn);
			} else {
				done = 1;
			}
		}
	}

	return YAFFS_OK;
}





static yaffs_Object *yaffs_AllocateEmptyObject(yaffs_Device *dev)
{
	yaffs_Object *obj = yaffs_AllocateRawObject(dev);

	if (obj) {
		dev->nObjects++;

		

		memset(obj, 0, sizeof(yaffs_Object));
		obj->beingCreated = 1;

		obj->myDev = dev;
		obj->hdrChunk = 0;
		obj->variantType = YAFFS_OBJECT_TYPE_UNKNOWN;
		YINIT_LIST_HEAD(&(obj->hardLinks));
		YINIT_LIST_HEAD(&(obj->hashLink));
		YINIT_LIST_HEAD(&obj->siblings);


		
		if (dev->rootDir) {
			obj->parent = dev->rootDir;
			ylist_add(&(obj->siblings), &dev->rootDir->variant.directoryVariant.children);
		}

		
		if (dev->lostNFoundDir)
			yaffs_AddObjectToDirectory(dev->lostNFoundDir, obj);

		obj->beingCreated = 0;
	}

	dev->nCheckpointBlocksRequired = 0; 

	return obj;
}

static yaffs_Object *yaffs_CreateFakeDirectory(yaffs_Device *dev, int number,
					       __u32 mode)
{

	yaffs_Object *obj =
	    yaffs_CreateNewObject(dev, number, YAFFS_OBJECT_TYPE_DIRECTORY);
	if (obj) {
		obj->fake = 1;		
		obj->renameAllowed = 0;	
		obj->unlinkAllowed = 0;	
		obj->deleted = 0;
		obj->unlinked = 0;
		obj->yst_mode = mode;
		obj->myDev = dev;
		obj->hdrChunk = 0;	
	}

	return obj;

}

static void yaffs_UnhashObject(yaffs_Object *obj)
{
	int bucket;
	yaffs_Device *dev = obj->myDev;

	
	if (!ylist_empty(&obj->hashLink)) {
		ylist_del_init(&obj->hashLink);
		bucket = yaffs_HashFunction(obj->objectId);
		dev->objectBucket[bucket].count--;
	}
}


static void yaffs_FreeObject(yaffs_Object *obj)
{
	yaffs_Device *dev = obj->myDev;

	T(YAFFS_TRACE_OS, (TSTR("FreeObject %p inode %p"TENDSTR), obj, obj->myInode));

	if (!obj)
		YBUG();
	if (obj->parent)
		YBUG();
	if (!ylist_empty(&obj->siblings))
		YBUG();


	if (obj->myInode) {
		
		obj->deferedFree = 1;
		return;
	}

	yaffs_UnhashObject(obj);

	yaffs_FreeRawObject(dev,obj);
	dev->nObjects--;
	dev->nCheckpointBlocksRequired = 0; 
}


void yaffs_HandleDeferedFree(yaffs_Object *obj)
{
	if (obj->deferedFree)
		yaffs_FreeObject(obj);
}

static void yaffs_InitialiseTnodesAndObjects(yaffs_Device *dev)
{
	int i;

	dev->nObjects = 0;
	dev->nTnodes = 0;

	yaffs_InitialiseRawTnodesAndObjects(dev);

	for (i = 0; i < YAFFS_NOBJECT_BUCKETS; i++) {
		YINIT_LIST_HEAD(&dev->objectBucket[i].list);
		dev->objectBucket[i].count = 0;
	}
}

static int yaffs_FindNiceObjectBucket(yaffs_Device *dev)
{
	int i;
	int l = 999;
	int lowest = 999999;


	

	for (i = 0; i < 10 && lowest > 4; i++) {
		dev->bucketFinder++;
		dev->bucketFinder %= YAFFS_NOBJECT_BUCKETS;
		if (dev->objectBucket[dev->bucketFinder].count < lowest) {
			lowest = dev->objectBucket[dev->bucketFinder].count;
			l = dev->bucketFinder;
		}

	}

	return l;
}

static int yaffs_CreateNewObjectNumber(yaffs_Device *dev)
{
	int bucket = yaffs_FindNiceObjectBucket(dev);

	

	int found = 0;
	struct ylist_head *i;

	__u32 n = (__u32) bucket;

	

	while (!found) {
		found = 1;
		n += YAFFS_NOBJECT_BUCKETS;
		if (1 || dev->objectBucket[bucket].count > 0) {
			ylist_for_each(i, &dev->objectBucket[bucket].list) {
				
				if (i && ylist_entry(i, yaffs_Object,
						hashLink)->objectId == n) {
					found = 0;
				}
			}
		}
	}

	return n;
}

static void yaffs_HashObject(yaffs_Object *in)
{
	int bucket = yaffs_HashFunction(in->objectId);
	yaffs_Device *dev = in->myDev;

	ylist_add(&in->hashLink, &dev->objectBucket[bucket].list);
	dev->objectBucket[bucket].count++;
}

yaffs_Object *yaffs_FindObjectByNumber(yaffs_Device *dev, __u32 number)
{
	int bucket = yaffs_HashFunction(number);
	struct ylist_head *i;
	yaffs_Object *in;

	ylist_for_each(i, &dev->objectBucket[bucket].list) {
		
		if (i) {
			in = ylist_entry(i, yaffs_Object, hashLink);
			if (in->objectId == number) {

				
				if (in->deferedFree)
					return NULL;

				return in;
			}
		}
	}

	return NULL;
}

yaffs_Object *yaffs_CreateNewObject(yaffs_Device *dev, int number,
				    yaffs_ObjectType type)
{
	yaffs_Object *theObject=NULL;
	yaffs_Tnode *tn = NULL;

	if (number < 0)
		number = yaffs_CreateNewObjectNumber(dev);

	if (type == YAFFS_OBJECT_TYPE_FILE) {
		tn = yaffs_GetTnode(dev);
		if (!tn)
			return NULL;
	}

	theObject = yaffs_AllocateEmptyObject(dev);
	if (!theObject){
		if(tn)
			yaffs_FreeTnode(dev,tn);
		return NULL;
	}


	if (theObject) {
		theObject->fake = 0;
		theObject->renameAllowed = 1;
		theObject->unlinkAllowed = 1;
		theObject->objectId = number;
		yaffs_HashObject(theObject);
		theObject->variantType = type;
#ifdef CONFIG_YAFFS_WINCE
		yfsd_WinFileTimeNow(theObject->win_atime);
		theObject->win_ctime[0] = theObject->win_mtime[0] =
		    theObject->win_atime[0];
		theObject->win_ctime[1] = theObject->win_mtime[1] =
		    theObject->win_atime[1];

#else

		theObject->yst_atime = theObject->yst_mtime =
		    theObject->yst_ctime = Y_CURRENT_TIME;
#endif
		switch (type) {
		case YAFFS_OBJECT_TYPE_FILE:
			theObject->variant.fileVariant.fileSize = 0;
			theObject->variant.fileVariant.scannedFileSize = 0;
			theObject->variant.fileVariant.shrinkSize = 0xFFFFFFFF;	
			theObject->variant.fileVariant.topLevel = 0;
			theObject->variant.fileVariant.top = tn;
			break;
		case YAFFS_OBJECT_TYPE_DIRECTORY:
			YINIT_LIST_HEAD(&theObject->variant.directoryVariant.
					children);
			YINIT_LIST_HEAD(&theObject->variant.directoryVariant.
					dirty);
			break;
		case YAFFS_OBJECT_TYPE_SYMLINK:
		case YAFFS_OBJECT_TYPE_HARDLINK:
		case YAFFS_OBJECT_TYPE_SPECIAL:
			
			break;
		case YAFFS_OBJECT_TYPE_UNKNOWN:
			
			break;
		}
	}

	return theObject;
}

yaffs_Object *yaffs_FindOrCreateObjectByNumber(yaffs_Device *dev,
						int number,
						yaffs_ObjectType type)
{
	yaffs_Object *theObject = NULL;

	if (number > 0)
		theObject = yaffs_FindObjectByNumber(dev, number);

	if (!theObject)
		theObject = yaffs_CreateNewObject(dev, number, type);

	return theObject;

}


YCHAR *yaffs_CloneString(const YCHAR *str)
{
	YCHAR *newStr = NULL;
	int len;

	if (!str)
		str = _Y("");

	len = yaffs_strnlen(str,YAFFS_MAX_ALIAS_LENGTH);
	newStr = YMALLOC((len + 1) * sizeof(YCHAR));
	if (newStr){
		yaffs_strncpy(newStr, str,len);
		newStr[len] = 0;
	}
	return newStr;

}



static yaffs_Object *yaffs_MknodObject(yaffs_ObjectType type,
				       yaffs_Object *parent,
				       const YCHAR *name,
				       __u32 mode,
				       __u32 uid,
				       __u32 gid,
				       yaffs_Object *equivalentObject,
				       const YCHAR *aliasString, __u32 rdev)
{
	yaffs_Object *in;
	YCHAR *str = NULL;

	yaffs_Device *dev = parent->myDev;

	
	if (yaffs_FindObjectByName(parent, name))
		return NULL;

	if (type == YAFFS_OBJECT_TYPE_SYMLINK) {
		str = yaffs_CloneString(aliasString);
		if (!str)
			return NULL;
	}

	in = yaffs_CreateNewObject(dev, -1, type);

	if (!in){
		if(str)
			YFREE(str);
		return NULL;
	}





	if (in) {
		in->hdrChunk = 0;
		in->valid = 1;
		in->variantType = type;

		in->yst_mode = mode;

#ifdef CONFIG_YAFFS_WINCE
		yfsd_WinFileTimeNow(in->win_atime);
		in->win_ctime[0] = in->win_mtime[0] = in->win_atime[0];
		in->win_ctime[1] = in->win_mtime[1] = in->win_atime[1];

#else
		in->yst_atime = in->yst_mtime = in->yst_ctime = Y_CURRENT_TIME;

		in->yst_rdev = rdev;
		in->yst_uid = uid;
		in->yst_gid = gid;
#endif
		in->nDataChunks = 0;

		yaffs_SetObjectName(in, name);
		in->dirty = 1;

		yaffs_AddObjectToDirectory(parent, in);

		in->myDev = parent->myDev;

		switch (type) {
		case YAFFS_OBJECT_TYPE_SYMLINK:
			in->variant.symLinkVariant.alias = str;
			break;
		case YAFFS_OBJECT_TYPE_HARDLINK:
			in->variant.hardLinkVariant.equivalentObject =
				equivalentObject;
			in->variant.hardLinkVariant.equivalentObjectId =
				equivalentObject->objectId;
			ylist_add(&in->hardLinks, &equivalentObject->hardLinks);
			break;
		case YAFFS_OBJECT_TYPE_FILE:
		case YAFFS_OBJECT_TYPE_DIRECTORY:
		case YAFFS_OBJECT_TYPE_SPECIAL:
		case YAFFS_OBJECT_TYPE_UNKNOWN:
			
			break;
		}

		if (yaffs_UpdateObjectHeader(in, name, 0, 0, 0, NULL) < 0) {
			
			yaffs_DeleteObject(in);
			in = NULL;
		}

		yaffs_UpdateParent(parent);
	}

	return in;
}

yaffs_Object *yaffs_MknodFile(yaffs_Object *parent, const YCHAR *name,
			__u32 mode, __u32 uid, __u32 gid)
{
	return yaffs_MknodObject(YAFFS_OBJECT_TYPE_FILE, parent, name, mode,
				uid, gid, NULL, NULL, 0);
}

yaffs_Object *yaffs_MknodDirectory(yaffs_Object *parent, const YCHAR *name,
				__u32 mode, __u32 uid, __u32 gid)
{
	return yaffs_MknodObject(YAFFS_OBJECT_TYPE_DIRECTORY, parent, name,
				 mode, uid, gid, NULL, NULL, 0);
}

yaffs_Object *yaffs_MknodSpecial(yaffs_Object *parent, const YCHAR *name,
				__u32 mode, __u32 uid, __u32 gid, __u32 rdev)
{
	return yaffs_MknodObject(YAFFS_OBJECT_TYPE_SPECIAL, parent, name, mode,
				 uid, gid, NULL, NULL, rdev);
}

yaffs_Object *yaffs_MknodSymLink(yaffs_Object *parent, const YCHAR *name,
				__u32 mode, __u32 uid, __u32 gid,
				const YCHAR *alias)
{
	return yaffs_MknodObject(YAFFS_OBJECT_TYPE_SYMLINK, parent, name, mode,
				uid, gid, NULL, alias, 0);
}


yaffs_Object *yaffs_Link(yaffs_Object *parent, const YCHAR *name,
			yaffs_Object *equivalentObject)
{
	
	equivalentObject = yaffs_GetEquivalentObject(equivalentObject);

	if (yaffs_MknodObject
	    (YAFFS_OBJECT_TYPE_HARDLINK, parent, name, 0, 0, 0,
	     equivalentObject, NULL, 0)) {
		return equivalentObject;
	} else {
		return NULL;
	}

}

static int yaffs_ChangeObjectName(yaffs_Object *obj, yaffs_Object *newDir,
				const YCHAR *newName, int force, int shadows)
{
	int unlinkOp;
	int deleteOp;

	yaffs_Object *existingTarget;

	if (newDir == NULL)
		newDir = obj->parent;	

	if (newDir->variantType != YAFFS_OBJECT_TYPE_DIRECTORY) {
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR
		   ("tragedy: yaffs_ChangeObjectName: newDir is not a directory"
		    TENDSTR)));
		YBUG();
	}

	
	if (obj->myDev->param.isYaffs2)
		unlinkOp = (newDir == obj->myDev->unlinkedDir);
	else
		unlinkOp = (newDir == obj->myDev->unlinkedDir
			    && obj->variantType == YAFFS_OBJECT_TYPE_FILE);

	deleteOp = (newDir == obj->myDev->deletedDir);

	existingTarget = yaffs_FindObjectByName(newDir, newName);

	
	if ((unlinkOp ||
	     deleteOp ||
	     force ||
	     (shadows > 0) ||
	     !existingTarget) &&
	    newDir->variantType == YAFFS_OBJECT_TYPE_DIRECTORY) {
		yaffs_SetObjectName(obj, newName);
		obj->dirty = 1;

		yaffs_AddObjectToDirectory(newDir, obj);

		if (unlinkOp)
			obj->unlinked = 1;

		
		if (yaffs_UpdateObjectHeader(obj, newName, 0, deleteOp, shadows, NULL) >= 0)
			return YAFFS_OK;
	}

	return YAFFS_FAIL;
}

int yaffs_RenameObject(yaffs_Object *oldDir, const YCHAR *oldName,
		yaffs_Object *newDir, const YCHAR *newName)
{
	yaffs_Object *obj = NULL;
	yaffs_Object *existingTarget = NULL;
	int force = 0;
	int result;
	yaffs_Device *dev;


	if (!oldDir || oldDir->variantType != YAFFS_OBJECT_TYPE_DIRECTORY)
		YBUG();
	if (!newDir || newDir->variantType != YAFFS_OBJECT_TYPE_DIRECTORY)
		YBUG();

	dev = oldDir->myDev;

#ifdef CONFIG_YAFFS_CASE_INSENSITIVE
	
	if (oldDir == newDir && yaffs_strcmp(oldName, newName) == 0)
		force = 1;
#endif

	if(yaffs_strnlen(newName,YAFFS_MAX_NAME_LENGTH+1) > YAFFS_MAX_NAME_LENGTH)
		
		return YAFFS_FAIL;

	obj = yaffs_FindObjectByName(oldDir, oldName);

	if (obj && obj->renameAllowed) {

		

		existingTarget = yaffs_FindObjectByName(newDir, newName);
		if (existingTarget &&
			existingTarget->variantType == YAFFS_OBJECT_TYPE_DIRECTORY &&
			!ylist_empty(&existingTarget->variant.directoryVariant.children)) {
			
			return YAFFS_FAIL;	
		} else if (existingTarget && existingTarget != obj) {
			
			dev->gcDisable=1;
			yaffs_ChangeObjectName(obj, newDir, newName, force,
						existingTarget->objectId);
			existingTarget->isShadowed = 1;
			yaffs_UnlinkObject(existingTarget);
			dev->gcDisable=0;
		}

		result = yaffs_ChangeObjectName(obj, newDir, newName, 1, 0);

		yaffs_UpdateParent(oldDir);
		if(newDir != oldDir)
			yaffs_UpdateParent(newDir);
		
		return result;
	}
	return YAFFS_FAIL;
}



static int yaffs_InitialiseBlocks(yaffs_Device *dev)
{
	int nBlocks = dev->internalEndBlock - dev->internalStartBlock + 1;

	dev->blockInfo = NULL;
	dev->chunkBits = NULL;

	dev->allocationBlock = -1;	

	
	dev->blockInfo = YMALLOC(nBlocks * sizeof(yaffs_BlockInfo));
	if (!dev->blockInfo) {
		dev->blockInfo = YMALLOC_ALT(nBlocks * sizeof(yaffs_BlockInfo));
		dev->blockInfoAlt = 1;
	} else
		dev->blockInfoAlt = 0;

	if (dev->blockInfo) {
		
		dev->chunkBitmapStride = (dev->param.nChunksPerBlock + 7) / 8; 
		dev->chunkBits = YMALLOC(dev->chunkBitmapStride * nBlocks);
		if (!dev->chunkBits) {
			dev->chunkBits = YMALLOC_ALT(dev->chunkBitmapStride * nBlocks);
			dev->chunkBitsAlt = 1;
		} else
			dev->chunkBitsAlt = 0;
	}

	if (dev->blockInfo && dev->chunkBits) {
		memset(dev->blockInfo, 0, nBlocks * sizeof(yaffs_BlockInfo));
		memset(dev->chunkBits, 0, dev->chunkBitmapStride * nBlocks);
		return YAFFS_OK;
	}

	return YAFFS_FAIL;
}

static void yaffs_DeinitialiseBlocks(yaffs_Device *dev)
{
	if (dev->blockInfoAlt && dev->blockInfo)
		YFREE_ALT(dev->blockInfo);
	else if (dev->blockInfo)
		YFREE(dev->blockInfo);

	dev->blockInfoAlt = 0;

	dev->blockInfo = NULL;

	if (dev->chunkBitsAlt && dev->chunkBits)
		YFREE_ALT(dev->chunkBits);
	else if (dev->chunkBits)
		YFREE(dev->chunkBits);
	dev->chunkBitsAlt = 0;
	dev->chunkBits = NULL;
}

void yaffs_BlockBecameDirty(yaffs_Device *dev, int blockNo)
{
	yaffs_BlockInfo *bi = yaffs_GetBlockInfo(dev, blockNo);

	int erasedOk = 0;

	

	T(YAFFS_TRACE_GC | YAFFS_TRACE_ERASE,
		(TSTR("yaffs_BlockBecameDirty block %d state %d %s"TENDSTR),
		blockNo, bi->blockState, (bi->needsRetiring) ? "needs retiring" : ""));

	yaffs2_ClearOldestDirtySequence(dev,bi);

	bi->blockState = YAFFS_BLOCK_STATE_DIRTY;

	
	if(blockNo == dev->gcBlock)
		dev->gcBlock = 0;

	
	if(blockNo == dev->gcDirtiest){
		dev->gcDirtiest = 0;
		dev->gcPagesInUse = 0;
	}

	if (!bi->needsRetiring) {
		yaffs2_InvalidateCheckpoint(dev);
		erasedOk = yaffs_EraseBlockInNAND(dev, blockNo);
		if (!erasedOk) {
			dev->nErasureFailures++;
			T(YAFFS_TRACE_ERROR | YAFFS_TRACE_BAD_BLOCKS,
			  (TSTR("**>> Erasure failed %d" TENDSTR), blockNo));
		}
	}

	if (erasedOk &&
	    ((yaffs_traceMask & YAFFS_TRACE_ERASE) || !yaffs_SkipVerification(dev))) {
		int i;
		for (i = 0; i < dev->param.nChunksPerBlock; i++) {
			if (!yaffs_CheckChunkErased
			    (dev, blockNo * dev->param.nChunksPerBlock + i)) {
				T(YAFFS_TRACE_ERROR,
				  (TSTR
				   (">>Block %d erasure supposedly OK, but chunk %d not erased"
				    TENDSTR), blockNo, i));
			}
		}
	}

	if (erasedOk) {
		
		bi->blockState = YAFFS_BLOCK_STATE_EMPTY;
		bi->sequenceNumber = 0;
		dev->nErasedBlocks++;
		bi->pagesInUse = 0;
		bi->softDeletions = 0;
		bi->hasShrinkHeader = 0;
		bi->skipErasedCheck = 1;  
		bi->gcPrioritise = 0;
		yaffs_ClearChunkBits(dev, blockNo);

		T(YAFFS_TRACE_ERASE,
		  (TSTR("Erased block %d" TENDSTR), blockNo));
	} else {
		dev->nFreeChunks -= dev->param.nChunksPerBlock;	

		yaffs_RetireBlock(dev, blockNo);
		T(YAFFS_TRACE_ERROR | YAFFS_TRACE_BAD_BLOCKS,
		  (TSTR("**>> Block %d retired" TENDSTR), blockNo));
	}
}

static int yaffs_FindBlockForAllocation(yaffs_Device *dev)
{
	int i;

	yaffs_BlockInfo *bi;

	if (dev->nErasedBlocks < 1) {
		
		T(YAFFS_TRACE_ERROR,
		  (TSTR("yaffs tragedy: no more erased blocks" TENDSTR)));

		return -1;
	}

	

	for (i = dev->internalStartBlock; i <= dev->internalEndBlock; i++) {
		dev->allocationBlockFinder++;
		if (dev->allocationBlockFinder < dev->internalStartBlock
		    || dev->allocationBlockFinder > dev->internalEndBlock) {
			dev->allocationBlockFinder = dev->internalStartBlock;
		}

		bi = yaffs_GetBlockInfo(dev, dev->allocationBlockFinder);

		if (bi->blockState == YAFFS_BLOCK_STATE_EMPTY) {
			bi->blockState = YAFFS_BLOCK_STATE_ALLOCATING;
			dev->sequenceNumber++;
			bi->sequenceNumber = dev->sequenceNumber;
			dev->nErasedBlocks--;
			T(YAFFS_TRACE_ALLOCATE,
			  (TSTR("Allocated block %d, seq  %d, %d left" TENDSTR),
			   dev->allocationBlockFinder, dev->sequenceNumber,
			   dev->nErasedBlocks));
			return dev->allocationBlockFinder;
		}
	}

	T(YAFFS_TRACE_ALWAYS,
	  (TSTR
	   ("yaffs tragedy: no more erased blocks, but there should have been %d"
	    TENDSTR), dev->nErasedBlocks));

	return -1;
}



int yaffs_CheckSpaceForAllocation(yaffs_Device *dev, int nChunks)
{
	int reservedChunks;
	int reservedBlocks = dev->param.nReservedBlocks;
	int checkpointBlocks;

	checkpointBlocks = yaffs2_CalcCheckpointBlocksRequired(dev);

	reservedChunks = ((reservedBlocks + checkpointBlocks) * dev->param.nChunksPerBlock);

	return (dev->nFreeChunks > (reservedChunks + nChunks));
}

static int yaffs_AllocateChunk(yaffs_Device *dev, int useReserve,
		yaffs_BlockInfo **blockUsedPtr)
{
	int retVal;
	yaffs_BlockInfo *bi;

	if (dev->allocationBlock < 0) {
		
		dev->allocationBlock = yaffs_FindBlockForAllocation(dev);
		dev->allocationPage = 0;
	}

	if (!useReserve && !yaffs_CheckSpaceForAllocation(dev, 1)) {
		
		return -1;
	}

	if (dev->nErasedBlocks < dev->param.nReservedBlocks
			&& dev->allocationPage == 0) {
		T(YAFFS_TRACE_ALLOCATE, (TSTR("Allocating reserve" TENDSTR)));
	}

	
	if (dev->allocationBlock >= 0) {
		bi = yaffs_GetBlockInfo(dev, dev->allocationBlock);

		retVal = (dev->allocationBlock * dev->param.nChunksPerBlock) +
			dev->allocationPage;
		bi->pagesInUse++;
		yaffs_SetChunkBit(dev, dev->allocationBlock,
				dev->allocationPage);

		dev->allocationPage++;

		dev->nFreeChunks--;

		
		if (dev->allocationPage >= dev->param.nChunksPerBlock) {
			bi->blockState = YAFFS_BLOCK_STATE_FULL;
			dev->allocationBlock = -1;
		}

		if (blockUsedPtr)
			*blockUsedPtr = bi;

		return retVal;
	}

	T(YAFFS_TRACE_ERROR,
			(TSTR("!!!!!!!!! Allocator out !!!!!!!!!!!!!!!!!" TENDSTR)));

	return -1;
}

static int yaffs_GetErasedChunks(yaffs_Device *dev)
{
	int n;

	n = dev->nErasedBlocks * dev->param.nChunksPerBlock;

	if (dev->allocationBlock > 0)
		n += (dev->param.nChunksPerBlock - dev->allocationPage);

	return n;

}


void yaffs_SkipRestOfBlock(yaffs_Device *dev)
{
	if(dev->allocationBlock > 0){
		yaffs_BlockInfo *bi = yaffs_GetBlockInfo(dev, dev->allocationBlock);
		if(bi->blockState == YAFFS_BLOCK_STATE_ALLOCATING){
			bi->blockState = YAFFS_BLOCK_STATE_FULL;
			dev->allocationBlock = -1;
		}
	}
}


static int yaffs_GarbageCollectBlock(yaffs_Device *dev, int block,
		int wholeBlock)
{
	int oldChunk;
	int newChunk;
	int markNAND;
	int retVal = YAFFS_OK;
	int i;
	int isCheckpointBlock;
	int matchingChunk;
	int maxCopies;

	int chunksBefore = yaffs_GetErasedChunks(dev);
	int chunksAfter;

	yaffs_ExtendedTags tags;

	yaffs_BlockInfo *bi = yaffs_GetBlockInfo(dev, block);

	yaffs_Object *object;

	isCheckpointBlock = (bi->blockState == YAFFS_BLOCK_STATE_CHECKPOINT);


	T(YAFFS_TRACE_TRACING,
			(TSTR("Collecting block %d, in use %d, shrink %d, wholeBlock %d" TENDSTR),
			 block,
			 bi->pagesInUse,
			 bi->hasShrinkHeader,
			 wholeBlock));

	

	if(bi->blockState == YAFFS_BLOCK_STATE_FULL)
		bi->blockState = YAFFS_BLOCK_STATE_COLLECTING;
	
	bi->hasShrinkHeader = 0;	

	dev->gcDisable = 1;

	if (isCheckpointBlock ||
			!yaffs_StillSomeChunkBits(dev, block)) {
		T(YAFFS_TRACE_TRACING,
				(TSTR
				 ("Collecting block %d that has no chunks in use" TENDSTR),
				 block));
		yaffs_BlockBecameDirty(dev, block);
	} else {

		__u8 *buffer = yaffs_GetTempBuffer(dev, __LINE__);

		yaffs_VerifyBlock(dev, bi, block);

		maxCopies = (wholeBlock) ? dev->param.nChunksPerBlock : 5;
		oldChunk = block * dev->param.nChunksPerBlock + dev->gcChunk;

		for (;
		     retVal == YAFFS_OK &&
		     dev->gcChunk < dev->param.nChunksPerBlock &&
		     (bi->blockState == YAFFS_BLOCK_STATE_COLLECTING) &&
		     maxCopies > 0;
		     dev->gcChunk++, oldChunk++) {
			if (yaffs_CheckChunkBit(dev, block, dev->gcChunk)) {

				

				maxCopies--;

				markNAND = 1;

				yaffs_InitialiseTags(&tags);

				yaffs_ReadChunkWithTagsFromNAND(dev, oldChunk,
								buffer, &tags);

				object =
				    yaffs_FindObjectByNumber(dev,
							     tags.objectId);

				T(YAFFS_TRACE_GC_DETAIL,
				  (TSTR
				   ("Collecting chunk in block %d, %d %d %d " TENDSTR),
				   dev->gcChunk, tags.objectId, tags.chunkId,
				   tags.byteCount));

				if (object && !yaffs_SkipVerification(dev)) {
					if (tags.chunkId == 0)
						matchingChunk = object->hdrChunk;
					else if (object->softDeleted)
						matchingChunk = oldChunk; 
					else
						matchingChunk = yaffs_FindChunkInFile(object, tags.chunkId, NULL);

					if (oldChunk != matchingChunk)
						T(YAFFS_TRACE_ERROR,
						  (TSTR("gc: page in gc mismatch: %d %d %d %d"TENDSTR),
						  oldChunk, matchingChunk, tags.objectId, tags.chunkId));

				}

				if (!object) {
					T(YAFFS_TRACE_ERROR,
					  (TSTR
					   ("page %d in gc has no object: %d %d %d "
					    TENDSTR), oldChunk,
					    tags.objectId, tags.chunkId, tags.byteCount));
				}

				if (object &&
				    object->deleted &&
				    object->softDeleted &&
				    tags.chunkId != 0) {
					/* Data chunk in a soft deleted file, throw it away
					 * It's a soft deleted data chunk,
					 * No need to copy this, just forget about it and
					 * fix up the object.
					 */
					 
					/* Free chunks already includes softdeleted chunks.
					 * How ever this chunk is going to soon be really deleted
					 * which will increment free chunks.
					 * We have to decrement free chunks so this works out properly.
					 */
					dev->nFreeChunks--;
					bi->softDeletions--;

					object->nDataChunks--;

					if (object->nDataChunks <= 0) {
						
						dev->gcCleanupList[dev->nCleanups] =
						    tags.objectId;
						dev->nCleanups++;
					}
					markNAND = 0;
				} else if (0) {
					
					
					object->hdrChunk = 0;
					yaffs_FreeTnode(object->myDev,
							object->variant.
							fileVariant.top);
					object->variant.fileVariant.top = NULL;
					yaffs_DoGenericObjectDeletion(object);

				} else if (object) {
					
					tags.serialNumber++;

					dev->nGCCopies++;

					if (tags.chunkId == 0) {
						

						yaffs_ObjectHeader *oh;
						oh = (yaffs_ObjectHeader *)buffer;

						oh->isShrink = 0;
						tags.extraIsShrinkHeader = 0;

						oh->shadowsObject = 0;
						oh->inbandShadowsObject = 0;
						tags.extraShadows = 0;

						
						if(object->variantType == YAFFS_OBJECT_TYPE_FILE){
							oh->fileSize = object->variant.fileVariant.fileSize;
							tags.extraFileLength = oh->fileSize;
						}

						yaffs_VerifyObjectHeader(object, oh, &tags, 1);
						newChunk =
						    yaffs_WriteNewChunkWithTagsToNAND(dev,(__u8 *) oh, &tags, 1);
					} else
						newChunk =
						    yaffs_WriteNewChunkWithTagsToNAND(dev, buffer, &tags, 1);

					if (newChunk < 0) {
						retVal = YAFFS_FAIL;
					} else {

						

						if (tags.chunkId == 0) {
							
							object->hdrChunk =  newChunk;
							object->serial =   tags.serialNumber;
						} else {
							
							int ok;
							ok = yaffs_PutChunkIntoFile
							    (object,
							     tags.chunkId,
							     newChunk, 0);
						}
					}
				}

				if (retVal == YAFFS_OK)
					yaffs_DeleteChunk(dev, oldChunk, markNAND, __LINE__);

			}
		}

		yaffs_ReleaseTempBuffer(dev, buffer, __LINE__);



	}

	yaffs_VerifyCollectedBlock(dev, bi, block);



	if (bi->blockState == YAFFS_BLOCK_STATE_COLLECTING) {
		/*
		 * The gc did not complete. Set block state back to FULL
		 * because checkpointing does not restore gc.
		 */
		bi->blockState = YAFFS_BLOCK_STATE_FULL;
	} else {
		
		
		for (i = 0; i < dev->nCleanups; i++) {
			
			object =
			    yaffs_FindObjectByNumber(dev,
						     dev->gcCleanupList[i]);
			if (object) {
				yaffs_FreeTnode(dev,
						object->variant.fileVariant.
						top);
				object->variant.fileVariant.top = NULL;
				T(YAFFS_TRACE_GC,
				  (TSTR
				   ("yaffs: About to finally delete object %d"
				    TENDSTR), object->objectId));
				yaffs_DoGenericObjectDeletion(object);
				object->myDev->nDeletedFiles--;
			}

		}


		chunksAfter = yaffs_GetErasedChunks(dev);
		if (chunksBefore >= chunksAfter) {
			T(YAFFS_TRACE_GC,
			  (TSTR
			   ("gc did not increase free chunks before %d after %d"
			    TENDSTR), chunksBefore, chunksAfter));
		}
		dev->gcBlock = 0;
		dev->gcChunk = 0;
		dev->nCleanups = 0;
	}

	dev->gcDisable = 0;

	return retVal;
}



static unsigned yaffs_FindBlockForGarbageCollection(yaffs_Device *dev,
					int aggressive,
					int background)
{
	int i;
	int iterations;
	unsigned selected = 0;
	int prioritised = 0;
	int prioritisedExists = 0;
	yaffs_BlockInfo *bi;
	int threshold;

	
	if (dev->hasPendingPrioritisedGCs && !aggressive) {
		dev->gcDirtiest = 0;
		bi = dev->blockInfo;
		for (i = dev->internalStartBlock;
			i <= dev->internalEndBlock && !selected;
			i++) {

			if (bi->gcPrioritise) {
				prioritisedExists = 1;
				if (bi->blockState == YAFFS_BLOCK_STATE_FULL &&
				   yaffs2_BlockNotDisqualifiedFromGC(dev, bi)) {
					selected = i;
					prioritised = 1;
				}
			}
			bi++;
		}

		/*
		 * If there is a prioritised block and none was selected then
		 * this happened because there is at least one old dirty block gumming
		 * up the works. Let's gc the oldest dirty block.
		 */

		if(prioritisedExists &&
			!selected &&
			dev->oldestDirtyBlock > 0)
			selected = dev->oldestDirtyBlock;

		if (!prioritisedExists) 
			dev->hasPendingPrioritisedGCs = 0;
	}

	/* If we're doing aggressive GC then we are happy to take a less-dirty block, and
	 * search harder.
	 * else (we're doing a leasurely gc), then we only bother to do this if the
	 * block has only a few pages in use.
	 */

	if (!selected){
		int pagesUsed;
		int nBlocks = dev->internalEndBlock - dev->internalStartBlock + 1;
		if (aggressive){
			threshold = dev->param.nChunksPerBlock;
			iterations = nBlocks;
		} else {
			int maxThreshold;

			if(background)
				maxThreshold = dev->param.nChunksPerBlock/2;
			else
				maxThreshold = dev->param.nChunksPerBlock/8;

			if(maxThreshold <  YAFFS_GC_PASSIVE_THRESHOLD)
				maxThreshold = YAFFS_GC_PASSIVE_THRESHOLD;

			threshold = background ?
				(dev->gcNotDone + 2) * 2 : 0;
			if(threshold <YAFFS_GC_PASSIVE_THRESHOLD)
				threshold = YAFFS_GC_PASSIVE_THRESHOLD;
			if(threshold > maxThreshold)
				threshold = maxThreshold;

			iterations = nBlocks / 16 + 1;
			if (iterations > 100)
				iterations = 100;
		}

		for (i = 0;
			i < iterations &&
			(dev->gcDirtiest < 1 ||
				dev->gcPagesInUse > YAFFS_GC_GOOD_ENOUGH);
			i++) {
			dev->gcBlockFinder++;
			if (dev->gcBlockFinder < dev->internalStartBlock ||
				dev->gcBlockFinder > dev->internalEndBlock)
				dev->gcBlockFinder = dev->internalStartBlock;

			bi = yaffs_GetBlockInfo(dev, dev->gcBlockFinder);

			pagesUsed = bi->pagesInUse - bi->softDeletions;

			if (bi->blockState == YAFFS_BLOCK_STATE_FULL &&
				pagesUsed < dev->param.nChunksPerBlock &&
				(dev->gcDirtiest < 1 || pagesUsed < dev->gcPagesInUse) &&
				yaffs2_BlockNotDisqualifiedFromGC(dev, bi)) {
				dev->gcDirtiest = dev->gcBlockFinder;
				dev->gcPagesInUse = pagesUsed;
			}
		}

		if(dev->gcDirtiest > 0 && dev->gcPagesInUse <= threshold)
			selected = dev->gcDirtiest;
	}

	/*
	 * If nothing has been selected for a while, try selecting the oldest dirty
	 * because that's gumming up the works.
	 */

	if(!selected && dev->param.isYaffs2 &&
		dev->gcNotDone >= ( background ? 10 : 20)){
		yaffs2_FindOldestDirtySequence(dev);
		if(dev->oldestDirtyBlock > 0) {
			selected = dev->oldestDirtyBlock;
			dev->gcDirtiest = selected;
			dev->oldestDirtyGCs++;
			bi = yaffs_GetBlockInfo(dev, selected);
			dev->gcPagesInUse =  bi->pagesInUse - bi->softDeletions;
		} else
			dev->gcNotDone = 0;
	}

	if(selected){
		T(YAFFS_TRACE_GC,
		  (TSTR("GC Selected block %d with %d free, prioritised:%d" TENDSTR),
		  selected,
		  dev->param.nChunksPerBlock - dev->gcPagesInUse,
		  prioritised));

		dev->nGCBlocks++;
		if(background)
			dev->backgroundGCs++;

		dev->gcDirtiest = 0;
		dev->gcPagesInUse = 0;
		dev->gcNotDone = 0;
		if(dev->refreshSkip > 0)
			dev->refreshSkip--;
	} else{
		dev->gcNotDone++;
		T(YAFFS_TRACE_GC,
		  (TSTR("GC none: finder %d skip %d threshold %d dirtiest %d using %d oldest %d%s" TENDSTR),
		  dev->gcBlockFinder, dev->gcNotDone,
		  threshold,
		  dev->gcDirtiest, dev->gcPagesInUse,
		  dev->oldestDirtyBlock,
		  background ? " bg" : ""));
	}

	return selected;
}


static int yaffs_CheckGarbageCollection(yaffs_Device *dev, int background)
{
	int aggressive = 0;
	int gcOk = YAFFS_OK;
	int maxTries = 0;
	int minErased;
	int erasedChunks;
	int checkpointBlockAdjust;

	if(dev->param.gcControl &&
		(dev->param.gcControl(dev) & 1) == 0)
		return YAFFS_OK;

	if (dev->gcDisable) {
		
		return YAFFS_OK;
	}

	

	do {
		maxTries++;

		checkpointBlockAdjust = yaffs2_CalcCheckpointBlocksRequired(dev);

		minErased  = dev->param.nReservedBlocks + checkpointBlockAdjust + 1;
		erasedChunks = dev->nErasedBlocks * dev->param.nChunksPerBlock;

		
		if (dev->nErasedBlocks < minErased)
			aggressive = 1;
		else {
			if(!background && erasedChunks > (dev->nFreeChunks / 4))
				break;

			if(dev->gcSkip > 20)
				dev->gcSkip = 20;
			if(erasedChunks < dev->nFreeChunks/2 ||
				dev->gcSkip < 1 ||
				background)
				aggressive = 0;
			else {
				dev->gcSkip--;
				break;
			}
		}

		dev->gcSkip = 5;

                

		if (dev->gcBlock < 1 && !aggressive) {
			dev->gcBlock = yaffs2_FindRefreshBlock(dev);
			dev->gcChunk = 0;
			dev->nCleanups=0;
		}
		if (dev->gcBlock < 1) {
			dev->gcBlock = yaffs_FindBlockForGarbageCollection(dev, aggressive, background);
			dev->gcChunk = 0;
			dev->nCleanups=0;
		}

		if (dev->gcBlock > 0) {
			dev->allGCs++;
			if (!aggressive)
				dev->passiveGCs++;

			T(YAFFS_TRACE_GC,
			  (TSTR
			   ("yaffs: GC erasedBlocks %d aggressive %d" TENDSTR),
			   dev->nErasedBlocks, aggressive));

			gcOk = yaffs_GarbageCollectBlock(dev, dev->gcBlock, aggressive);
		}

		if (dev->nErasedBlocks < (dev->param.nReservedBlocks) && dev->gcBlock > 0) {
			T(YAFFS_TRACE_GC,
			  (TSTR
			   ("yaffs: GC !!!no reclaim!!! erasedBlocks %d after try %d block %d"
			    TENDSTR), dev->nErasedBlocks, maxTries, dev->gcBlock));
		}
	} while ((dev->nErasedBlocks < dev->param.nReservedBlocks) &&
		 (dev->gcBlock > 0) &&
		 (maxTries < 2));

	return aggressive ? gcOk : YAFFS_OK;
}


int yaffs_BackgroundGarbageCollect(yaffs_Device *dev, unsigned urgency)
{
	int erasedChunks = dev->nErasedBlocks * dev->param.nChunksPerBlock;

	T(YAFFS_TRACE_BACKGROUND, (TSTR("Background gc %u" TENDSTR),urgency));

	yaffs_CheckGarbageCollection(dev, 1);
	return erasedChunks > dev->nFreeChunks/2;
}



static int yaffs_TagsMatch(const yaffs_ExtendedTags *tags, int objectId,
			   int chunkInObject)
{
	return (tags->chunkId == chunkInObject &&
		tags->objectId == objectId && !tags->chunkDeleted) ? 1 : 0;

}




static int yaffs_FindChunkInFile(yaffs_Object *in, int chunkInInode,
				 yaffs_ExtendedTags *tags)
{
	
	yaffs_Tnode *tn;
	int theChunk = -1;
	yaffs_ExtendedTags localTags;
	int retVal = -1;

	yaffs_Device *dev = in->myDev;

	if (!tags) {
		
		tags = &localTags;
	}

	tn = yaffs_FindLevel0Tnode(dev, &in->variant.fileVariant, chunkInInode);

	if (tn) {
		theChunk = yaffs_GetChunkGroupBase(dev, tn, chunkInInode);

		retVal =
		    yaffs_FindChunkInGroup(dev, theChunk, tags, in->objectId,
					   chunkInInode);
	}
	return retVal;
}

static int yaffs_FindAndDeleteChunkInFile(yaffs_Object *in, int chunkInInode,
					  yaffs_ExtendedTags *tags)
{
	
	yaffs_Tnode *tn;
	int theChunk = -1;
	yaffs_ExtendedTags localTags;

	yaffs_Device *dev = in->myDev;
	int retVal = -1;

	if (!tags) {
		
		tags = &localTags;
	}

	tn = yaffs_FindLevel0Tnode(dev, &in->variant.fileVariant, chunkInInode);

	if (tn) {

		theChunk = yaffs_GetChunkGroupBase(dev, tn, chunkInInode);

		retVal =
		    yaffs_FindChunkInGroup(dev, theChunk, tags, in->objectId,
					   chunkInInode);

		
		if (retVal != -1)
			yaffs_LoadLevel0Tnode(dev, tn, chunkInInode, 0);
	}

	return retVal;
}


int yaffs_PutChunkIntoFile(yaffs_Object *in, int chunkInInode,
			        int chunkInNAND, int inScan)
{
	

	yaffs_Tnode *tn;
	yaffs_Device *dev = in->myDev;
	int existingChunk;
	yaffs_ExtendedTags existingTags;
	yaffs_ExtendedTags newTags;
	unsigned existingSerial, newSerial;

	if (in->variantType != YAFFS_OBJECT_TYPE_FILE) {
		
		if (!inScan) {
			T(YAFFS_TRACE_ERROR,
			  (TSTR
			   ("yaffs tragedy:attempt to put data chunk into a non-file"
			    TENDSTR)));
			YBUG();
		}

		yaffs_DeleteChunk(dev, chunkInNAND, 1, __LINE__);
		return YAFFS_OK;
	}

	tn = yaffs_AddOrFindLevel0Tnode(dev,
					&in->variant.fileVariant,
					chunkInInode,
					NULL);
	if (!tn)
		return YAFFS_FAIL;
	
	if(!chunkInNAND)
		
		return YAFFS_OK;

	existingChunk = yaffs_GetChunkGroupBase(dev, tn, chunkInInode);

	if (inScan != 0) {
		

		if (existingChunk > 0) {
			

			if (inScan > 0) {
				
				yaffs_ReadChunkWithTagsFromNAND(dev,
								chunkInNAND,
								NULL, &newTags);

				
				existingChunk =
				    yaffs_FindChunkInFile(in, chunkInInode,
							  &existingTags);
			}

			if (existingChunk <= 0) {
				

				T(YAFFS_TRACE_ERROR,
				  (TSTR
				   ("yaffs tragedy: existing chunk < 0 in scan"
				    TENDSTR)));

			}

			

			if (inScan > 0) {
				newSerial = newTags.serialNumber;
				existingSerial = existingTags.serialNumber;
			}

			if ((inScan > 0) &&
			    (existingChunk <= 0 ||
			     ((existingSerial + 1) & 3) == newSerial)) {
				
				yaffs_DeleteChunk(dev, existingChunk, 1,
						  __LINE__);
			} else {
				
				yaffs_DeleteChunk(dev, chunkInNAND, 1,
						  __LINE__);
				return YAFFS_OK;
			}
		}

	}

	if (existingChunk == 0)
		in->nDataChunks++;

	yaffs_LoadLevel0Tnode(dev, tn, chunkInInode, chunkInNAND);

	return YAFFS_OK;
}

static int yaffs_ReadChunkDataFromObject(yaffs_Object *in, int chunkInInode,
					__u8 *buffer)
{
	int chunkInNAND = yaffs_FindChunkInFile(in, chunkInInode, NULL);

	if (chunkInNAND >= 0)
		return yaffs_ReadChunkWithTagsFromNAND(in->myDev, chunkInNAND,
						buffer, NULL);
	else {
		T(YAFFS_TRACE_NANDACCESS,
		  (TSTR("Chunk %d not found zero instead" TENDSTR),
		   chunkInNAND));
		
		memset(buffer, 0, in->myDev->nDataBytesPerChunk);
		return 0;
	}

}

void yaffs_DeleteChunk(yaffs_Device *dev, int chunkId, int markNAND, int lyn)
{
	int block;
	int page;
	yaffs_ExtendedTags tags;
	yaffs_BlockInfo *bi;

	if (chunkId <= 0)
		return;

	dev->nDeletions++;
	block = chunkId / dev->param.nChunksPerBlock;
	page = chunkId % dev->param.nChunksPerBlock;


	if (!yaffs_CheckChunkBit(dev, block, page))
		T(YAFFS_TRACE_VERIFY,
			(TSTR("Deleting invalid chunk %d"TENDSTR),
			 chunkId));

	bi = yaffs_GetBlockInfo(dev, block);
	
	yaffs2_UpdateOldestDirtySequence(dev, block, bi);

	T(YAFFS_TRACE_DELETION,
	  (TSTR("line %d delete of chunk %d" TENDSTR), lyn, chunkId));

	if (!dev->param.isYaffs2 && markNAND &&
	    bi->blockState != YAFFS_BLOCK_STATE_COLLECTING) {

		yaffs_InitialiseTags(&tags);

		tags.chunkDeleted = 1;

		yaffs_WriteChunkWithTagsToNAND(dev, chunkId, NULL, &tags);
		yaffs_HandleUpdateChunk(dev, chunkId, &tags);
	} else {
		dev->nUnmarkedDeletions++;
	}

	/* Pull out of the management area.
	 * If the whole block became dirty, this will kick off an erasure.
	 */
	if (bi->blockState == YAFFS_BLOCK_STATE_ALLOCATING ||
	    bi->blockState == YAFFS_BLOCK_STATE_FULL ||
	    bi->blockState == YAFFS_BLOCK_STATE_NEEDS_SCANNING ||
	    bi->blockState == YAFFS_BLOCK_STATE_COLLECTING) {
		dev->nFreeChunks++;

		yaffs_ClearChunkBit(dev, block, page);

		bi->pagesInUse--;

		if (bi->pagesInUse == 0 &&
		    !bi->hasShrinkHeader &&
		    bi->blockState != YAFFS_BLOCK_STATE_ALLOCATING &&
		    bi->blockState != YAFFS_BLOCK_STATE_NEEDS_SCANNING) {
			yaffs_BlockBecameDirty(dev, block);
		}

	}

}

static int yaffs_WriteChunkDataToObject(yaffs_Object *in, int chunkInInode,
					const __u8 *buffer, int nBytes,
					int useReserve)
{
	

	int prevChunkId;
	yaffs_ExtendedTags prevTags;

	int newChunkId;
	yaffs_ExtendedTags newTags;

	yaffs_Device *dev = in->myDev;

	yaffs_CheckGarbageCollection(dev,0);

	/* Get the previous chunk at this location in the file if it exists.
	 * If it does not exist then put a zero into the tree. This creates
	 * the tnode now, rather than later when it is harder to clean up.
	 */
	prevChunkId = yaffs_FindChunkInFile(in, chunkInInode, &prevTags);
	if(prevChunkId < 1 &&
		!yaffs_PutChunkIntoFile(in, chunkInInode, 0, 0))
		return 0;

	
	yaffs_InitialiseTags(&newTags);

	newTags.chunkId = chunkInInode;
	newTags.objectId = in->objectId;
	newTags.serialNumber =
	    (prevChunkId > 0) ? prevTags.serialNumber + 1 : 1;
	newTags.byteCount = nBytes;

	if (nBytes < 1 || nBytes > dev->param.totalBytesPerChunk) {
		T(YAFFS_TRACE_ERROR,
		(TSTR("Writing %d bytes to chunk!!!!!!!!!" TENDSTR), nBytes));
		YBUG();
	}
	
		
	newChunkId =
	    yaffs_WriteNewChunkWithTagsToNAND(dev, buffer, &newTags,
					      useReserve);

	if (newChunkId > 0) {
		yaffs_PutChunkIntoFile(in, chunkInInode, newChunkId, 0);

		if (prevChunkId > 0)
			yaffs_DeleteChunk(dev, prevChunkId, 1, __LINE__);

		yaffs_VerifyFileSanity(in);
	}
	return newChunkId;

}


int yaffs_UpdateObjectHeader(yaffs_Object *in, const YCHAR *name, int force,
			     int isShrink, int shadows, yaffs_XAttrMod *xmod)
{

	yaffs_BlockInfo *bi;

	yaffs_Device *dev = in->myDev;

	int prevChunkId;
	int retVal = 0;
	int result = 0;

	int newChunkId;
	yaffs_ExtendedTags newTags;
	yaffs_ExtendedTags oldTags;
	const YCHAR *alias = NULL;

	__u8 *buffer = NULL;
	YCHAR oldName[YAFFS_MAX_NAME_LENGTH + 1];

	yaffs_ObjectHeader *oh = NULL;

	yaffs_strcpy(oldName, _Y("silly old name"));


	if (!in->fake ||
		in == dev->rootDir || 
		force  || xmod) {

		yaffs_CheckGarbageCollection(dev,0);
		yaffs_CheckObjectDetailsLoaded(in);

		buffer = yaffs_GetTempBuffer(in->myDev, __LINE__);
		oh = (yaffs_ObjectHeader *) buffer;

		prevChunkId = in->hdrChunk;

		if (prevChunkId > 0) {
			result = yaffs_ReadChunkWithTagsFromNAND(dev, prevChunkId,
							buffer, &oldTags);

			yaffs_VerifyObjectHeader(in, oh, &oldTags, 0);

			memcpy(oldName, oh->name, sizeof(oh->name));
			memset(buffer, 0xFF, sizeof(yaffs_ObjectHeader));
		} else
			memset(buffer, 0xFF, dev->nDataBytesPerChunk);

		oh->type = in->variantType;
		oh->yst_mode = in->yst_mode;
		oh->shadowsObject = oh->inbandShadowsObject = shadows;

#ifdef CONFIG_YAFFS_WINCE
		oh->win_atime[0] = in->win_atime[0];
		oh->win_ctime[0] = in->win_ctime[0];
		oh->win_mtime[0] = in->win_mtime[0];
		oh->win_atime[1] = in->win_atime[1];
		oh->win_ctime[1] = in->win_ctime[1];
		oh->win_mtime[1] = in->win_mtime[1];
#else
		oh->yst_uid = in->yst_uid;
		oh->yst_gid = in->yst_gid;
		oh->yst_atime = in->yst_atime;
		oh->yst_mtime = in->yst_mtime;
		oh->yst_ctime = in->yst_ctime;
		oh->yst_rdev = in->yst_rdev;
#endif
		if (in->parent)
			oh->parentObjectId = in->parent->objectId;
		else
			oh->parentObjectId = 0;

		if (name && *name) {
			memset(oh->name, 0, sizeof(oh->name));
			yaffs_LoadObjectHeaderFromName(dev,oh->name,name);
		} else if (prevChunkId > 0)
			memcpy(oh->name, oldName, sizeof(oh->name));
		else
			memset(oh->name, 0, sizeof(oh->name));

		oh->isShrink = isShrink;

		switch (in->variantType) {
		case YAFFS_OBJECT_TYPE_UNKNOWN:
			
			break;
		case YAFFS_OBJECT_TYPE_FILE:
			oh->fileSize =
			    (oh->parentObjectId == YAFFS_OBJECTID_DELETED
			     || oh->parentObjectId ==
			     YAFFS_OBJECTID_UNLINKED) ? 0 : in->variant.
			    fileVariant.fileSize;
			break;
		case YAFFS_OBJECT_TYPE_HARDLINK:
			oh->equivalentObjectId =
			    in->variant.hardLinkVariant.equivalentObjectId;
			break;
		case YAFFS_OBJECT_TYPE_SPECIAL:
			
			break;
		case YAFFS_OBJECT_TYPE_DIRECTORY:
			
			break;
		case YAFFS_OBJECT_TYPE_SYMLINK:
			alias = in->variant.symLinkVariant.alias;
			if(!alias)
				alias = _Y("no alias");
			yaffs_strncpy(oh->alias,
					alias,
				      YAFFS_MAX_ALIAS_LENGTH);
			oh->alias[YAFFS_MAX_ALIAS_LENGTH] = 0;
			break;
		}

		
		if(xmod)
			yaffs_ApplyXMod(in, (char *)buffer, xmod);


		
		yaffs_InitialiseTags(&newTags);
		in->serial++;
		newTags.chunkId = 0;
		newTags.objectId = in->objectId;
		newTags.serialNumber = in->serial;

		

		newTags.extraHeaderInfoAvailable = 1;
		newTags.extraParentObjectId = oh->parentObjectId;
		newTags.extraFileLength = oh->fileSize;
		newTags.extraIsShrinkHeader = oh->isShrink;
		newTags.extraEquivalentObjectId = oh->equivalentObjectId;
		newTags.extraShadows = (oh->shadowsObject > 0) ? 1 : 0;
		newTags.extraObjectType = in->variantType;

		yaffs_VerifyObjectHeader(in, oh, &newTags, 1);

		
		newChunkId =
		    yaffs_WriteNewChunkWithTagsToNAND(dev, buffer, &newTags,
						      (prevChunkId > 0) ? 1 : 0);

		if (newChunkId >= 0) {

			in->hdrChunk = newChunkId;

			if (prevChunkId > 0) {
				yaffs_DeleteChunk(dev, prevChunkId, 1,
						  __LINE__);
			}

			if (!yaffs_ObjectHasCachedWriteData(in))
				in->dirty = 0;

			
			if (isShrink) {
				bi = yaffs_GetBlockInfo(in->myDev,
					newChunkId / in->myDev->param.nChunksPerBlock);
				bi->hasShrinkHeader = 1;
			}

		}

		retVal = newChunkId;

	}

	if (buffer)
		yaffs_ReleaseTempBuffer(dev, buffer, __LINE__);

	return retVal;
}

/*------------------------ Short Operations Cache ----------------------------------------
 *   In many situations where there is no high level buffering (eg WinCE) a lot of
 *   reads might be short sequential reads, and a lot of writes may be short
 *   sequential writes. eg. scanning/writing a jpeg file.
 *   In these cases, a short read/write cache can provide a huge perfomance benefit
 *   with dumb-as-a-rock code.
 *   In Linux, the page cache provides read buffering aand the short op cache provides write
 *   buffering.
 *
 *   There are a limited number (~10) of cache chunks per device so that we don't
 *   need a very intelligent search.
 */

static int yaffs_ObjectHasCachedWriteData(yaffs_Object *obj)
{
	yaffs_Device *dev = obj->myDev;
	int i;
	yaffs_ChunkCache *cache;
	int nCaches = obj->myDev->param.nShortOpCaches;

	for (i = 0; i < nCaches; i++) {
		cache = &dev->srCache[i];
		if (cache->object == obj &&
		    cache->dirty)
			return 1;
	}

	return 0;
}


static void yaffs_FlushFilesChunkCache(yaffs_Object *obj)
{
	yaffs_Device *dev = obj->myDev;
	int lowest = -99;	
	int i;
	yaffs_ChunkCache *cache;
	int chunkWritten = 0;
	int nCaches = obj->myDev->param.nShortOpCaches;

	if (nCaches > 0) {
		do {
			cache = NULL;

			
			for (i = 0; i < nCaches; i++) {
				if (dev->srCache[i].object == obj &&
				    dev->srCache[i].dirty) {
					if (!cache
					    || dev->srCache[i].chunkId <
					    lowest) {
						cache = &dev->srCache[i];
						lowest = cache->chunkId;
					}
				}
			}

			if (cache && !cache->locked) {
				

				chunkWritten =
				    yaffs_WriteChunkDataToObject(cache->object,
								 cache->chunkId,
								 cache->data,
								 cache->nBytes,
								 1);
				cache->dirty = 0;
				cache->object = NULL;
			}

		} while (cache && chunkWritten > 0);

		if (cache) {
			
			T(YAFFS_TRACE_ERROR,
			  (TSTR("yaffs tragedy: no space during cache write" TENDSTR)));

		}
	}

}



void yaffs_FlushEntireDeviceCache(yaffs_Device *dev)
{
	yaffs_Object *obj;
	int nCaches = dev->param.nShortOpCaches;
	int i;

	/* Find a dirty object in the cache and flush it...
	 * until there are no further dirty objects.
	 */
	do {
		obj = NULL;
		for (i = 0; i < nCaches && !obj; i++) {
			if (dev->srCache[i].object &&
			    dev->srCache[i].dirty)
				obj = dev->srCache[i].object;

		}
		if (obj)
			yaffs_FlushFilesChunkCache(obj);

	} while (obj);

}



static yaffs_ChunkCache *yaffs_GrabChunkCacheWorker(yaffs_Device *dev)
{
	int i;

	if (dev->param.nShortOpCaches > 0) {
		for (i = 0; i < dev->param.nShortOpCaches; i++) {
			if (!dev->srCache[i].object)
				return &dev->srCache[i];
		}
	}

	return NULL;
}

static yaffs_ChunkCache *yaffs_GrabChunkCache(yaffs_Device *dev)
{
	yaffs_ChunkCache *cache;
	yaffs_Object *theObj;
	int usage;
	int i;
	int pushout;

	if (dev->param.nShortOpCaches > 0) {
		

		cache = yaffs_GrabChunkCacheWorker(dev);

		if (!cache) {
			/* They were all dirty, find the last recently used object and flush
			 * its cache, then  find again.
			 * NB what's here is not very accurate, we actually flush the object
			 * the last recently used page.
			 */

			

			theObj = NULL;
			usage = -1;
			cache = NULL;
			pushout = -1;

			for (i = 0; i < dev->param.nShortOpCaches; i++) {
				if (dev->srCache[i].object &&
				    !dev->srCache[i].locked &&
				    (dev->srCache[i].lastUse < usage || !cache)) {
					usage = dev->srCache[i].lastUse;
					theObj = dev->srCache[i].object;
					cache = &dev->srCache[i];
					pushout = i;
				}
			}

			if (!cache || cache->dirty) {
				
				yaffs_FlushFilesChunkCache(theObj);
				cache = yaffs_GrabChunkCacheWorker(dev);
			}

		}
		return cache;
	} else
		return NULL;

}


static yaffs_ChunkCache *yaffs_FindChunkCache(const yaffs_Object *obj,
					      int chunkId)
{
	yaffs_Device *dev = obj->myDev;
	int i;
	if (dev->param.nShortOpCaches > 0) {
		for (i = 0; i < dev->param.nShortOpCaches; i++) {
			if (dev->srCache[i].object == obj &&
			    dev->srCache[i].chunkId == chunkId) {
				dev->cacheHits++;

				return &dev->srCache[i];
			}
		}
	}
	return NULL;
}


static void yaffs_UseChunkCache(yaffs_Device *dev, yaffs_ChunkCache *cache,
				int isAWrite)
{

	if (dev->param.nShortOpCaches > 0) {
		if (dev->srLastUse < 0 || dev->srLastUse > 100000000) {
			
			int i;
			for (i = 1; i < dev->param.nShortOpCaches; i++)
				dev->srCache[i].lastUse = 0;

			dev->srLastUse = 0;
		}

		dev->srLastUse++;

		cache->lastUse = dev->srLastUse;

		if (isAWrite)
			cache->dirty = 1;
	}
}


static void yaffs_InvalidateChunkCache(yaffs_Object *object, int chunkId)
{
	if (object->myDev->param.nShortOpCaches > 0) {
		yaffs_ChunkCache *cache = yaffs_FindChunkCache(object, chunkId);

		if (cache)
			cache->object = NULL;
	}
}


static void yaffs_InvalidateWholeChunkCache(yaffs_Object *in)
{
	int i;
	yaffs_Device *dev = in->myDev;

	if (dev->param.nShortOpCaches > 0) {
		
		for (i = 0; i < dev->param.nShortOpCaches; i++) {
			if (dev->srCache[i].object == in)
				dev->srCache[i].object = NULL;
		}
	}
}




int yaffs_ReadDataFromFile(yaffs_Object *in, __u8 *buffer, loff_t offset,
			int nBytes)
{

	int chunk;
	__u32 start;
	int nToCopy;
	int n = nBytes;
	int nDone = 0;
	yaffs_ChunkCache *cache;

	yaffs_Device *dev;

	dev = in->myDev;

	while (n > 0) {
		
		
		yaffs_AddrToChunk(dev, offset, &chunk, &start);
		chunk++;

		
		if ((start + n) < dev->nDataBytesPerChunk)
			nToCopy = n;
		else
			nToCopy = dev->nDataBytesPerChunk - start;

		cache = yaffs_FindChunkCache(in, chunk);

		
		if (cache || nToCopy != dev->nDataBytesPerChunk || dev->param.inbandTags) {
			if (dev->param.nShortOpCaches > 0) {

				

				if (!cache) {
					cache = yaffs_GrabChunkCache(in->myDev);
					cache->object = in;
					cache->chunkId = chunk;
					cache->dirty = 0;
					cache->locked = 0;
					yaffs_ReadChunkDataFromObject(in, chunk,
								      cache->
								      data);
					cache->nBytes = 0;
				}

				yaffs_UseChunkCache(dev, cache, 0);

				cache->locked = 1;


				memcpy(buffer, &cache->data[start], nToCopy);

				cache->locked = 0;
			} else {
				

				__u8 *localBuffer =
				    yaffs_GetTempBuffer(dev, __LINE__);
				yaffs_ReadChunkDataFromObject(in, chunk,
							      localBuffer);

				memcpy(buffer, &localBuffer[start], nToCopy);


				yaffs_ReleaseTempBuffer(dev, localBuffer,
							__LINE__);
			}

		} else {

			
			yaffs_ReadChunkDataFromObject(in, chunk, buffer);

		}

		n -= nToCopy;
		offset += nToCopy;
		buffer += nToCopy;
		nDone += nToCopy;

	}

	return nDone;
}

int yaffs_DoWriteDataToFile(yaffs_Object *in, const __u8 *buffer, loff_t offset,
			int nBytes, int writeThrough)
{

	int chunk;
	__u32 start;
	int nToCopy;
	int n = nBytes;
	int nDone = 0;
	int nToWriteBack;
	int startOfWrite = offset;
	int chunkWritten = 0;
	__u32 nBytesRead;
	__u32 chunkStart;

	yaffs_Device *dev;

	dev = in->myDev;

	while (n > 0 && chunkWritten >= 0) {
		yaffs_AddrToChunk(dev, offset, &chunk, &start);

		if (chunk * dev->nDataBytesPerChunk + start != offset ||
				start >= dev->nDataBytesPerChunk) {
			T(YAFFS_TRACE_ERROR, (
			   TSTR("AddrToChunk of offset %d gives chunk %d start %d"
			   TENDSTR),
			   (int)offset, chunk, start));
		}
		chunk++; 

		/* OK now check for the curveball where the start and end are in
		 * the same chunk.
		 */

		if ((start + n) < dev->nDataBytesPerChunk) {
			nToCopy = n;

			/* Now folks, to calculate how many bytes to write back....
			 * If we're overwriting and not writing to then end of file then
			 * we need to write back as much as was there before.
			 */

			chunkStart = ((chunk - 1) * dev->nDataBytesPerChunk);

			if (chunkStart > in->variant.fileVariant.fileSize)
				nBytesRead = 0; 
			else
				nBytesRead = in->variant.fileVariant.fileSize - chunkStart;

			if (nBytesRead > dev->nDataBytesPerChunk)
				nBytesRead = dev->nDataBytesPerChunk;

			nToWriteBack =
			    (nBytesRead >
			     (start + n)) ? nBytesRead : (start + n);

			if (nToWriteBack < 0 || nToWriteBack > dev->nDataBytesPerChunk)
				YBUG();

		} else {
			nToCopy = dev->nDataBytesPerChunk - start;
			nToWriteBack = dev->nDataBytesPerChunk;
		}

		if (nToCopy != dev->nDataBytesPerChunk || dev->param.inbandTags) {
			
			if (dev->param.nShortOpCaches > 0) {
				yaffs_ChunkCache *cache;
				
				cache = yaffs_FindChunkCache(in, chunk);

				if (!cache
				    && yaffs_CheckSpaceForAllocation(dev, 1)) {
					cache = yaffs_GrabChunkCache(dev);
					cache->object = in;
					cache->chunkId = chunk;
					cache->dirty = 0;
					cache->locked = 0;
					yaffs_ReadChunkDataFromObject(in, chunk,
								      cache->data);
				} else if (cache &&
					!cache->dirty &&
					!yaffs_CheckSpaceForAllocation(dev, 1)) {
					
					 cache = NULL;
				}

				if (cache) {
					yaffs_UseChunkCache(dev, cache, 1);
					cache->locked = 1;


					memcpy(&cache->data[start], buffer,
					       nToCopy);


					cache->locked = 0;
					cache->nBytes = nToWriteBack;

					if (writeThrough) {
						chunkWritten =
						    yaffs_WriteChunkDataToObject
						    (cache->object,
						     cache->chunkId,
						     cache->data, cache->nBytes,
						     1);
						cache->dirty = 0;
					}

				} else {
					chunkWritten = -1;	
				}
			} else {
				/* An incomplete start or end chunk (or maybe both start and end chunk)
				 * Read into the local buffer then copy, then copy over and write back.
				 */

				__u8 *localBuffer =
				    yaffs_GetTempBuffer(dev, __LINE__);

				yaffs_ReadChunkDataFromObject(in, chunk,
							      localBuffer);



				memcpy(&localBuffer[start], buffer, nToCopy);

				chunkWritten =
				    yaffs_WriteChunkDataToObject(in, chunk,
								 localBuffer,
								 nToWriteBack,
								 0);

				yaffs_ReleaseTempBuffer(dev, localBuffer,
							__LINE__);

			}

		} else {
			



			chunkWritten =
			    yaffs_WriteChunkDataToObject(in, chunk, buffer,
							 dev->nDataBytesPerChunk,
							 0);

			
			yaffs_InvalidateChunkCache(in, chunk);
		}

		if (chunkWritten >= 0) {
			n -= nToCopy;
			offset += nToCopy;
			buffer += nToCopy;
			nDone += nToCopy;
		}

	}

	

	if ((startOfWrite + nDone) > in->variant.fileVariant.fileSize)
		in->variant.fileVariant.fileSize = (startOfWrite + nDone);

	in->dirty = 1;

	return nDone;
}

int yaffs_WriteDataToFile(yaffs_Object *in, const __u8 *buffer, loff_t offset,
			int nBytes, int writeThrough)
{
	yaffs2_HandleHole(in,offset);
	return yaffs_DoWriteDataToFile(in,buffer,offset,nBytes,writeThrough);
}





static void yaffs_PruneResizedChunks(yaffs_Object *in, int newSize)
{

	yaffs_Device *dev = in->myDev;
	int oldFileSize = in->variant.fileVariant.fileSize;

	int lastDel = 1 + (oldFileSize - 1) / dev->nDataBytesPerChunk;

	int startDel = 1 + (newSize + dev->nDataBytesPerChunk - 1) /
	    dev->nDataBytesPerChunk;
	int i;
	int chunkId;

	
	for (i = lastDel; i >= startDel; i--) {
		

		chunkId = yaffs_FindAndDeleteChunkInFile(in, i, NULL);
		if (chunkId > 0) {
			if (chunkId <
			    (dev->internalStartBlock * dev->param.nChunksPerBlock)
			    || chunkId >=
			    ((dev->internalEndBlock +
			      1) * dev->param.nChunksPerBlock)) {
				T(YAFFS_TRACE_ALWAYS,
				  (TSTR("Found daft chunkId %d for %d" TENDSTR),
				   chunkId, i));
			} else {
				in->nDataChunks--;
				yaffs_DeleteChunk(dev, chunkId, 1, __LINE__);
			}
		}
	}

}


void yaffs_ResizeDown( yaffs_Object *obj, loff_t newSize)
{
	int newFullChunks;
	__u32 newSizeOfPartialChunk;
	yaffs_Device *dev = obj->myDev;

	yaffs_AddrToChunk(dev, newSize, &newFullChunks, &newSizeOfPartialChunk);

	yaffs_PruneResizedChunks(obj, newSize);

	if (newSizeOfPartialChunk != 0) {
		int lastChunk = 1 + newFullChunks;
		__u8 *localBuffer = yaffs_GetTempBuffer(dev, __LINE__);

		
		yaffs_ReadChunkDataFromObject(obj, lastChunk, localBuffer);
		memset(localBuffer + newSizeOfPartialChunk, 0,
			dev->nDataBytesPerChunk - newSizeOfPartialChunk);

		yaffs_WriteChunkDataToObject(obj, lastChunk, localBuffer,
					     newSizeOfPartialChunk, 1);

		yaffs_ReleaseTempBuffer(dev, localBuffer, __LINE__);
	}

	obj->variant.fileVariant.fileSize = newSize;

	yaffs_PruneFileStructure(dev, &obj->variant.fileVariant);
}


int yaffs_ResizeFile(yaffs_Object *in, loff_t newSize)
{
	yaffs_Device *dev = in->myDev;
	int oldFileSize = in->variant.fileVariant.fileSize;

	yaffs_FlushFilesChunkCache(in);
	yaffs_InvalidateWholeChunkCache(in);

	yaffs_CheckGarbageCollection(dev,0);

	if (in->variantType != YAFFS_OBJECT_TYPE_FILE)
		return YAFFS_FAIL;

	if (newSize == oldFileSize)
		return YAFFS_OK;
		
	if(newSize > oldFileSize){
		yaffs2_HandleHole(in,newSize);
		in->variant.fileVariant.fileSize = newSize;
	} else {
		 
		yaffs_ResizeDown(in, newSize);
	} 

	
	if (in->parent &&
	    !in->isShadowed &&
	    in->parent->objectId != YAFFS_OBJECTID_UNLINKED &&
	    in->parent->objectId != YAFFS_OBJECTID_DELETED)
		yaffs_UpdateObjectHeader(in, NULL, 0, 0, 0, NULL);


	return YAFFS_OK;
}

loff_t yaffs_GetFileSize(yaffs_Object *obj)
{
	YCHAR *alias = NULL;
	obj = yaffs_GetEquivalentObject(obj);

	switch (obj->variantType) {
	case YAFFS_OBJECT_TYPE_FILE:
		return obj->variant.fileVariant.fileSize;
	case YAFFS_OBJECT_TYPE_SYMLINK:
		alias = obj->variant.symLinkVariant.alias;
		if(!alias)
			return 0;
		return yaffs_strnlen(alias,YAFFS_MAX_ALIAS_LENGTH);
	default:
		return 0;
	}
}



int yaffs_FlushFile(yaffs_Object *in, int updateTime, int dataSync)
{
	int retVal;
	if (in->dirty) {
		yaffs_FlushFilesChunkCache(in);
		if(dataSync) 
			retVal=YAFFS_OK;
		else {
			if (updateTime) {
#ifdef CONFIG_YAFFS_WINCE
				yfsd_WinFileTimeNow(in->win_mtime);
#else

				in->yst_mtime = Y_CURRENT_TIME;

#endif
			}

			retVal = (yaffs_UpdateObjectHeader(in, NULL, 0, 0, 0, NULL) >=
				0) ? YAFFS_OK : YAFFS_FAIL;
		}
	} else {
		retVal = YAFFS_OK;
	}

	return retVal;

}

static int yaffs_DoGenericObjectDeletion(yaffs_Object *in)
{

	
	yaffs_InvalidateWholeChunkCache(in);

	if (in->myDev->param.isYaffs2 && (in->parent != in->myDev->deletedDir)) {
		
		yaffs_ChangeObjectName(in, in->myDev->deletedDir, _Y("deleted"), 0, 0);

	}

	yaffs_RemoveObjectFromDirectory(in);
	yaffs_DeleteChunk(in->myDev, in->hdrChunk, 1, __LINE__);
	in->hdrChunk = 0;

	yaffs_FreeObject(in);
	return YAFFS_OK;

}


static int yaffs_UnlinkFileIfNeeded(yaffs_Object *in)
{

	int retVal;
	int immediateDeletion = 0;
	yaffs_Device *dev = in->myDev;

	if (!in->myInode)
		immediateDeletion = 1;

	if (immediateDeletion) {
		retVal =
		    yaffs_ChangeObjectName(in, in->myDev->deletedDir,
					   _Y("deleted"), 0, 0);
		T(YAFFS_TRACE_TRACING,
		  (TSTR("yaffs: immediate deletion of file %d" TENDSTR),
		   in->objectId));
		in->deleted = 1;
		in->myDev->nDeletedFiles++;
		if (dev->param.disableSoftDelete || dev->param.isYaffs2)
			yaffs_ResizeFile(in, 0);
		yaffs_SoftDeleteFile(in);
	} else {
		retVal =
		    yaffs_ChangeObjectName(in, in->myDev->unlinkedDir,
					   _Y("unlinked"), 0, 0);
	}


	return retVal;
}

int yaffs_DeleteFile(yaffs_Object *in)
{
	int retVal = YAFFS_OK;
	int deleted; 
	yaffs_Device *dev = in->myDev;

	if (dev->param.disableSoftDelete || dev->param.isYaffs2)
		yaffs_ResizeFile(in, 0);

	if (in->nDataChunks > 0) {
		
		if (!in->unlinked)
			retVal = yaffs_UnlinkFileIfNeeded(in);

		deleted = in->deleted;

		if (retVal == YAFFS_OK && in->unlinked && !in->deleted) {
			in->deleted = 1;
			deleted = 1;
			in->myDev->nDeletedFiles++;
			yaffs_SoftDeleteFile(in);
		}
		return deleted ? YAFFS_OK : YAFFS_FAIL;
	} else {
		
		yaffs_FreeTnode(in->myDev, in->variant.fileVariant.top);
		in->variant.fileVariant.top = NULL;
		yaffs_DoGenericObjectDeletion(in);

		return YAFFS_OK;
	}
}

static int yaffs_IsNonEmptyDirectory(yaffs_Object *obj)
{
	return (obj->variantType == YAFFS_OBJECT_TYPE_DIRECTORY) &&
		!(ylist_empty(&obj->variant.directoryVariant.children));
}

static int yaffs_DeleteDirectory(yaffs_Object *obj)
{
	
	if (yaffs_IsNonEmptyDirectory(obj))
		return YAFFS_FAIL;

	return yaffs_DoGenericObjectDeletion(obj);
}

static int yaffs_DeleteSymLink(yaffs_Object *in)
{
	if(in->variant.symLinkVariant.alias)
		YFREE(in->variant.symLinkVariant.alias);
	in->variant.symLinkVariant.alias=NULL;

	return yaffs_DoGenericObjectDeletion(in);
}

static int yaffs_DeleteHardLink(yaffs_Object *in)
{
	
	ylist_del_init(&in->hardLinks);
	return yaffs_DoGenericObjectDeletion(in);
}

int yaffs_DeleteObject(yaffs_Object *obj)
{
int retVal = -1;
	switch (obj->variantType) {
	case YAFFS_OBJECT_TYPE_FILE:
		retVal = yaffs_DeleteFile(obj);
		break;
	case YAFFS_OBJECT_TYPE_DIRECTORY:
		if(!ylist_empty(&obj->variant.directoryVariant.dirty)){
			T(YAFFS_TRACE_BACKGROUND, (TSTR("Remove object %d from dirty directories" TENDSTR),obj->objectId));
			ylist_del_init(&obj->variant.directoryVariant.dirty);
		}
		return yaffs_DeleteDirectory(obj);
		break;
	case YAFFS_OBJECT_TYPE_SYMLINK:
		retVal = yaffs_DeleteSymLink(obj);
		break;
	case YAFFS_OBJECT_TYPE_HARDLINK:
		retVal = yaffs_DeleteHardLink(obj);
		break;
	case YAFFS_OBJECT_TYPE_SPECIAL:
		retVal = yaffs_DoGenericObjectDeletion(obj);
		break;
	case YAFFS_OBJECT_TYPE_UNKNOWN:
		retVal = 0;
		break;		
	}

	return retVal;
}

static int yaffs_UnlinkWorker(yaffs_Object *obj)
{

	int immediateDeletion = 0;

	if (!obj->myInode)
		immediateDeletion = 1;

	if(obj)
		yaffs_UpdateParent(obj->parent);

	if (obj->variantType == YAFFS_OBJECT_TYPE_HARDLINK) {
		return yaffs_DeleteHardLink(obj);
	} else if (!ylist_empty(&obj->hardLinks)) {
		/* Curve ball: We're unlinking an object that has a hardlink.
		 *
		 * This problem arises because we are not strictly following
		 * The Linux link/inode model.
		 *
		 * We can't really delete the object.
		 * Instead, we do the following:
		 * - Select a hardlink.
		 * - Unhook it from the hard links
		 * - Move it from its parent directory (so that the rename can work)
		 * - Rename the object to the hardlink's name.
		 * - Delete the hardlink
		 */

		yaffs_Object *hl;
		yaffs_Object *parent;
		int retVal;
		YCHAR name[YAFFS_MAX_NAME_LENGTH + 1];

		hl = ylist_entry(obj->hardLinks.next, yaffs_Object, hardLinks);

		yaffs_GetObjectName(hl, name, YAFFS_MAX_NAME_LENGTH + 1);
		parent = hl->parent;

		ylist_del_init(&hl->hardLinks);

 		yaffs_AddObjectToDirectory(obj->myDev->unlinkedDir, hl);

		retVal = yaffs_ChangeObjectName(obj,parent, name, 0, 0);

		if (retVal == YAFFS_OK)
			retVal = yaffs_DoGenericObjectDeletion(hl);

		return retVal;

	} else if (immediateDeletion) {
		switch (obj->variantType) {
		case YAFFS_OBJECT_TYPE_FILE:
			return yaffs_DeleteFile(obj);
			break;
		case YAFFS_OBJECT_TYPE_DIRECTORY:
			ylist_del_init(&obj->variant.directoryVariant.dirty);
			return yaffs_DeleteDirectory(obj);
			break;
		case YAFFS_OBJECT_TYPE_SYMLINK:
			return yaffs_DeleteSymLink(obj);
			break;
		case YAFFS_OBJECT_TYPE_SPECIAL:
			return yaffs_DoGenericObjectDeletion(obj);
			break;
		case YAFFS_OBJECT_TYPE_HARDLINK:
		case YAFFS_OBJECT_TYPE_UNKNOWN:
		default:
			return YAFFS_FAIL;
		}
	} else if(yaffs_IsNonEmptyDirectory(obj))
		return YAFFS_FAIL;
	else
		return yaffs_ChangeObjectName(obj, obj->myDev->unlinkedDir,
					   _Y("unlinked"), 0, 0);
}


static int yaffs_UnlinkObject(yaffs_Object *obj)
{

	if (obj && obj->unlinkAllowed)
		return yaffs_UnlinkWorker(obj);

	return YAFFS_FAIL;

}
int yaffs_Unlink(yaffs_Object *dir, const YCHAR *name)
{
	yaffs_Object *obj;

	obj = yaffs_FindObjectByName(dir, name);
	return yaffs_UnlinkObject(obj);
}



void yaffs_HandleShadowedObject(yaffs_Device *dev, int objId,
				int backwardScanning)
{
	yaffs_Object *obj;

	if (!backwardScanning) {
		

	} else {
		/* Handle YAFFS2 case (backward scanning)
		 * If the shadowed object exists then ignore.
		 */
		obj = yaffs_FindObjectByNumber(dev, objId);
		if(obj)
			return;
	}

	/* Let's create it (if it does not exist) assuming it is a file so that it can do shrinking etc.
	 * We put it in unlinked dir to be cleaned up after the scanning
	 */
	obj =
	    yaffs_FindOrCreateObjectByNumber(dev, objId,
					     YAFFS_OBJECT_TYPE_FILE);
	if (!obj)
		return;
	obj->isShadowed = 1;
	yaffs_AddObjectToDirectory(dev->unlinkedDir, obj);
	obj->variant.fileVariant.shrinkSize = 0;
	obj->valid = 1;		

}


void yaffs_HardlinkFixup(yaffs_Device *dev, yaffs_Object *hardList)
{
	yaffs_Object *hl;
	yaffs_Object *in;

	while (hardList) {
		hl = hardList;
		hardList = (yaffs_Object *) (hardList->hardLinks.next);

		in = yaffs_FindObjectByNumber(dev,
					      hl->variant.hardLinkVariant.
					      equivalentObjectId);

		if (in) {
			
			hl->variant.hardLinkVariant.equivalentObject = in;
			ylist_add(&hl->hardLinks, &in->hardLinks);
		} else {
			
			hl->variant.hardLinkVariant.equivalentObject = NULL;
			YINIT_LIST_HEAD(&hl->hardLinks);

		}
	}
}


static void yaffs_StripDeletedObjects(yaffs_Device *dev)
{
	
	struct ylist_head *i;
	struct ylist_head *n;
	yaffs_Object *l;

	if (dev->readOnly)
		return;

	
	ylist_for_each_safe(i, n,
		&dev->unlinkedDir->variant.directoryVariant.children) {
		if (i) {
			l = ylist_entry(i, yaffs_Object, siblings);
			yaffs_DeleteObject(l);
		}
	}

	ylist_for_each_safe(i, n,
		&dev->deletedDir->variant.directoryVariant.children) {
		if (i) {
			l = ylist_entry(i, yaffs_Object, siblings);
			yaffs_DeleteObject(l);
		}
	}

}

/*
 * This code iterates through all the objects making sure that they are rooted.
 * Any unrooted objects are re-rooted in lost+found.
 * An object needs to be in one of:
 * - Directly under deleted, unlinked
 * - Directly or indirectly under root.
 *
 * Note:
 *  This code assumes that we don't ever change the current relationships between
 *  directories:
 *   rootDir->parent == unlinkedDir->parent == deletedDir->parent == NULL
 *   lostNfound->parent == rootDir
 *
 * This fixes the problem where directories might have inadvertently been deleted
 * leaving the object "hanging" without being rooted in the directory tree.
 */
 
static int yaffs_HasNULLParent(yaffs_Device *dev, yaffs_Object *obj)
{
	return (obj == dev->deletedDir ||
		obj == dev->unlinkedDir||
		obj == dev->rootDir);
}

static void yaffs_FixHangingObjects(yaffs_Device *dev)
{
	yaffs_Object *obj;
	yaffs_Object *parent;
	int i;
	struct ylist_head *lh;
	struct ylist_head *n;
	int depthLimit;
	int hanging;

	if (dev->readOnly)
		return;

	/* Iterate through the objects in each hash entry,
	 * looking at each object.
	 * Make sure it is rooted.
	 */

	for (i = 0; i <  YAFFS_NOBJECT_BUCKETS; i++) {
		ylist_for_each_safe(lh, n, &dev->objectBucket[i].list) {
			if (lh) {
				obj = ylist_entry(lh, yaffs_Object, hashLink);
				parent= obj->parent;
				
				if(yaffs_HasNULLParent(dev,obj)){
					
					hanging = 0;
				}
				else if(!parent || parent->variantType != YAFFS_OBJECT_TYPE_DIRECTORY)
					hanging = 1;
				else if(yaffs_HasNULLParent(dev,parent))
					hanging = 0;
				else {
					
					hanging = 0;
					depthLimit=100;

					while(parent != dev->rootDir &&
						parent->parent &&
						parent->parent->variantType == YAFFS_OBJECT_TYPE_DIRECTORY &&
						depthLimit > 0){
						parent = parent->parent;
						depthLimit--;
					}
					if(parent != dev->rootDir)
						hanging = 1;
				}
				if(hanging){
					T(YAFFS_TRACE_SCAN,
					  (TSTR("Hanging object %d moved to lost and found" TENDSTR),
					  	obj->objectId));
					yaffs_AddObjectToDirectory(dev->lostNFoundDir,obj);
				}
			}
		}
	}
}



static void yaffs_DeleteDirectoryContents(yaffs_Object *dir)
{
	yaffs_Object *obj;
	struct ylist_head *lh;
	struct ylist_head *n;

	if(dir->variantType != YAFFS_OBJECT_TYPE_DIRECTORY)
		YBUG();
	
	ylist_for_each_safe(lh, n, &dir->variant.directoryVariant.children) {
		if (lh) {
			obj = ylist_entry(lh, yaffs_Object, siblings);
			if(obj->variantType == YAFFS_OBJECT_TYPE_DIRECTORY)
				yaffs_DeleteDirectoryContents(obj);

			T(YAFFS_TRACE_SCAN,
				(TSTR("Deleting lost_found object %d" TENDSTR),
				obj->objectId));

			
			yaffs_UnlinkObject(obj); 
		}
	}
			
}

static void yaffs_EmptyLostAndFound(yaffs_Device *dev)
{
	yaffs_DeleteDirectoryContents(dev->lostNFoundDir);
}

static void yaffs_CheckObjectDetailsLoaded(yaffs_Object *in)
{
	__u8 *chunkData;
	yaffs_ObjectHeader *oh;
	yaffs_Device *dev;
	yaffs_ExtendedTags tags;
	int result;
	int alloc_failed = 0;

	if (!in)
		return;

	dev = in->myDev;

#if 0
	T(YAFFS_TRACE_SCAN, (TSTR("details for object %d %s loaded" TENDSTR),
		in->objectId,
		in->lazyLoaded ? "not yet" : "already"));
#endif

	if (in->lazyLoaded && in->hdrChunk > 0) {
		in->lazyLoaded = 0;
		chunkData = yaffs_GetTempBuffer(dev, __LINE__);

		result = yaffs_ReadChunkWithTagsFromNAND(dev, in->hdrChunk, chunkData, &tags);
		oh = (yaffs_ObjectHeader *) chunkData;

		in->yst_mode = oh->yst_mode;
#ifdef CONFIG_YAFFS_WINCE
		in->win_atime[0] = oh->win_atime[0];
		in->win_ctime[0] = oh->win_ctime[0];
		in->win_mtime[0] = oh->win_mtime[0];
		in->win_atime[1] = oh->win_atime[1];
		in->win_ctime[1] = oh->win_ctime[1];
		in->win_mtime[1] = oh->win_mtime[1];
#else
		in->yst_uid = oh->yst_uid;
		in->yst_gid = oh->yst_gid;
		in->yst_atime = oh->yst_atime;
		in->yst_mtime = oh->yst_mtime;
		in->yst_ctime = oh->yst_ctime;
		in->yst_rdev = oh->yst_rdev;

#endif
		yaffs_SetObjectNameFromOH(in, oh);

		if (in->variantType == YAFFS_OBJECT_TYPE_SYMLINK) {
			in->variant.symLinkVariant.alias =
						    yaffs_CloneString(oh->alias);
			if (!in->variant.symLinkVariant.alias)
				alloc_failed = 1; 
		}

		yaffs_ReleaseTempBuffer(dev, chunkData, __LINE__);
	}
}



/*
 *yaffs_UpdateParent() handles fixing a directories mtime and ctime when a new
 * link (ie. name) is created or deleted in the directory.
 *
 * ie.
 *   create dir/a : update dir's mtime/ctime
 *   rm dir/a:   update dir's mtime/ctime
 *   modify dir/a: don't update dir's mtimme/ctime
 *
 * This can be handled immediately or defered. Defering helps reduce the number
 * of updates when many files in a directory are changed within a brief period.
 *
 * If the directory updating is defered then yaffs_UpdateDirtyDirecories must be
 * called periodically.
 */
 
static void yaffs_UpdateParent(yaffs_Object *obj)
{
	yaffs_Device *dev;
	if(!obj)
		return;
#ifndef CONFIG_YAFFS_WINCE

	dev = obj->myDev;
	obj->dirty = 1;
	obj->yst_mtime = obj->yst_ctime = Y_CURRENT_TIME;
	if(dev->param.deferDirectoryUpdate){
		struct ylist_head *link = &obj->variant.directoryVariant.dirty; 
	
		if(ylist_empty(link)){
			ylist_add(link,&dev->dirtyDirectories);
			T(YAFFS_TRACE_BACKGROUND, (TSTR("Added object %d to dirty directories" TENDSTR),obj->objectId));
		}

	} else
		yaffs_UpdateObjectHeader(obj, NULL, 0, 0, 0, NULL);
#endif
}

void yaffs_UpdateDirtyDirectories(yaffs_Device *dev)
{
	struct ylist_head *link;
	yaffs_Object *obj;
	yaffs_DirectoryStructure *dS;
	yaffs_ObjectVariant *oV;

	T(YAFFS_TRACE_BACKGROUND, (TSTR("Update dirty directories" TENDSTR)));

	while(!ylist_empty(&dev->dirtyDirectories)){
		link = dev->dirtyDirectories.next;
		ylist_del_init(link);
		
		dS=ylist_entry(link,yaffs_DirectoryStructure,dirty);
		oV = ylist_entry(dS,yaffs_ObjectVariant,directoryVariant);
		obj = ylist_entry(oV,yaffs_Object,variant);

		T(YAFFS_TRACE_BACKGROUND, (TSTR("Update directory %d" TENDSTR), obj->objectId));

		if(obj->dirty)
			yaffs_UpdateObjectHeader(obj, NULL, 0, 0, 0, NULL);
	}
}

static void yaffs_RemoveObjectFromDirectory(yaffs_Object *obj)
{
	yaffs_Device *dev = obj->myDev;
	yaffs_Object *parent;

	yaffs_VerifyObjectInDirectory(obj);
	parent = obj->parent;

	yaffs_VerifyDirectory(parent);

	if (dev && dev->param.removeObjectCallback)
		dev->param.removeObjectCallback(obj);


	ylist_del_init(&obj->siblings);
	obj->parent = NULL;
	
	yaffs_VerifyDirectory(parent);
}

void yaffs_AddObjectToDirectory(yaffs_Object *directory,
					yaffs_Object *obj)
{
	if (!directory) {
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR
		   ("tragedy: Trying to add an object to a null pointer directory"
		    TENDSTR)));
		YBUG();
		return;
	}
	if (directory->variantType != YAFFS_OBJECT_TYPE_DIRECTORY) {
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR
		   ("tragedy: Trying to add an object to a non-directory"
		    TENDSTR)));
		YBUG();
	}

	if (obj->siblings.prev == NULL) {
		/* Not initialised */
		YBUG();
	}


	yaffs_VerifyDirectory(directory);

	yaffs_RemoveObjectFromDirectory(obj);


	/* Now add it */
	ylist_add(&obj->siblings, &directory->variant.directoryVariant.children);
	obj->parent = directory;

	if (directory == obj->myDev->unlinkedDir
			|| directory == obj->myDev->deletedDir) {
		obj->unlinked = 1;
		obj->myDev->nUnlinkedFiles++;
		obj->renameAllowed = 0;
	}

	yaffs_VerifyDirectory(directory);
	yaffs_VerifyObjectInDirectory(obj);
}

yaffs_Object *yaffs_FindObjectByName(yaffs_Object *directory,
				     const YCHAR *name)
{
	int sum;

	struct ylist_head *i;
	YCHAR buffer[YAFFS_MAX_NAME_LENGTH + 1];

	yaffs_Object *l;

	if (!name)
		return NULL;

	if (!directory) {
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR
		   ("tragedy: yaffs_FindObjectByName: null pointer directory"
		    TENDSTR)));
		YBUG();
		return NULL;
	}
	if (directory->variantType != YAFFS_OBJECT_TYPE_DIRECTORY) {
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR
		   ("tragedy: yaffs_FindObjectByName: non-directory" TENDSTR)));
		YBUG();
	}

	sum = yaffs_CalcNameSum(name);

	ylist_for_each(i, &directory->variant.directoryVariant.children) {
		if (i) {
			l = ylist_entry(i, yaffs_Object, siblings);

			if (l->parent != directory)
				YBUG();

			yaffs_CheckObjectDetailsLoaded(l);

			/* Special case for lost-n-found */
			if (l->objectId == YAFFS_OBJECTID_LOSTNFOUND) {
				if (yaffs_strcmp(name, YAFFS_LOSTNFOUND_NAME) == 0)
					return l;
			} else if (yaffs_SumCompare(l->sum, sum) || l->hdrChunk <= 0) {
				/* LostnFound chunk called Objxxx
				 * Do a real check
				 */
				yaffs_GetObjectName(l, buffer,
						    YAFFS_MAX_NAME_LENGTH + 1);
				if (yaffs_strncmp(name, buffer, YAFFS_MAX_NAME_LENGTH) == 0)
					return l;
			}
		}
	}

	return NULL;
}


#if 0
int yaffs_ApplyToDirectoryChildren(yaffs_Object *theDir,
					int (*fn) (yaffs_Object *))
{
	struct ylist_head *i;
	yaffs_Object *l;

	if (!theDir) {
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR
		   ("tragedy: yaffs_FindObjectByName: null pointer directory"
		    TENDSTR)));
		YBUG();
		return YAFFS_FAIL;
	}
	if (theDir->variantType != YAFFS_OBJECT_TYPE_DIRECTORY) {
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR
		   ("tragedy: yaffs_FindObjectByName: non-directory" TENDSTR)));
		YBUG();
		return YAFFS_FAIL;
	}

	ylist_for_each(i, &theDir->variant.directoryVariant.children) {
		if (i) {
			l = ylist_entry(i, yaffs_Object, siblings);
			if (l && !fn(l))
				return YAFFS_FAIL;
		}
	}

	return YAFFS_OK;

}
#endif

/* GetEquivalentObject dereferences any hard links to get to the
 * actual object.
 */

yaffs_Object *yaffs_GetEquivalentObject(yaffs_Object *obj)
{
	if (obj && obj->variantType == YAFFS_OBJECT_TYPE_HARDLINK) {
		/* We want the object id of the equivalent object, not this one */
		obj = obj->variant.hardLinkVariant.equivalentObject;
		yaffs_CheckObjectDetailsLoaded(obj);
	}
	return obj;
}

/*
 *  A note or two on object names.
 *  * If the object name is missing, we then make one up in the form objnnn
 *
 *  * ASCII names are stored in the object header's name field from byte zero
 *  * Unicode names are historically stored starting from byte zero.
 *
 * Then there are automatic Unicode names...
 * The purpose of these is to save names in a way that can be read as
 * ASCII or Unicode names as appropriate, thus allowing a Unicode and ASCII
 * system to share files.
 *
 * These automatic unicode are stored slightly differently...
 *  - If the name can fit in the ASCII character space then they are saved as 
 *    ascii names as per above.
 *  - If the name needs Unicode then the name is saved in Unicode
 *    starting at oh->name[1].

 */
static void yaffs_FixNullName(yaffs_Object * obj,YCHAR * name, int buffSize)
{
	
	if(yaffs_strnlen(name,YAFFS_MAX_NAME_LENGTH) == 0){
		YCHAR locName[20];
		YCHAR numString[20];
		YCHAR *x = &numString[19];
		unsigned v = obj->objectId;
		numString[19] = 0;
		while(v>0){
			x--;
			*x = '0' + (v % 10);
			v /= 10;
		}
		/* make up a name */
		yaffs_strcpy(locName, YAFFS_LOSTNFOUND_PREFIX);
		yaffs_strcat(locName,x);
		yaffs_strncpy(name, locName, buffSize - 1);
	}
}

static void yaffs_LoadNameFromObjectHeader(yaffs_Device *dev,YCHAR *name, const YCHAR *ohName, int bufferSize)
{
#ifdef CONFIG_YAFFS_AUTO_UNICODE
	if(dev->param.autoUnicode){
		if(*ohName){
			
			const char *asciiOhName = (const char *)ohName;
			int n = bufferSize - 1;
			while(n > 0 && *asciiOhName){
				*name = *asciiOhName;
				name++;
				asciiOhName++;
				n--;
			}
		} else 
			yaffs_strncpy(name,ohName+1, bufferSize -1);
	} else
#endif
		yaffs_strncpy(name, ohName, bufferSize - 1);
}


static void yaffs_LoadObjectHeaderFromName(yaffs_Device *dev, YCHAR *ohName, const YCHAR *name)
{
#ifdef CONFIG_YAFFS_AUTO_UNICODE

	int isAscii;
	YCHAR *w;

	if(dev->param.autoUnicode){

		isAscii = 1;
		w = name;
	
		
		while(isAscii && *w){
			if((*w) & 0xff00)
				isAscii = 0;
			w++;
		}

		if(isAscii){
			
			char *asciiOhName = (char *)ohName;
			int n = YAFFS_MAX_NAME_LENGTH  - 1;
			while(n > 0 && *name){
				*asciiOhName= *name;
				name++;
				asciiOhName++;
				n--;
			}
		} else{
			
			*ohName = 0;
			yaffs_strncpy(ohName+1,name, YAFFS_MAX_NAME_LENGTH -2);
		}
	}
	else 
#endif
		yaffs_strncpy(ohName,name, YAFFS_MAX_NAME_LENGTH - 1);

}

int yaffs_GetObjectName(yaffs_Object * obj, YCHAR * name, int buffSize)
{
	memset(name, 0, buffSize * sizeof(YCHAR));
	
	yaffs_CheckObjectDetailsLoaded(obj);

	if (obj->objectId == YAFFS_OBJECTID_LOSTNFOUND) {
		yaffs_strncpy(name, YAFFS_LOSTNFOUND_NAME, buffSize - 1);
	} 
#ifdef CONFIG_YAFFS_SHORT_NAMES_IN_RAM
	else if (obj->shortName[0]) {
		yaffs_strcpy(name, obj->shortName);
	}
#endif
	else if(obj->hdrChunk > 0) {
		int result;
		__u8 *buffer = yaffs_GetTempBuffer(obj->myDev, __LINE__);

		yaffs_ObjectHeader *oh = (yaffs_ObjectHeader *) buffer;

		memset(buffer, 0, obj->myDev->nDataBytesPerChunk);

		if (obj->hdrChunk > 0) {
			result = yaffs_ReadChunkWithTagsFromNAND(obj->myDev,
							obj->hdrChunk, buffer,
							NULL);
		}
		yaffs_LoadNameFromObjectHeader(obj->myDev,name,oh->name,buffSize);

		yaffs_ReleaseTempBuffer(obj->myDev, buffer, __LINE__);
	}

	yaffs_FixNullName(obj,name,buffSize);

	return yaffs_strnlen(name,YAFFS_MAX_NAME_LENGTH);
}


int yaffs_GetObjectFileLength(yaffs_Object *obj)
{
	/* Dereference any hard linking */
	obj = yaffs_GetEquivalentObject(obj);

	if (obj->variantType == YAFFS_OBJECT_TYPE_FILE)
		return obj->variant.fileVariant.fileSize;
	if (obj->variantType == YAFFS_OBJECT_TYPE_SYMLINK){
		if(!obj->variant.symLinkVariant.alias)
			return 0;
		return yaffs_strnlen(obj->variant.symLinkVariant.alias,YAFFS_MAX_ALIAS_LENGTH);
	} else {
		/* Only a directory should drop through to here */
		return obj->myDev->nDataBytesPerChunk;
	}
}

int yaffs_GetObjectLinkCount(yaffs_Object *obj)
{
	int count = 0;
	struct ylist_head *i;

	if (!obj->unlinked)
		count++;		/* the object itself */

	ylist_for_each(i, &obj->hardLinks)
		count++;		/* add the hard links; */

	return count;
}

int yaffs_GetObjectInode(yaffs_Object *obj)
{
	obj = yaffs_GetEquivalentObject(obj);

	return obj->objectId;
}

unsigned yaffs_GetObjectType(yaffs_Object *obj)
{
	obj = yaffs_GetEquivalentObject(obj);

	switch (obj->variantType) {
	case YAFFS_OBJECT_TYPE_FILE:
		return DT_REG;
		break;
	case YAFFS_OBJECT_TYPE_DIRECTORY:
		return DT_DIR;
		break;
	case YAFFS_OBJECT_TYPE_SYMLINK:
		return DT_LNK;
		break;
	case YAFFS_OBJECT_TYPE_HARDLINK:
		return DT_REG;
		break;
	case YAFFS_OBJECT_TYPE_SPECIAL:
		if (S_ISFIFO(obj->yst_mode))
			return DT_FIFO;
		if (S_ISCHR(obj->yst_mode))
			return DT_CHR;
		if (S_ISBLK(obj->yst_mode))
			return DT_BLK;
		if (S_ISSOCK(obj->yst_mode))
			return DT_SOCK;
	default:
		return DT_REG;
		break;
	}
}

YCHAR *yaffs_GetSymlinkAlias(yaffs_Object *obj)
{
	obj = yaffs_GetEquivalentObject(obj);
	if (obj->variantType == YAFFS_OBJECT_TYPE_SYMLINK)
		return yaffs_CloneString(obj->variant.symLinkVariant.alias);
	else
		return yaffs_CloneString(_Y(""));
}

#ifndef CONFIG_YAFFS_WINCE

int yaffs_SetAttributes(yaffs_Object *obj, struct iattr *attr)
{
	unsigned int valid = attr->ia_valid;

	if (valid & ATTR_MODE)
		obj->yst_mode = attr->ia_mode;
	if (valid & ATTR_UID)
		obj->yst_uid = attr->ia_uid;
	if (valid & ATTR_GID)
		obj->yst_gid = attr->ia_gid;

	if (valid & ATTR_ATIME)
		obj->yst_atime = Y_TIME_CONVERT(attr->ia_atime);
	if (valid & ATTR_CTIME)
		obj->yst_ctime = Y_TIME_CONVERT(attr->ia_ctime);
	if (valid & ATTR_MTIME)
		obj->yst_mtime = Y_TIME_CONVERT(attr->ia_mtime);

	if (valid & ATTR_SIZE)
		yaffs_ResizeFile(obj, attr->ia_size);

	yaffs_UpdateObjectHeader(obj, NULL, 1, 0, 0, NULL);

	return YAFFS_OK;

}
int yaffs_GetAttributes(yaffs_Object *obj, struct iattr *attr)
{
	unsigned int valid = 0;

	attr->ia_mode = obj->yst_mode;
	valid |= ATTR_MODE;
	attr->ia_uid = obj->yst_uid;
	valid |= ATTR_UID;
	attr->ia_gid = obj->yst_gid;
	valid |= ATTR_GID;

	Y_TIME_CONVERT(attr->ia_atime) = obj->yst_atime;
	valid |= ATTR_ATIME;
	Y_TIME_CONVERT(attr->ia_ctime) = obj->yst_ctime;
	valid |= ATTR_CTIME;
	Y_TIME_CONVERT(attr->ia_mtime) = obj->yst_mtime;
	valid |= ATTR_MTIME;

	attr->ia_size = yaffs_GetFileSize(obj);
	valid |= ATTR_SIZE;

	attr->ia_valid = valid;

	return YAFFS_OK;
}

#endif


static int yaffs_DoXMod(yaffs_Object *obj, int set, const YCHAR *name, const void *value, int size, int flags)
{
	yaffs_XAttrMod xmod;

	int result;

	xmod.set = set;
	xmod.name = name;
	xmod.data = value;
	xmod.size =  size;
	xmod.flags = flags;
	xmod.result = -ENOSPC;

	result = yaffs_UpdateObjectHeader(obj, NULL, 0, 0, 0, &xmod);

	if(result > 0)
		return xmod.result;
	else
		return -ENOSPC;
}

static int yaffs_ApplyXMod(yaffs_Object *obj, char *buffer, yaffs_XAttrMod *xmod)
{
	int retval = 0;
	int x_offs = sizeof(yaffs_ObjectHeader);
	yaffs_Device *dev = obj->myDev;
	int x_size = dev->nDataBytesPerChunk - sizeof(yaffs_ObjectHeader);

	char * x_buffer = buffer + x_offs;

	if(xmod->set)
		retval = nval_set(x_buffer, x_size, xmod->name, xmod->data, xmod->size, xmod->flags);
	else
		retval = nval_del(x_buffer, x_size, xmod->name);

	obj->hasXattr = nval_hasvalues(x_buffer, x_size);
	obj->xattrKnown = 1;

	xmod->result = retval;

	return retval;
}

static int yaffs_DoXFetch(yaffs_Object *obj, const YCHAR *name, void *value, int size)
{
	char *buffer = NULL;
	int result;
	yaffs_ExtendedTags tags;
	yaffs_Device *dev = obj->myDev;
	int x_offs = sizeof(yaffs_ObjectHeader);
	int x_size = dev->nDataBytesPerChunk - sizeof(yaffs_ObjectHeader);

	char * x_buffer;

	int retval = 0;

	if(obj->hdrChunk < 1)
		return -ENODATA;

	
	if(obj->xattrKnown && !obj->hasXattr){
		if(name)
			return -ENODATA;
		else
			return 0;
	}

	buffer = (char *) yaffs_GetTempBuffer(dev, __LINE__);
	if(!buffer)
		return -ENOMEM;

	result = yaffs_ReadChunkWithTagsFromNAND(dev,obj->hdrChunk, (__u8 *)buffer, &tags);

	if(result != YAFFS_OK)
		retval = -ENOENT;
	else{
		x_buffer =  buffer + x_offs;

		if (!obj->xattrKnown){
			obj->hasXattr = nval_hasvalues(x_buffer, x_size);
			obj->xattrKnown = 1;
		}

		if(name)
			retval = nval_get(x_buffer, x_size, name, value, size);
		else
			retval = nval_list(x_buffer, x_size, value,size);
	}
	yaffs_ReleaseTempBuffer(dev,(__u8 *)buffer,__LINE__);
	return retval;
}

int yaffs_SetXAttribute(yaffs_Object *obj, const YCHAR *name, const void * value, int size, int flags)
{
	return yaffs_DoXMod(obj, 1, name, value, size, flags);
}

int yaffs_RemoveXAttribute(yaffs_Object *obj, const YCHAR *name)
{
	return yaffs_DoXMod(obj, 0, name, NULL, 0, 0);
}

int yaffs_GetXAttribute(yaffs_Object *obj, const YCHAR *name, void *value, int size)
{
	return yaffs_DoXFetch(obj, name, value, size);
}

int yaffs_ListXAttributes(yaffs_Object *obj, char *buffer, int size)
{
	return yaffs_DoXFetch(obj, NULL, buffer,size);
}



#if 0
int yaffs_DumpObject(yaffs_Object *obj)
{
	YCHAR name[257];

	yaffs_GetObjectName(obj, name, YAFFS_MAX_NAME_LENGTH + 1);

	T(YAFFS_TRACE_ALWAYS,
	  (TSTR
	   ("Object %d, inode %d \"%s\"\n dirty %d valid %d serial %d sum %d"
	    " chunk %d type %d size %d\n"
	    TENDSTR), obj->objectId, yaffs_GetObjectInode(obj), name,
	   obj->dirty, obj->valid, obj->serial, obj->sum, obj->hdrChunk,
	   yaffs_GetObjectType(obj), yaffs_GetObjectFileLength(obj)));

	return YAFFS_OK;
}
#endif

/*---------------------------- Initialisation code -------------------------------------- */

static int yaffs_CheckDevFunctions(const yaffs_Device *dev)
{

	/* Common functions, gotta have */
	if (!dev->param.eraseBlockInNAND || !dev->param.initialiseNAND)
		return 0;

#ifdef CONFIG_YAFFS_YAFFS2

	/* Can use the "with tags" style interface for yaffs1 or yaffs2 */
	if (dev->param.writeChunkWithTagsToNAND &&
	    dev->param.readChunkWithTagsFromNAND &&
	    !dev->param.writeChunkToNAND &&
	    !dev->param.readChunkFromNAND &&
	    dev->param.markNANDBlockBad &&
	    dev->param.queryNANDBlock)
		return 1;
#endif

	/* Can use the "spare" style interface for yaffs1 */
	if (!dev->param.isYaffs2 &&
	    !dev->param.writeChunkWithTagsToNAND &&
	    !dev->param.readChunkWithTagsFromNAND &&
	    dev->param.writeChunkToNAND &&
	    dev->param.readChunkFromNAND &&
	    !dev->param.markNANDBlockBad &&
	    !dev->param.queryNANDBlock)
		return 1;

	return 0;	/* bad */
}


static int yaffs_CreateInitialDirectories(yaffs_Device *dev)
{
	/* Initialise the unlinked, deleted, root and lost and found directories */

	dev->lostNFoundDir = dev->rootDir =  NULL;
	dev->unlinkedDir = dev->deletedDir = NULL;

	dev->unlinkedDir =
	    yaffs_CreateFakeDirectory(dev, YAFFS_OBJECTID_UNLINKED, S_IFDIR);

	dev->deletedDir =
	    yaffs_CreateFakeDirectory(dev, YAFFS_OBJECTID_DELETED, S_IFDIR);

	dev->rootDir =
	    yaffs_CreateFakeDirectory(dev, YAFFS_OBJECTID_ROOT,
				      YAFFS_ROOT_MODE | S_IFDIR);
	dev->lostNFoundDir =
	    yaffs_CreateFakeDirectory(dev, YAFFS_OBJECTID_LOSTNFOUND,
				      YAFFS_LOSTNFOUND_MODE | S_IFDIR);

	if (dev->lostNFoundDir && dev->rootDir && dev->unlinkedDir && dev->deletedDir) {
		yaffs_AddObjectToDirectory(dev->rootDir, dev->lostNFoundDir);
		return YAFFS_OK;
	}

	return YAFFS_FAIL;
}

int yaffs_GutsInitialise(yaffs_Device *dev)
{
	int init_failed = 0;
	unsigned x;
	int bits;

	T(YAFFS_TRACE_TRACING, (TSTR("yaffs: yaffs_GutsInitialise()" TENDSTR)));

	/* Check stuff that must be set */

	if (!dev) {
		T(YAFFS_TRACE_ALWAYS, (TSTR("yaffs: Need a device" TENDSTR)));
		return YAFFS_FAIL;
	}

	dev->internalStartBlock = dev->param.startBlock;
	dev->internalEndBlock = dev->param.endBlock;
	dev->blockOffset = 0;
	dev->chunkOffset = 0;
	dev->nFreeChunks = 0;

	dev->gcBlock = 0;

	if (dev->param.startBlock == 0) {
		dev->internalStartBlock = dev->param.startBlock + 1;
		dev->internalEndBlock = dev->param.endBlock + 1;
		dev->blockOffset = 1;
		dev->chunkOffset = dev->param.nChunksPerBlock;
	}

	/* Check geometry parameters. */

	if ((!dev->param.inbandTags && dev->param.isYaffs2 && dev->param.totalBytesPerChunk < 1024) ||
	    (!dev->param.isYaffs2 && dev->param.totalBytesPerChunk < 512) ||
	    (dev->param.inbandTags && !dev->param.isYaffs2) ||
	     dev->param.nChunksPerBlock < 2 ||
	     dev->param.nReservedBlocks < 2 ||
	     dev->internalStartBlock <= 0 ||
	     dev->internalEndBlock <= 0 ||
	     dev->internalEndBlock <= (dev->internalStartBlock + dev->param.nReservedBlocks + 2)) {	
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR
		   ("yaffs: NAND geometry problems: chunk size %d, type is yaffs%s, inbandTags %d "
		    TENDSTR), dev->param.totalBytesPerChunk, dev->param.isYaffs2 ? "2" : "", dev->param.inbandTags));
		return YAFFS_FAIL;
	}

	if (yaffs_InitialiseNAND(dev) != YAFFS_OK) {
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR("yaffs: InitialiseNAND failed" TENDSTR)));
		return YAFFS_FAIL;
	}

	/* Sort out space for inband tags, if required */
	if (dev->param.inbandTags)
		dev->nDataBytesPerChunk = dev->param.totalBytesPerChunk - sizeof(yaffs_PackedTags2TagsPart);
	else
		dev->nDataBytesPerChunk = dev->param.totalBytesPerChunk;

	/* Got the right mix of functions? */
	if (!yaffs_CheckDevFunctions(dev)) {
		/* Function missing */
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR
		   ("yaffs: device function(s) missing or wrong\n" TENDSTR)));

		return YAFFS_FAIL;
	}

	/* This is really a compilation check. */
	if (!yaffs_CheckStructures()) {
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR("yaffs_CheckStructures failed\n" TENDSTR)));
		return YAFFS_FAIL;
	}

	if (dev->isMounted) {
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR("yaffs: device already mounted\n" TENDSTR)));
		return YAFFS_FAIL;
	}

	/* Finished with most checks. One or two more checks happen later on too. */

	dev->isMounted = 1;

	/* OK now calculate a few things for the device */

	/*
	 *  Calculate all the chunk size manipulation numbers:
	 */
	x = dev->nDataBytesPerChunk;
	/* We always use dev->chunkShift and dev->chunkDiv */
	dev->chunkShift = Shifts(x);
	x >>= dev->chunkShift;
	dev->chunkDiv = x;
	/* We only use chunk mask if chunkDiv is 1 */
	dev->chunkMask = (1<<dev->chunkShift) - 1;

	/*
	 * Calculate chunkGroupBits.
	 * We need to find the next power of 2 > than internalEndBlock
	 */

	x = dev->param.nChunksPerBlock * (dev->internalEndBlock + 1);

	bits = ShiftsGE(x);

	/* Set up tnode width if wide tnodes are enabled. */
	if (!dev->param.wideTnodesDisabled) {
		/* bits must be even so that we end up with 32-bit words */
		if (bits & 1)
			bits++;
		if (bits < 16)
			dev->tnodeWidth = 16;
		else
			dev->tnodeWidth = bits;
	} else
		dev->tnodeWidth = 16;

	dev->tnodeMask = (1<<dev->tnodeWidth)-1;

	/* Level0 Tnodes are 16 bits or wider (if wide tnodes are enabled),
	 * so if the bitwidth of the
	 * chunk range we're using is greater than 16 we need
	 * to figure out chunk shift and chunkGroupSize
	 */

	if (bits <= dev->tnodeWidth)
		dev->chunkGroupBits = 0;
	else
		dev->chunkGroupBits = bits - dev->tnodeWidth;

	dev->tnodeSize = (dev->tnodeWidth * YAFFS_NTNODES_LEVEL0)/8;
	if(dev->tnodeSize < sizeof(yaffs_Tnode))
		dev->tnodeSize = sizeof(yaffs_Tnode);

	dev->chunkGroupSize = 1 << dev->chunkGroupBits;

	if (dev->param.nChunksPerBlock < dev->chunkGroupSize) {
		/* We have a problem because the soft delete won't work if
		 * the chunk group size > chunks per block.
		 * This can be remedied by using larger "virtual blocks".
		 */
		T(YAFFS_TRACE_ALWAYS,
		  (TSTR("yaffs: chunk group too large\n" TENDSTR)));

		return YAFFS_FAIL;
	}

	/* OK, we've finished verifying the device, lets continue with initialisation */

	/* More device initialisation */
	dev->allGCs = 0;
	dev->passiveGCs = 0;
	dev->oldestDirtyGCs = 0;
	dev->backgroundGCs = 0;
	dev->gcBlockFinder = 0;
	dev->bufferedBlock = -1;
	dev->doingBufferedBlockRewrite = 0;
	dev->nDeletedFiles = 0;
	dev->nBackgroundDeletions = 0;
	dev->nUnlinkedFiles = 0;
	dev->eccFixed = 0;
	dev->eccUnfixed = 0;
	dev->tagsEccFixed = 0;
	dev->tagsEccUnfixed = 0;
	dev->nErasureFailures = 0;
	dev->nErasedBlocks = 0;
	dev->gcDisable= 0;
	dev->hasPendingPrioritisedGCs = 1; /* Assume the worst for now, will get fixed on first GC */
	YINIT_LIST_HEAD(&dev->dirtyDirectories);
	dev->oldestDirtySequence = 0;
	dev->oldestDirtyBlock = 0;

	/* Initialise temporary buffers and caches. */
	if (!yaffs_InitialiseTempBuffers(dev))
		init_failed = 1;

	dev->srCache = NULL;
	dev->gcCleanupList = NULL;


	if (!init_failed &&
	    dev->param.nShortOpCaches > 0) {
		int i;
		void *buf;
		int srCacheBytes = dev->param.nShortOpCaches * sizeof(yaffs_ChunkCache);

		if (dev->param.nShortOpCaches > YAFFS_MAX_SHORT_OP_CACHES)
			dev->param.nShortOpCaches = YAFFS_MAX_SHORT_OP_CACHES;

		dev->srCache =  YMALLOC(srCacheBytes);

		buf = (__u8 *) dev->srCache;

		if (dev->srCache)
			memset(dev->srCache, 0, srCacheBytes);

		for (i = 0; i < dev->param.nShortOpCaches && buf; i++) {
			dev->srCache[i].object = NULL;
			dev->srCache[i].lastUse = 0;
			dev->srCache[i].dirty = 0;
			dev->srCache[i].data = buf = YMALLOC_DMA(dev->param.totalBytesPerChunk);
		}
		if (!buf)
			init_failed = 1;

		dev->srLastUse = 0;
	}

	dev->cacheHits = 0;

	if (!init_failed) {
		dev->gcCleanupList = YMALLOC(dev->param.nChunksPerBlock * sizeof(__u32));
		if (!dev->gcCleanupList)
			init_failed = 1;
	}

	if (dev->param.isYaffs2)
		dev->param.useHeaderFileSize = 1;

	if (!init_failed && !yaffs_InitialiseBlocks(dev))
		init_failed = 1;

	yaffs_InitialiseTnodesAndObjects(dev);

	if (!init_failed && !yaffs_CreateInitialDirectories(dev))
		init_failed = 1;


	if (!init_failed) {
		/* Now scan the flash. */
		if (dev->param.isYaffs2) {
			if (yaffs2_CheckpointRestore(dev)) {
				yaffs_CheckObjectDetailsLoaded(dev->rootDir);
				T(YAFFS_TRACE_ALWAYS,
				  (TSTR("yaffs: restored from checkpoint" TENDSTR)));
			} else {

				/* Clean up the mess caused by an aborted checkpoint load
				 * and scan backwards.
				 */
				yaffs_DeinitialiseBlocks(dev);

				yaffs_DeinitialiseTnodesAndObjects(dev);

				dev->nErasedBlocks = 0;
				dev->nFreeChunks = 0;
				dev->allocationBlock = -1;
				dev->allocationPage = -1;
				dev->nDeletedFiles = 0;
				dev->nUnlinkedFiles = 0;
				dev->nBackgroundDeletions = 0;

				if (!init_failed && !yaffs_InitialiseBlocks(dev))
					init_failed = 1;

				yaffs_InitialiseTnodesAndObjects(dev);

				if (!init_failed && !yaffs_CreateInitialDirectories(dev))
					init_failed = 1;

				if (!init_failed && !yaffs2_ScanBackwards(dev))
					init_failed = 1;
			}
		} else if (!yaffs1_Scan(dev))
				init_failed = 1;

		yaffs_StripDeletedObjects(dev);
		yaffs_FixHangingObjects(dev);
		if(dev->param.emptyLostAndFound)
			yaffs_EmptyLostAndFound(dev);
	}

	if (init_failed) {
		/* Clean up the mess */
		T(YAFFS_TRACE_TRACING,
		  (TSTR("yaffs: yaffs_GutsInitialise() aborted.\n" TENDSTR)));

		yaffs_Deinitialise(dev);
		return YAFFS_FAIL;
	}

	/* Zero out stats */
	dev->nPageReads = 0;
	dev->nPageWrites = 0;
	dev->nBlockErasures = 0;
	dev->nGCCopies = 0;
	dev->nRetriedWrites = 0;

	dev->nRetiredBlocks = 0;

	yaffs_VerifyFreeChunks(dev);
	yaffs_VerifyBlocks(dev);

	/* Clean up any aborted checkpoint data */
	if(!dev->isCheckpointed && dev->blocksInCheckpoint > 0)
		yaffs2_InvalidateCheckpoint(dev);

	T(YAFFS_TRACE_TRACING,
	  (TSTR("yaffs: yaffs_GutsInitialise() done.\n" TENDSTR)));
	return YAFFS_OK;

}

void yaffs_Deinitialise(yaffs_Device *dev)
{
	if (dev->isMounted) {
		int i;

		yaffs_DeinitialiseBlocks(dev);
		yaffs_DeinitialiseTnodesAndObjects(dev);
		if (dev->param.nShortOpCaches > 0 &&
		    dev->srCache) {

			for (i = 0; i < dev->param.nShortOpCaches; i++) {
				if (dev->srCache[i].data)
					YFREE(dev->srCache[i].data);
				dev->srCache[i].data = NULL;
			}

			YFREE(dev->srCache);
			dev->srCache = NULL;
		}

		YFREE(dev->gcCleanupList);

		for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++)
			YFREE(dev->tempBuffer[i].buffer);

		dev->isMounted = 0;

		if (dev->param.deinitialiseNAND)
			dev->param.deinitialiseNAND(dev);
	}
}

int yaffs_CountFreeChunks(yaffs_Device *dev)
{
	int nFree=0;
	int b;

	yaffs_BlockInfo *blk;

	blk = dev->blockInfo;
	for (b = dev->internalStartBlock; b <= dev->internalEndBlock; b++) {
		switch (blk->blockState) {
		case YAFFS_BLOCK_STATE_EMPTY:
		case YAFFS_BLOCK_STATE_ALLOCATING:
		case YAFFS_BLOCK_STATE_COLLECTING:
		case YAFFS_BLOCK_STATE_FULL:
			nFree +=
			    (dev->param.nChunksPerBlock - blk->pagesInUse +
			     blk->softDeletions);
			break;
		default:
			break;
		}
		blk++;
	}

	return nFree;
}

int yaffs_GetNumberOfFreeChunks(yaffs_Device *dev)
{
	/* This is what we report to the outside world */

	int nFree;
	int nDirtyCacheChunks;
	int blocksForCheckpoint;
	int i;

#if 1
	nFree = dev->nFreeChunks;
#else
	nFree = yaffs_CountFreeChunks(dev);
#endif

	nFree += dev->nDeletedFiles;

	/* Now count the number of dirty chunks in the cache and subtract those */

	for (nDirtyCacheChunks = 0, i = 0; i < dev->param.nShortOpCaches; i++) {
		if (dev->srCache[i].dirty)
			nDirtyCacheChunks++;
	}

	nFree -= nDirtyCacheChunks;

	nFree -= ((dev->param.nReservedBlocks + 1) * dev->param.nChunksPerBlock);

	/* Now we figure out how much to reserve for the checkpoint and report that... */
	blocksForCheckpoint = yaffs2_CalcCheckpointBlocksRequired(dev);

	nFree -= (blocksForCheckpoint * dev->param.nChunksPerBlock);

	if (nFree < 0)
		nFree = 0;

	return nFree;

}


/*---------------------------------------- YAFFS test code ----------------------*/

#define yaffs_CheckStruct(structure, syze, name) \
	do { \
		if (sizeof(structure) != syze) { \
			T(YAFFS_TRACE_ALWAYS, (TSTR("%s should be %d but is %d\n" TENDSTR),\
				name, syze, (int) sizeof(structure))); \
			return YAFFS_FAIL; \
		} \
	} while (0)

static int yaffs_CheckStructures(void)
{
/*      yaffs_CheckStruct(yaffs_Tags,8,"yaffs_Tags"); */
/*      yaffs_CheckStruct(yaffs_TagsUnion,8,"yaffs_TagsUnion"); */
/*      yaffs_CheckStruct(yaffs_Spare,16,"yaffs_Spare"); */


#ifndef CONFIG_YAFFS_WINCE
	yaffs_CheckStruct(yaffs_ObjectHeader, 512, "yaffs_ObjectHeader");
#endif
	return YAFFS_OK;
}
