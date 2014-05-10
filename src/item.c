/*
 * item.c
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

#include <stdlib.h>
#include <string.h>

#include "item.h"

#define NAMES_INCREASE    10
#define PROPS_INCREASE    10
#define PROPS_SEPARATOR   L'@'

int itemSetProperty_(struct ItemStruct *item, struct PropertyStruct *prop);
int itemInsertProperty(struct ItemStruct *item, struct PropertyStruct *prop);
int itemAddFileName_(struct ItemStruct *item, wchar_t *allocName);
wchar_t **itemGetFileNameArrayAddrByNum(const struct ItemStruct *item, unsigned int pos);
wchar_t **itemGetFileNameArrayAddrByName(const struct ItemStruct *item, const wchar_t *fileName);

struct ItemStruct* itemInit(size_t fileSize, const wchar_t *fileHash)
{
	struct ItemStruct *item = malloc(sizeof(struct ItemStruct));
	if (item != NULL)
	{
		item->fileNameMax   = 0;
		item->fileNameCount = 0;
		item->fileNames     = NULL;
		item->fileSize      = fileSize;
		item->propsMax      = 0;
		item->propsCount    = 0;
		item->props         = NULL;
		wcsncpy(item->hash, fileHash, FILE_HASH_LEN);
		item->hash[FILE_HASH_LEN] = L'\0';
	}
	return item;
}

struct ItemStruct *itemInitFromRawData(size_t fSize, const wchar_t *fHash, const wchar_t *fName, const wchar_t *addPropStr, const wchar_t *setPropStr)
{
	struct ItemStruct *item = itemInit(fSize, fHash);
	if (item != NULL)
	{
		int res;
		if (fName != NULL)
			res = itemAddFileName(item, fName);

		if (res == EXIT_SUCCESS && setPropStr != NULL)
			res = itemSetPropertiesRaw(item, setPropStr);

		if (res == EXIT_SUCCESS && addPropStr != NULL)
			res = itemAddPropertiesRaw(item, addPropStr);

		if (res != EXIT_SUCCESS)
		{
			itemFree(item);
			item = NULL;
		}
	}
	return item;
}

void itemFree(struct ItemStruct *item)
{
	itemClearFileNames(item);

	if (item->props != NULL)
	{
		unsigned int cnt = item->propsCount;
		struct PropertyStruct **pProp = item->props;
		for ( ; cnt != 0; ++pProp)
		{
			struct PropertyStruct *prop = *pProp;
			if (prop != NULL)
			{
				propFree(prop);
				--cnt;
			}
		}
		free(item->props);
	}
	free(item);
}

int itemIsFileName(struct ItemStruct *item, const wchar_t *fileName)
{
	if (itemGetFileNameArrayAddrByName(item, fileName) != NULL)
		return 1;
	return 0;
}

int itemAddFileName(struct ItemStruct *item, const wchar_t *fileName)
{
	if (itemIsFileName(item, fileName) != 0)
		return EXIT_SUCCESS;

	wchar_t *pName = malloc((wcslen(fileName) + 1) * sizeof(wchar_t));
	if (pName == NULL)
		return EXIT_FAILURE;
	wcscpy(pName, fileName);

	if (itemAddFileName_(item, pName) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	free(pName);
	return EXIT_FAILURE;
}

void itemRemoveFileName(struct ItemStruct *item, const wchar_t *fileName)
{
	wchar_t **pName = itemGetFileNameArrayAddrByName(item, fileName);
	if (pName != NULL)
	{
		--item->fileNameCount;
		free(*pName);
		*pName = NULL;
		if (item->fileNameCount == 0)
		{
			item->fileNameMax = 0;
			free(item->fileNames);
			item->fileNames = NULL;
		}
	}
}

wchar_t *itemGetFileName(const struct ItemStruct *item, unsigned int pos)
{
	wchar_t **pName = itemGetFileNameArrayAddrByNum(item, pos);
	if (pName == NULL)
		return NULL;
	return *pName;
}

void itemClearFileNames(struct ItemStruct *item)
{
	if (item->fileNames != NULL)
	{
		unsigned int cnt = item->fileNameCount;
		wchar_t **names  = item->fileNames;
		for ( ; cnt != 0; ++names)
		{
			wchar_t *nm = *names;
			if (nm != NULL)
			{
				*names = NULL;
				free(nm);
				--cnt;
			}
		}
		free(item->fileNames);
		item->fileNames = NULL;
		item->fileNameCount = 0;
		item->fileNameMax = 0;
	}
}

int itemIsEqual(const struct ItemStruct *item1, const struct ItemStruct *item2)
{
	if (item1->fileSize != item2->fileSize || item1->propsCount != item2->propsCount || wcscmp(item1->hash, item2->hash) != 0)
		return 0;

	const unsigned int propCnt = item1->propsCount;
	unsigned int i = 0;
	for ( ; i < propCnt; ++i)
	{
		struct PropertyStruct *prop1 = *itemGetPropArrayAddrByNum(item1, i);
		struct PropertyStruct **pProp2 = itemGetPropertyPosByName(item2, propGetName(prop1));
		if (pProp2 == NULL)
			return 0;
		if (!propIsEqualValue(prop1, *pProp2))
			return 0;
	}
	return 1;
}

int itemMerge(struct ItemStruct *itemTo, struct ItemStruct *itemFrom)
{
	while (itemFrom->fileNameCount != 0)
	{
		wchar_t **pName = itemGetFileNameArrayAddrByNum(itemFrom, 0);
		if (!itemIsFileName(itemTo, *pName))
		{
			if (itemAddFileName_(itemTo, *pName) != EXIT_SUCCESS)
				return EXIT_FAILURE;
		}
		else
			free(*pName);

		--itemFrom->fileNameCount;
		*pName = NULL;
	}

	while (itemFrom->propsCount != 0)
	{
		struct PropertyStruct **ppF = itemGetPropArrayAddrByNum(itemFrom, 0);
		struct PropertyStruct **ppT = itemGetPropertyPosByName(itemTo, propGetName(*ppF));
		if (ppT == NULL)
		{
			if (itemInsertProperty(itemTo, *ppF) != EXIT_SUCCESS)
				return EXIT_FAILURE;
		}
		else
		{
			struct PropertyStruct *propF = *ppF;
			struct PropertyStruct *propT = *ppT;
			++propT->userData;
			const wchar_t *subvalStr;
			unsigned int i = 0;
			while ((subvalStr = propGetSubval(propF, i++, None)) != NULL)
			{
				struct SubvalHandle *subvalT = propIsSubval(propT, subvalStr);
				if (subvalT != NULL)
					++subvalT->userData;
				else
				{
					propT = propAddSubval(propT, subvalStr);
					if (propT == NULL)
						return EXIT_FAILURE;
					*ppT = propT;
				}
			}
			free(*ppF);
		}
		*ppF = NULL;
		--itemFrom->propsCount;
	}

	itemFree(itemFrom);
	return EXIT_SUCCESS;
}

int itemSetProperty(struct ItemStruct *item, const wchar_t *name, const wchar_t *value)
{
	struct PropertyStruct *newProp = propInit(name, value);
	if (newProp == NULL)
		return EXIT_FAILURE;

	if (itemSetProperty_(item, newProp) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	propFree(newProp);
	return EXIT_FAILURE;
}

int itemAddPropertiesRaw(struct ItemStruct *item, const wchar_t *rawVal)
{
	const wchar_t *startProp = rawVal;
	do
	{
		const wchar_t *endProp = wcschr(startProp, PROPS_SEPARATOR);
		const wchar_t *valPos  = wcschr(startProp, L'=');
		if (valPos == NULL || (endProp != NULL && valPos > endProp))
			return EXIT_FAILURE;

		wchar_t name[2048];
		unsigned int len = valPos - startProp;
		if (len == 0 || len >= sizeof(name) / sizeof(wchar_t))
			return EXIT_FAILURE;

		wcsncpy(name, startProp, len);
		name[len] = L'\0';
		++valPos;
		if (endProp != NULL)
		{
			wchar_t val[2048];
			len = endProp - valPos;
			if (len >= sizeof(val) / sizeof(wchar_t))
				return EXIT_FAILURE;
			if (len > 0)
				wcsncpy(val, valPos, len);
			val[len] = L'\0';
			valPos = val;
		}

		struct PropertyStruct **pp = itemGetPropertyPosByName(item, name);
		if (pp == NULL)
		{
			struct PropertyStruct *prop = propInit(name, valPos);
			if (prop == NULL)
				return EXIT_FAILURE;
			if (itemInsertProperty(item, prop) != EXIT_SUCCESS)
			{
				propFree(prop);
				return EXIT_FAILURE;
			}
		}
		else
			if (propAddSubvalues(pp, valPos) != EXIT_SUCCESS)
				return EXIT_FAILURE;

		if (endProp == NULL)
			return EXIT_SUCCESS;
		startProp = endProp + 1;
	} while (*startProp != L'\0');

	return EXIT_FAILURE;
}

int itemSetPropertiesRaw(struct ItemStruct *item, const wchar_t *rawVal)
{
	const wchar_t *startProp = rawVal;
	do
	{
		wchar_t *endProp = wcschr(startProp, PROPS_SEPARATOR);
		wchar_t *valPos  = wcschr(startProp, L'=');
		if (valPos == NULL || (endProp != NULL && valPos > endProp))
			return EXIT_FAILURE;

		wchar_t name[2048];
		unsigned int len = valPos - startProp;
		if (len == 0 || len >= sizeof(name) / sizeof(wchar_t))
			return EXIT_FAILURE;

		wcsncpy(name, startProp, len);
		name[len] = L'\0';
		++valPos;
		if (endProp == NULL)
			return itemSetProperty(item, name, valPos);

		wchar_t val[2048];
		len = endProp - valPos;
		if (len >= sizeof(val) / sizeof(wchar_t))
			return EXIT_FAILURE;

		if (len > 0)
			wcsncpy(val, valPos, len);
		val[len] = L'\0';
		if (itemSetProperty(item, name, val) != EXIT_SUCCESS)
			return EXIT_FAILURE;

		startProp = endProp + 1;
	} while (*startProp != L'\0');

	return EXIT_SUCCESS;
}

int itemDelPropertiesRaw(struct ItemStruct *item, const wchar_t *rawVal)
{
	const wchar_t *startProp = rawVal;
	do
	{
		const wchar_t *endProp = wcschr(startProp, PROPS_SEPARATOR);
		const wchar_t *valPos  = wcschr(startProp, L'=');
		if (valPos == NULL || (endProp != NULL && valPos > endProp))
		{
			struct PropertyStruct **pp;
			if (valPos == NULL && endProp == NULL)
				pp = itemGetPropertyPosByName(item, startProp);
			else
			{
				wchar_t name[2048];
				unsigned int nameLen = endProp - startProp;
				if (nameLen == 0 || nameLen >= sizeof(name) / sizeof(wchar_t))
					return EXIT_FAILURE;
				wcsncpy(name, startProp, nameLen);
				name[nameLen] = L'\0';
				pp = itemGetPropertyPosByName(item, name);
			}
			if (pp != NULL)
			{
				propFree(*pp);
				*pp = NULL;
				--item->propsCount;
			}
		}

		else
		{
			wchar_t name[2048];
			unsigned int len = valPos - startProp;
			if (len == 0 || len >= sizeof(name) / sizeof(wchar_t))
				return EXIT_FAILURE;

			wcsncpy(name, startProp, len);
			name[len] = L'\0';
			struct PropertyStruct **pp = itemGetPropertyPosByName(item, name);
			if (pp != NULL)
			{
				++valPos;
				if (endProp == NULL)
					return propDelSubvalues(pp, valPos);

				wchar_t val[2048];
				len = endProp - valPos;
				if (len == 0 || len >= sizeof(val) / sizeof(wchar_t))
					return EXIT_FAILURE;
				wcsncpy(val, valPos, len);
				val[len] = L'\0';
				if (propDelSubvalues(pp, val) != EXIT_SUCCESS)
					return EXIT_FAILURE;
			}
		}
		if (endProp == NULL)
			break;
		startProp = endProp + 1;
	} while (*startProp != L'\0');

	return EXIT_SUCCESS;
}

const wchar_t *itemPropertyGetName(const struct ItemStruct *item, unsigned int propNum)
{
	struct PropertyStruct **ptr = itemGetPropArrayAddrByNum(item, propNum);
	if (ptr != NULL)
	{
		return propGetName(*ptr);
	}
	return NULL;
}

int itemPropertyValueToString(const struct ItemStruct *item, unsigned int propNum, wchar_t *strBuf, int bufLen)
{
	struct PropertyStruct **ptr = itemGetPropArrayAddrByNum(item, propNum);
	if (ptr == NULL)
		return EXIT_FAILURE;

	struct PropertyStruct *prop = *ptr;
	unsigned int cnt = prop->valCount;
	int buffRem = bufLen - 1;
	wchar_t *buffPos = strBuf;
	unsigned int subvalNum = 0;
	while (cnt != 0 && buffRem != 0)
	{
		if (buffPos != strBuf)
		{
			--buffPos;
			if (buffRem <= 1)
				break;
			*buffPos++ = L',';
			--buffRem;
		}
		const wchar_t *subvalPos = propGetSubval(prop, subvalNum++, ByValue);
		if (subvalPos == NULL)
			return EXIT_FAILURE;
		wchar_t ch;
		do
		{
			ch = *subvalPos++;
			*buffPos = ch;
			if (--buffRem == 0)
				break;
			++buffPos;
		} while (ch != L'\0');
		--cnt;
	}
	*buffPos = L'\0';
	return EXIT_SUCCESS;
}

struct PropertyStruct **itemGetPropArrayAddrByNum(const struct ItemStruct* item, unsigned int num)
{
	if (num < item->propsCount)
	{
		struct PropertyStruct **ptr = item->props;
		unsigned int n = num + 1;
		for ( ; ; ++ptr)
		{
			if (*ptr != NULL)
			{
				if (--n == 0)
					return ptr;
			}
		}
	}
	return NULL;
}

struct PropertyStruct **itemGetPropertyPosByName(const struct ItemStruct *item, const wchar_t *propName)
{
	unsigned int cnt = item->propsCount;
	if (cnt > 0)
	{
		struct PropertyStruct **ptr = item->props;
		do
		{
			const struct PropertyStruct *prop = *ptr;
			if (prop != NULL)
			{
				if (wcscasecmp(propName, propGetName(prop)) == 0)
					return ptr;
				--cnt;
			}
			++ptr;
		} while (cnt != 0);
	}
	return NULL;
}

/**************************** Private ********************************/

