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
/*
 * @OSF_FREE_COPYRIGHT@
 */

#include "config.h"
#include "device_tree.h"

#ifndef NULL
#define       NULL    ((void *) 0)
#endif

#define round_long(x)	(((x) + 3) & -4)
#define next_prop(x)	((DeviceTreeNodeProperty *) (((int)x) + sizeof(DeviceTreeNodeProperty) + round_long(x->length)))

/* Entry*/
typedef DeviceTreeNode *RealDTEntry;

static int DTInitialized;
static RealDTEntry DTRootNode;
static unsigned long DTSize;

/*
 * Support Routines
 */
static RealDTEntry
skipProperties(RealDTEntry entry)
{
	DeviceTreeNodeProperty *prop;
	int k;

	if (entry == NULL || entry->nProperties == 0) {
		return NULL;
	} else {
		prop = (DeviceTreeNodeProperty *) (entry + 1);
		for (k = 0; k < entry->nProperties; k++) {
			prop = next_prop(prop);
		}
	}
	return ((RealDTEntry) prop);
}

static RealDTEntry
skipTree(RealDTEntry root)
{
	RealDTEntry entry;
	int k;

	entry = skipProperties(root);
	if (entry == NULL) {
		return NULL;
	}
	for (k = 0; k < root->nChildren; k++) {
		entry = skipTree(entry);
	}
	return entry;
}

static RealDTEntry
GetFirstChild(RealDTEntry parent)
{
	return skipProperties(parent);
}

static RealDTEntry
GetNextChild(RealDTEntry sibling)
{
	return skipTree(sibling);
}

static const char *
GetNextComponent(const char *cp, char *bp)
{
	while (*cp != 0) {
		if (*cp == kDTPathNameSeparator) {
			cp++;
			break;
		}
		*bp++ = *cp++;
	}
	*bp = 0;
	return cp;
}

static RealDTEntry
FindChild(RealDTEntry cur, char *buf)
{
	RealDTEntry	child;
	unsigned long	index;
	char *		str;
	int		dummy;

	if (cur->nChildren == 0) {
		return NULL;
	}
	index = 1;
	child = GetFirstChild(cur);
	while (1) {
		if (DTGetProperty(child, "name", (void **)&str, &dummy) != kSuccess) {
			break;
		}
		if (strcmp(str, buf) == 0) {
			return child;
		}
		if (index >= cur->nChildren) {
			break;
		}
		child = GetNextChild(child);
		index++;
	}
	return NULL;
}


/*
 * External Routines
 */
void
DTInit(void *base, unsigned long size)
{
	DTRootNode = (RealDTEntry) base;
    DTSize = size;
	DTInitialized = (DTRootNode != 0);
}

int
DTEntryIsEqual(const DTEntry ref1, const DTEntry ref2)
{
	/* equality of pointers */
	return (ref1 == ref2);
}

static char *startingP;		// needed for find_entry
int find_entry(const char *propName, const char *propValue, DTEntry *entryH);

int DTFindEntry(const char *propName, const char *propValue, DTEntry *entryH)
{
	if (!DTInitialized) {
		return kError;
	}

	startingP = (char *)DTRootNode;
	return(find_entry(propName, propValue, entryH));
}

int find_entry(const char *propName, const char *propValue, DTEntry *entryH)
{
	DeviceTreeNode *nodeP = (DeviceTreeNode *) startingP;
	int k;

	if (nodeP->nProperties == 0) return(kError);	// End of the list of nodes
	startingP = (char *) (nodeP + 1);

	// Search current entry
	for (k = 0; k < nodeP->nProperties; ++k) {
		DeviceTreeNodeProperty *propP = (DeviceTreeNodeProperty *) startingP;

		startingP += sizeof (*propP) + ((propP->length + 3) & -4);

		if (strcmp (propP->name, propName) == 0) {
			if (strcmp( (char *)(propP + 1), propValue) == 0)
			{
				*entryH = (DTEntry)nodeP;
				return(kSuccess);
			}
		}
	}

	// Search child nodes
	for (k = 0; k < nodeP->nChildren; ++k)
	{
		if (find_entry(propName, propValue, entryH) == kSuccess)
			return(kSuccess);
	}
	return(kError);
}

