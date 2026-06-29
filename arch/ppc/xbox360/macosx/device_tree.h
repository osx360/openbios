/*
 * Copyright (c) 2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#ifndef _PEXPERT_DEVICE_TREE_H_
#define _PEXPERT_DEVICE_TREE_H_

/*
-------------------------------------------------------------------------------
 Foundation Types
-------------------------------------------------------------------------------
*/
enum {
	kDTPathNameSeparator	= '/'				/* 0x2F */
};


/* Property Name Definitions (Property Names are C-Strings)*/
enum {
	kDTMaxPropertyNameLength=31	/* Max length of Property Name (terminator not included) */
};

typedef char DTPropertyNameBuf[32];


/* Entry Name Definitions (Entry Names are C-Strings)*/
enum {
	kDTMaxEntryNameLength		= 31	/* Max length of a C-String Entry Name (terminator not included) */
};

/* length of DTEntryNameBuf = kDTMaxEntryNameLength +1*/
typedef char DTEntryNameBuf[32];


/* Entry*/
typedef struct OpaqueDTEntry* DTEntry;


/* status values*/
enum {
		kError = -1,
		kIterationDone = 0,
		kSuccess = 1
};

/*

Structures for a Flattened Device Tree
 */

#define kPropNameLength	32

typedef struct DeviceTreeNodeProperty {
    char		name[kPropNameLength];	// NUL terminated property name
    unsigned long	length;		// Length (bytes) of folloing prop value
//  unsigned long	value[1];	// Variable length value of property
					// Padded to a multiple of a longword?
} DeviceTreeNodeProperty;

typedef struct OpaqueDTEntry {
    unsigned long	nProperties;	// Number of props[] elements (0 => end)
    unsigned long	nChildren;	// Number of children[] elements
//  DeviceTreeNodeProperty	props[];// array size == nProperties
//  DeviceTreeNode	children[];	// array size == nChildren
} DeviceTreeNode;

/*
-------------------------------------------------------------------------------
 Device Tree Calls
-------------------------------------------------------------------------------
*/

/* Used to initalize the device tree functions. */
/* base is the base address of the flatened device tree */
void DTInit(void *base, unsigned long size);

/*
-------------------------------------------------------------------------------
 Entry Handling
-------------------------------------------------------------------------------
*/
/* Compare two Entry's for equality. */
extern int DTEntryIsEqual(const DTEntry ref1, const DTEntry ref2);

/*
-------------------------------------------------------------------------------
 LookUp Entry by Name
-------------------------------------------------------------------------------
*/
/*
 DTFindEntry:
 Find the device tree entry that contains propName=propValue.
 It currently  searches the entire
 tree.  This function should eventually go in DeviceTree.c.
 Returns:    kSuccess = entry was found.  Entry is in entryH.
             kError   = entry was not found
*/
extern int DTFindEntry(const char *propName, const char *propValue, DTEntry *entryH);

/*
 Lookup Entry
 Locates an entry given a specified subroot (searchPoint) and path name.  If the
 searchPoint pointer is NULL, the path name is assumed to be an absolute path
 name rooted to the root of the device tree.
*/
extern int DTLookupEntry(const DTEntry searchPoint, const char *pathName, DTEntry *foundEntry);

/*
-------------------------------------------------------------------------------
 Get Property Values
-------------------------------------------------------------------------------
*/
/*
 Get the value of the specified property for the specified entry.  

 Get Property
*/
extern int DTGetProperty(const DTEntry entry, const char *propertyName, void **propertyValue, int *propertySize);

extern int DTSetProperty(const DTEntry entry, const char *propertyName, void *propertyValue);

extern int DTAddProperty(const DTEntry entry, const char *propertyName, void *propertyValue, int propertySize, void **deviceTree, unsigned long *deviceTreeSize);

#endif
