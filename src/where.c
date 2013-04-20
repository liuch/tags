/*
 * where.c
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

#include "where.h"

#define CONDITION_INCREASE   10
#define CONDITIONS_SEPARATOR '@'

int whereSetConditionsRaw(struct WhereStruct *whr, const char *whereStr);
int whereSetConditions(struct WhereStruct *whr, const char *name, const char *value, unsigned int data);
int whereInsertConditions(struct WhereStruct *whr, struct PropertyStruct *prop);

struct WhereStruct *whereInit(const char *whereStr)
{
	struct WhereStruct *whr = malloc(sizeof(struct WhereStruct));
	if (whr != NULL)
	{
		bzero(whr, sizeof(struct WhereStruct));
		if (whereSetConditionsRaw(whr, whereStr) != EXIT_SUCCESS)
		{
			whereFree(whr);
			whr = NULL;
		}
	}
	return whr;
}

void whereFree(struct WhereStruct *whr)
{
	struct PropertyStruct **pCond = whr->conditions;
	if (pCond != NULL)
	{
		unsigned int cnt = whr->condCount;
		for ( ; cnt != 0; --cnt)
			propFree(*pCond++);
		free(whr->conditions);
	}
	free(whr);
}

int whereIsFiltered(const struct WhereStruct *whr, struct ItemStruct *item)
{
	unsigned int whrIdx;
	for (whrIdx = 0; whrIdx < whr->condCount; ++whrIdx)
	{
		struct PropertyStruct *cond = whr->conditions[whrIdx];
		struct PropertyStruct **pItemProp = itemGetPropertyPosByName(item, propGetName(cond));
		if (pItemProp == NULL)
		{
			if (!propIsEmpty(cond) || cond->userData)
				return 1;
		}
		else
		{
			struct PropertyStruct *itemProp = *pItemProp;
			if (propIsEmpty(cond))
			{
				if (propIsEmpty(itemProp) || cond->userData)
					continue;
				return 1;
			}
			struct SubvalHandle **condArray = propGetValueIndex(cond, None);
			unsigned int cnt = cond->valCount;
			unsigned int i;
			for (i = 0; ; ++i)
			{
				if (i == cnt)
					return 1;
				struct SubvalHandle *sub = *condArray++;
				if (propIsSubval(itemProp, subvalString(sub)))
					break;
			}
		}
	}
	return 0;
}

// ********************* Private ***************************

int whereSetConditionsRaw(struct WhereStruct *whr, const char *whereStr)
{
	const char *curStrPos = whereStr;
	do
	{
		const char *startVal = strchr(curStrPos, '=');
		const char *endVal = strchr(curStrPos, CONDITIONS_SEPARATOR);
		if (endVal == NULL)
		{
			if (startVal != NULL)
			{
				unsigned int nameLen = startVal - curStrPos;
				char name[2048];
				if (nameLen == 0 || nameLen >= sizeof(name))
					return EXIT_FAILURE;
				++startVal;
				strncpy(name, curStrPos, nameLen);
				name[nameLen] = '\0';
				return whereSetConditions(whr, name, startVal, 0);
			}
			return whereSetConditions(whr, curStrPos, NULL, 1);
		}
		unsigned int nameLen;
		if (startVal != NULL && startVal < endVal)
			nameLen = startVal - curStrPos;
		else
			nameLen = endVal - curStrPos;
		char name[2048];
		if (nameLen == 0 || nameLen >= sizeof(name))
			return EXIT_FAILURE;
		strncpy(name, curStrPos, nameLen);
		name[nameLen] = '\0';
		if (startVal == NULL || startVal > endVal)
		{
			if (whereSetConditions(whr, name, NULL, 1) != EXIT_SUCCESS)
				return EXIT_FAILURE;
		}
		else
		{
			++startVal;
			char val[2048];
			unsigned int valLen = endVal - startVal;
			if (valLen >= sizeof(val))
				return EXIT_FAILURE;
			if (valLen != 0)
				strncpy(val, startVal, valLen);
			val[valLen] = '\0';
			if (whereSetConditions(whr, name, val, 0) != EXIT_SUCCESS)
				return EXIT_FAILURE;
		}
		if (endVal == NULL)
			return (whr->condCount == 0) ? EXIT_FAILURE : EXIT_SUCCESS;
		curStrPos = endVal + 1;
	} while (*curStrPos != '\0');
	return EXIT_FAILURE;
}

int whereSetConditions(struct WhereStruct *whr, const char *name, const char *value, unsigned int data)
{
	struct PropertyStruct *prop = propInit(name, value);
	if (prop == NULL)
		return EXIT_FAILURE;
	prop->userData = data;
	if (whereInsertConditions(whr, prop) == EXIT_SUCCESS)
		return EXIT_SUCCESS;
	propFree(prop);
	return EXIT_FAILURE;
}

int whereInsertConditions(struct WhereStruct *whr, struct PropertyStruct *prop)
{
	unsigned int max = whr->condMax;
	unsigned int cnt = whr->condCount;
	if (cnt == max)
	{
		struct PropertyStruct **newPtr;
		if (max == 0)
			newPtr = malloc(sizeof(struct PropertyStruct *) * CONDITION_INCREASE);
		else
			newPtr = realloc(whr->conditions, sizeof(struct PropertyStruct *) * (max + CONDITION_INCREASE));
		if (newPtr == NULL)
			return EXIT_FAILURE;
		bzero(&newPtr[max], sizeof(struct PropertyStruct *) * CONDITION_INCREASE);
		newPtr[max] = prop;
		whr->conditions = newPtr;
		whr->condMax += CONDITION_INCREASE;
		++whr->condCount;
		return EXIT_SUCCESS;
	}
	whr->conditions[cnt] = prop;
	++whr->condCount;
	return EXIT_SUCCESS;
}