int
DTLookupEntry(const DTEntry searchPoint, const char *pathName, DTEntry *foundEntry)
{
	DTEntryNameBuf	buf;
	RealDTEntry	cur;
	const char *	cp;

	if (!DTInitialized) {
		return kError;
	}
	if (searchPoint == NULL) {
		cur = DTRootNode;
	} else {
		cur = searchPoint;
	}
	cp = pathName;
	if (*cp == kDTPathNameSeparator) {
		cp++;
		if (*cp == 0) {
			*foundEntry = cur;
			return kSuccess;
		}
	}
	do {
		cp = GetNextComponent(cp, buf);

		/* Check for done */
		if (*buf == 0) {
			if (*cp == 0) {
				*foundEntry = cur;
				return kSuccess;
			}
			break;
		}

		cur = FindChild(cur, buf);

	} while (cur != NULL);

	return kError;
}

int
DTGetProperty(const DTEntry entry, const char *propertyName, void **propertyValue, int *propertySize)
{
	DeviceTreeNodeProperty *prop;
	int k;

	if (entry == NULL || entry->nProperties == 0) {
		return kError;
	} else {
		prop = (DeviceTreeNodeProperty *) (entry + 1);
		for (k = 0; k < entry->nProperties; k++) {
			if (strcmp(prop->name, propertyName) == 0) {
				*propertyValue = (void *) (((int)prop)
						+ sizeof(DeviceTreeNodeProperty));
				*propertySize = prop->length;
				return kSuccess;
			}
			prop = next_prop(prop);
		}
	}
	return kError;
}

int
DTSetProperty(const DTEntry entry, const char *propertyName, void *propertyValue)
{
    DeviceTreeNodeProperty *prop;
	int k;

	if (entry == NULL || entry->nProperties == 0) {
		return kError;
	} else {
		prop = (DeviceTreeNodeProperty *) (entry + 1);
		for (k = 0; k < entry->nProperties; k++) {
			if (strcmp(prop->name, propertyName) == 0) {
                memcpy((void *) (((int)prop) + sizeof(DeviceTreeNodeProperty)), propertyValue, prop->length);
				return kSuccess;
			}
			prop = next_prop(prop);
		}
	}
	return kError;
}

int
DTAddProperty(const DTEntry entry, const char *propertyName, void *propertyValue, int propertySize, void **deviceTree, unsigned long *deviceTreeSize)
{
    char *newDeviceTree;
    unsigned long newDeviceTreeSize;
    DTEntry newDTEntry;
    unsigned long nodeOffset;
    unsigned long propOffset;
	unsigned long newPropOffset;
    DeviceTreeNodeProperty *propLast;
	DeviceTreeNodeProperty *propNew;
	int k;

    //
    // Calculate new flattened device tree size with new property.
    // Existing device tree will be copied into this new buffer, then the property, and finally the rest of the device tree.
    // Property will be added after any existing properties.
    //
    newDeviceTreeSize = DTSize + sizeof (DeviceTreeNodeProperty) + ((propertySize + 3) & -4);
    newDeviceTree = malloc(newDeviceTreeSize);
    if (newDeviceTree == NULL) {
        return kError;
    }

	propLast = (DeviceTreeNodeProperty *) (entry + 1);
	for (k = 0; k < entry->nProperties; k++) {
		propLast = next_prop(propLast);
	}

	nodeOffset = (unsigned long)entry - (unsigned long)DTRootNode;
    propOffset = (unsigned long)propLast - (unsigned long)DTRootNode;
    memcpy(newDeviceTree, DTRootNode, propOffset);
    newDTEntry = (DTEntry)&newDeviceTree[nodeOffset];
    newDTEntry->nProperties++;

    propNew = (DeviceTreeNodeProperty*) &newDeviceTree[propOffset];
    strncpy(propNew->name, propertyName, sizeof (propNew->name));
    propNew->length = propertySize;
    memcpy(propNew + 1, propertyValue, propertySize);
    newPropOffset = propOffset + sizeof (DeviceTreeNodeProperty) + ((propertySize + 3) & -4);
    memcpy(&newDeviceTree[newPropOffset], propLast, DTSize - propOffset);

    *deviceTree = newDeviceTree;
    *deviceTreeSize = newDeviceTreeSize;
    return kSuccess;
}