int itemSetProperty_(struct ItemStruct *item, struct PropertyStruct *prop)
{
	struct PropertyStruct **propPtr = itemGetPropertyPosByName(item, propGetName(prop));
	if (propPtr != NULL)
	{
		struct PropertyStruct *oldProp = *propPtr;
		*propPtr = prop;
		propFree(oldProp);
		return EXIT_SUCCESS;
	}
	return itemInsertProperty(item, prop);
}

int itemInsertProperty(struct ItemStruct *item, struct PropertyStruct *prop)
{
	unsigned int max = item->propsMax;
	unsigned int cnt = item->propsCount;
	if (max == cnt)
	{
		struct PropertyStruct **newPtr;
		if (max == 0)
			newPtr = malloc(sizeof(struct PropertyStruct *) * PROPS_INCREASE);
		else
			newPtr = realloc(item->props, sizeof(struct PropertyStruct *) * (max + PROPS_INCREASE));
		if (newPtr == NULL)
			return EXIT_FAILURE;

		bzero(&newPtr[max], sizeof(struct PropertyStruct *) * PROPS_INCREASE);
		item->props      = newPtr;
		item->props[max] = prop;
		item->propsMax   += PROPS_INCREASE;
		++item->propsCount;
		return EXIT_SUCCESS;
	}

	struct PropertyStruct **ptr = item->props;
	for ( ; ; ++ptr)
		if (*ptr == NULL)
			break;
	*ptr = prop;
	++item->propsCount;

	return EXIT_SUCCESS;
}

