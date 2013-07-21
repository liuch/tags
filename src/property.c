/*
 * property.c
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

#include "property.h"

#define PROP_SUBVAL_AV_LENGTH  5
#define SUBVAL_SEPARATOR       ','

void propFreeIndexes(struct PropertyStruct *prop);
int subvalCompareByValue(const void *p1, const void *p2);
int subvalCompareByUser(const void *p1, const void *p2);
struct SubvalHandle *propIsSubval_(const struct PropertyStruct *prop, const char *subval, unsigned int len);
int trimString(const char **pStartChar, const char **pEndChar);

struct PropertyStruct *propInit(const char *name, const char *value)
{
	{
		const char ch = name[0];
		if (ch == '!')
			return NULL;
	}
	unsigned int nameLen = strlen(name) + sizeof(char);
	unsigned int valLen = sizeof(char);
	if (value != NULL)
		valLen += strlen(value);

	unsigned int size = sizeof(struct PropertyStruct) + nameLen;
	size = (size + sizeof(void *) - 1) & (~(sizeof(void *) - 1));
	unsigned int size2;
	size2 = size + (sizeof(struct SubvalHandle) + PROP_SUBVAL_AV_LENGTH) * (valLen / PROP_SUBVAL_AV_LENGTH + 1);
	size2 = (size2 + sizeof(void *) - 1) & (~(sizeof(void *) - 1));
	struct PropertyStruct *prop = malloc(size2);
	if (prop == NULL)
		return NULL;
	prop->maxSize = size2;
	prop->blkCount = 0;

	{
		char *pNm = (void *)prop + sizeof(struct PropertyStruct);
		strcpy(pNm, name);
	}

	prop->curSize = size;
	prop->valCount = 0;
	prop->valuesOffset = size;
	prop->arrayDirect = NULL;
	prop->arrayByValue = NULL;
	prop->arrayByUser = NULL;
	prop->userData = 0;

	if (value != NULL)
	{
		unsigned int curSubvalOffset = prop->valuesOffset;
		const char *pStart = value;
		do
		{
			const char *pEnd = strchr(pStart, SUBVAL_SEPARATOR);

			if (pEnd == NULL)
			{
				struct PropertyStruct *newProp = propAddSubval(prop, pStart);
				if (newProp == NULL)
					propFree(prop);
				prop = newProp;
				break;
			}

			const char *pEndTrim = pEnd;
			unsigned int len = trimString(&pStart, &pEndTrim);
			if (len != 0 && propIsSubval_(prop, pStart, len) == NULL)
			{

				unsigned int subvalSize = sizeof(struct SubvalHandle) + (len + 1) * sizeof(char);
				subvalSize = (subvalSize + sizeof(void *) - 1) & (~(sizeof(void *) - 1));
				unsigned int newCurSize = prop->curSize + subvalSize;
				if (prop->maxSize < newCurSize)
				{
					struct PropertyStruct *newProp = realloc(prop, newCurSize);
					if (newProp == NULL)
					{
						propFree(prop);
						return NULL;
					}
					prop = newProp;
					prop->maxSize = newCurSize;
				}
				prop->curSize = newCurSize;
				struct SubvalHandle *pSubval = (void *)prop + curSubvalOffset;
				pSubval->subvalSize = subvalSize;
				pSubval->subvalStatus = Used;
				pSubval->userData = 0;
				char *pCh = (void *)prop + curSubvalOffset + sizeof(struct SubvalHandle);
				strncpy(pCh, pStart, len);
				pCh[len] = '\0';
				++prop->blkCount;
				++prop->valCount;
				curSubvalOffset += subvalSize;
			}

			pStart = pEnd + 1;
		} while (*pStart != '\0');
	}
	else
	{
		struct PropertyStruct *newProp = propAddSubval(prop, "");
		if (newProp == NULL)
			propFree(prop);
		prop = newProp;
	}
	return prop;
}

void propFree(struct PropertyStruct *prop)
{
	propFreeIndexes(prop);
	free(prop);
}

const char *propGetName(const struct PropertyStruct *prop)
{
	return ((void *)prop + sizeof(struct PropertyStruct));
}

int propIsEmpty(struct PropertyStruct *prop)
{
	return (prop->valCount == 1 && (propGetSubval(prop, 0, None))[0] == '\0') ? 1 : 0;
}

int propIsEqualValue(struct PropertyStruct *prop1, struct PropertyStruct *prop2)
{
	const unsigned int valCnt = prop1->valCount;
	if (valCnt != prop2->valCount)
		return 0;

	unsigned int i = 0;
	for ( ; i < valCnt; ++i)
	{
		const char *valueStr = propGetSubval(prop1, i, None);
		if (!propIsSubval(prop2, valueStr))
			return 0;
	}
	return 1;
}

const char *propGetSubval(struct PropertyStruct *prop, unsigned int num, enum PropSubvalOrder order)
{
	if (num >= prop->valCount)
		return NULL;

	struct SubvalHandle **index = propGetValueIndex(prop, order);
	if (index == NULL)
		return NULL;

	struct SubvalHandle *pSubval = index[num];
	return (void *)pSubval + sizeof(struct SubvalHandle);
}

struct SubvalHandle  **propGetValueIndex(struct PropertyStruct *prop, enum PropSubvalOrder order)
{
	unsigned int cnt = prop->valCount;
	if (cnt == 0)
		return NULL;

	struct SubvalHandle  ***ppArray = NULL;
	switch (order)
	{
		case None:
			ppArray = &prop->arrayDirect;
			break;
		case ByValue:
			ppArray = &prop->arrayByValue;
			break;
		case ByUser:
			ppArray = &prop->arrayByUser;
			break;
	}
	if (*ppArray != NULL)
		return *ppArray;

	struct SubvalHandle  **pArray = malloc(prop->valCount * sizeof(struct SubvalHandle *));
	*ppArray = pArray;
	if (pArray == NULL)
		return NULL;

	struct SubvalHandle *pSubval = (void *)prop + prop->valuesOffset;
	while (cnt != 0)
	{
		if (pSubval->subvalStatus == Used)
		{
			*pArray++ = pSubval;
			--cnt;
		}
		pSubval = (void *)pSubval + pSubval->subvalSize;
	}

	if (order == ByValue)
		qsort(*ppArray, prop->valCount, sizeof(struct SubvalHandle *), subvalCompareByValue);
	else if (order == ByUser)
		qsort(*ppArray, prop->valCount, sizeof(struct SubvalHandle *), subvalCompareByUser);

	return *ppArray;
}

int propAddSubvalues(struct PropertyStruct **pp, const char *value)
{
	unsigned int len = strlen(value);
	if (len == 0)
		return EXIT_FAILURE;

	struct PropertyStruct *prop = *pp;

	char *buff = malloc((len + 1) * sizeof(char));
	if (buff == NULL)
		return EXIT_FAILURE;
	strcpy(buff, value);

	const char *pStart = buff;
	do
	{
		char *pEnd = strchr(pStart, SUBVAL_SEPARATOR);
		if (pEnd != NULL)
			*pEnd = '\0';

		if (propIsSubval(prop, pStart) == NULL)
		{
			prop = propAddSubval(prop, pStart);
			if (prop == NULL)
				break;
			*pp = prop;
		}

		if (pEnd == NULL)
			break;
		pStart = pEnd + 1;
	} while (*pStart != '\0');
	free(buff);
	return (prop == NULL) ? EXIT_FAILURE : EXIT_SUCCESS;
}

struct SubvalHandle *propIsSubval(const struct PropertyStruct *prop, const char *subval)
{
	return propIsSubval_(prop, subval, strlen(subval));
}

struct PropertyStruct* propAddSubval(struct PropertyStruct *prop, const char *value)
{
	const char *valPtr = value;
	unsigned int valLen = trimString(&valPtr, NULL);

	if (prop->valCount != 0)
	{
		if (valLen == 0)
			return prop;
		if (propIsEmpty(prop) != 0)
		{
			struct PropertyStruct *newProp = propInit(propGetName(prop), value);
			if (newProp != NULL)
			{
				newProp->userData = prop->userData;
				propFree(prop);
			}
			return newProp;
		}
	}

	struct PropertyStruct *newProp = prop;

	unsigned int subvalSize = sizeof(struct SubvalHandle) + (valLen + 1) * sizeof(char);
	subvalSize = (subvalSize + sizeof(void *) - 1) & (~(sizeof(void *) - 1));
	unsigned int freeOffset = 0;
	unsigned int freeSize = 0;
	unsigned int blkCnt = prop->blkCount;
	struct SubvalHandle *pSubval = (void *)prop + prop->valuesOffset;
	if (blkCnt > prop->valCount)
		for ( ; blkCnt > 0; --blkCnt)
		{
			unsigned int sz = pSubval->subvalSize;
			if (pSubval->subvalStatus == NotUsed)
			{
				if (subvalSize <= sz && (freeOffset == 0 || freeSize > sz))
				{
					freeSize = sz;
					freeOffset = (void *)pSubval - (void *)prop;
					if (subvalSize == sz)
						break;
				}
			}
			pSubval = (void *)pSubval + sz;
		}

	pSubval = (void *)prop + freeOffset;
	if (freeOffset == 0)
	{
		freeOffset = prop->curSize;
		unsigned int newSize = prop->curSize + subvalSize;
		if (prop->maxSize < newSize)
		{
			newProp = realloc(prop, newSize);
			if (newProp == NULL)
				return NULL;
			newProp->maxSize = newSize;
		}
		newProp->curSize = newSize;
		++newProp->blkCount;
		pSubval = (void *)newProp + freeOffset;
		pSubval->subvalSize = subvalSize;
	}

	++newProp->valCount;
	pSubval->subvalStatus = Used;
	pSubval->userData = 0;
	char *pCh = (void *)pSubval + sizeof(struct SubvalHandle);
	strncpy(pCh, valPtr, valLen);
	pCh[valLen] = '\0';

	propFreeIndexes(newProp);
	return newProp;
}

int propDelSubvalues(struct PropertyStruct **pp, const char *value)
{
	struct PropertyStruct *prop = *pp;

	unsigned int len = strlen(value);
	if (len == 0)
		return EXIT_FAILURE;

	char *buff = malloc((len + 1) * sizeof(char));
	if (buff == NULL)
		return EXIT_FAILURE;
	strcpy(buff, value);

	const char *pStart = buff;
	do
	{
		char *pEnd = strchr(pStart, SUBVAL_SEPARATOR);
		if (pEnd != NULL)
			*pEnd = '\0';

		struct SubvalHandle *pSubval = (void *)prop + prop->valuesOffset;
		unsigned int cnt = prop->valCount;
		while (cnt > 0)
		{
			if (pSubval->subvalStatus == Used)
			{
				char *val = (void *)pSubval + sizeof(struct SubvalHandle);
				if (strcmp(val, pStart) == 0)
				{
					if (prop->valCount != 1)
					{
						pSubval->subvalStatus = NotUsed;
						--prop->valCount;
					}
					else
						val[0] = '\0';
					break;
				}
				--cnt;
			}
			pSubval = (void *)pSubval + pSubval->subvalSize;
		}

		if (pEnd == NULL)
			break;
		pStart = pEnd + 1;
	} while (*pStart != '\0');
	free(buff);
	propFreeIndexes(*pp);

	return EXIT_SUCCESS;
}

const char *subvalString(const struct SubvalHandle *subval)
{
	return (void *)subval + sizeof(struct SubvalHandle);
}

/************** PRIVATE *************************/

