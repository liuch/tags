/*
 * item.h
 * Copyright (C) 2013  Aleksey Andreev
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef ITEM_H
#define ITEM_H

#include <stddef.h>
#include <wchar.h>

#include "property.h"
#include "common.h"

struct ItemStruct
{
	size_t                fileSize;
	wchar_t               hash[FILE_HASH_LEN + 1];
	unsigned int          fileNameMax;
	unsigned int          fileNameCount;
	wchar_t               **fileNames;
	unsigned int          propsMax;
	unsigned int          propsCount;
	struct PropertyStruct **props;
};

struct ItemStruct *itemInit(size_t fileSize, const wchar_t *fileHash);
struct ItemStruct *itemInitFromRawData(size_t fSize, const wchar_t *fHash, const wchar_t *fName, const wchar_t *addPropStr, const wchar_t *setPropStr);
void itemFree(struct ItemStruct *item);
int itemIsFileName(struct ItemStruct *item, const wchar_t *fileName);
int itemAddFileName(struct ItemStruct *item, const wchar_t *fileName);
void itemRemoveFileName(struct ItemStruct *item, const wchar_t *fileName);
wchar_t *itemGetFileName(const struct ItemStruct *item, unsigned int pos);
void itemClearFileNames(struct ItemStruct *item);
int itemIsEqual(const struct ItemStruct *item1, const struct ItemStruct *item2);
int itemMerge(struct ItemStruct *itemTo, struct ItemStruct *itemFrom);

int itemSetProperty(struct ItemStruct *item, const wchar_t *name, const wchar_t *value);
int itemAddPropertiesRaw(struct ItemStruct *item, const wchar_t *rawVal);
int itemSetPropertiesRaw(struct ItemStruct *item, const wchar_t *rawVal);
int itemDelPropertiesRaw(struct ItemStruct *item, const wchar_t *rawVal);
const wchar_t *itemPropertyGetName(const struct ItemStruct *item, unsigned int propNum);
int itemPropertyValueToString(const struct ItemStruct *item, unsigned int propNum, wchar_t *strBuf, int bufLen);
struct PropertyStruct **itemGetPropArrayAddrByNum(const struct ItemStruct *item, unsigned int num);
struct PropertyStruct **itemGetPropertyPosByName(const struct ItemStruct *item, const wchar_t *propName);

#endif // ITEM_H