int itemAddFileName_(struct ItemStruct *item, wchar_t *allocName)
{
	unsigned int max = item->fileNameMax;
	unsigned int cnt = item->fileNameCount;
	if (max == cnt)
	{
		wchar_t **newPtr;
		if (max == 0)
			newPtr = malloc(sizeof(wchar_t *) * NAMES_INCREASE);
		else
			newPtr = realloc(item->fileNames, sizeof(wchar_t *) * (max + NAMES_INCREASE));
		if (newPtr == NULL)
			return EXIT_FAILURE;

		bzero(&newPtr[max], sizeof(wchar_t *) * NAMES_INCREASE);
		item->fileNames      = newPtr;
		item->fileNames[max] = allocName;
		item->fileNameMax   += NAMES_INCREASE;
		++item->fileNameCount;
		return EXIT_SUCCESS;
	}

	wchar_t **ptr = item->fileNames;
	for ( ; ; ++ptr)
		if (*ptr == NULL)
			break;
	*ptr = allocName;
	++item->fileNameCount;

	return EXIT_SUCCESS;
}

wchar_t **itemGetFileNameArrayAddrByNum(const struct ItemStruct *item, unsigned int pos)
{
	if (pos < item->fileNameCount)
	{
		wchar_t **pName = item->fileNames;
		unsigned int i;
		for (i = 0; ; ++pName)
		{
			if (*pName != NULL)
				if (i++ == pos)
					return pName;
		}
	}
	return NULL;
}

wchar_t **itemGetFileNameArrayAddrByName(const struct ItemStruct *item, const wchar_t *fileName)
{
	wchar_t **pName = item->fileNames;
	unsigned int cnt = item->fileNameCount;
	for ( ; cnt != 0; ++pName)
		if (*pName != NULL)
		{
			if (wcscmp(*pName, fileName) == 0)
				return pName;
			--cnt;
		}
	return NULL;
}