void propFreeIndexes(struct PropertyStruct *prop)
{
	if (prop->arrayDirect != NULL)
	{
		free(prop->arrayDirect);
		prop->arrayDirect = NULL;
	}
	if (prop->arrayByValue != NULL)
	{
		free(prop->arrayByValue);
		prop->arrayByValue = NULL;
	}
	if (prop->arrayByUser != NULL)
	{
		free(prop->arrayByUser);
		prop->arrayByUser = NULL;
	}
}

int subvalCompareByValue(const void *p1, const void *p2)
{
	const struct SubvalHandle *subval1 = *(const struct SubvalHandle **)p1;
	const struct SubvalHandle *subval2 = *(const struct SubvalHandle **)p2;
	return strcmp(subvalString(subval1), subvalString(subval2));
}

int subvalCompareByUser(const void *p1, const void *p2)
{
	const struct SubvalHandle *subval1 = *(const struct SubvalHandle **)p1;
	const struct SubvalHandle *subval2 = *(const struct SubvalHandle **)p2;
	return subval1->userData - subval2->userData;
}

struct SubvalHandle *propIsSubval_(const struct PropertyStruct *prop, const char *subval, unsigned int len)
{
	unsigned int cnt = prop->valCount;
	struct SubvalHandle *pSubval = (void *)prop + prop->valuesOffset;
	while (cnt != 0)
	{
		if (pSubval->subvalStatus == Used)
		{
			const char *pVal = (void *)pSubval + sizeof(struct SubvalHandle);
			if (strncmp(pVal, subval, len) == 0 && pVal[len] == '\0')
				return pSubval;
			--cnt;
		}
		pSubval = (void *)pSubval + pSubval->subvalSize;
	}
	return NULL;
}

int trimString(const char **pStartChar, const char **pEndChar)
{
	const char *valStart = *pStartChar;
	const char *valEnd = NULL;
	if (pEndChar != NULL)
		valEnd = *pEndChar;
	while (valStart != valEnd)
	{
		const char ch = *valStart;
		if (ch != ' ' || (valEnd == NULL && ch == '\0'))
			break;
		++valStart;
	}

	const char *valMid = valStart;
	const char *valMidTmp = valStart;
	while (valMidTmp != valEnd)
	{
		const char ch = *valMidTmp++;
		if (valEnd == NULL && ch == '\0')
			break;
		if (ch != ' ')
			valMid = valMidTmp;
	}

	*pStartChar = valStart;
	if (pEndChar != NULL)
		*pEndChar = valMid;
	return valMid - valStart;
}
