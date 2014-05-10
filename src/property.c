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

#include "property.h"

#define PROP_SUBVAL_AV_LENGTH  5
#define SUBVAL_SEPARATOR       L','

void propFreeIndexes(struct PropertyStruct *prop);
int subvalCompareByValue(const void *p1, const void *p2);
int subvalCompareByUser(const void *p1, const void *p2);
struct SubvalHandle *propIsSubval_(const struct PropertyStruct *prop, const wchar_t *subval, unsigned int len);
int trimString(const wchar_t **pStartChar, const wchar_t **pEndChar);

struct PropertyStruct *propInit(const wchar_t *name, const wchar_t *value)
{
	{
		const wchar_t ch = name[0];
		if (ch == L'!')
			return NULL;
	}
	unsigned int nameSize = (wcslen(name) + 1) * sizeof(wchar_t);
	unsigned int valLen   = 1;
	if (value != NULL)
		valLen += wcslen(value);

	unsigned int sizeWithoutValues = sizeof(struct PropertyStruct) + nameSize;
	sizeWithoutValues = (sizeWithoutValues + sizeof(void *) - 1) & (~(sizeof(void *) - 1)); // alignment
	unsigned int size = sizeWithoutValues;
	size += (sizeof(struct SubvalHandle) + PROP_SUBVAL_AV_LENGTH) * (valLen / PROP_SUBVAL_AV_LENGTH + 1);
	size  = (size + sizeof(void *) - 1) & (~(sizeof(void *) - 1)); // alignment
	struct PropertyStruct *prop = malloc(size);
	if (prop == NULL)
		return NULL;
	prop->maxSize = size;
	prop->blkCount = 0;

	{
		wchar_t *pNm = (void *)prop + sizeof(struct PropertyStruct);
		wcscpy(pNm, name);
	}

	prop->curSize = sizeWithoutValues;
	prop->valCount = 0;
	prop->valuesOffset = sizeWithoutValues;
	prop->arrayDirect = NULL;
	prop->arrayByValue = NULL;
	prop->arrayByUser = NULL;
	prop->userData = 0;

	if (value != NULL)
	{
		unsigned int curSubvalOffset = prop->valuesOffset;
		const wchar_t *pStart = value;
		do
		{
			const wchar_t *pEnd = wcschr(pStart, SUBVAL_SEPARATOR);

			if (pEnd == NULL)
			{
				struct PropertyStruct *newProp = propAddSubval(prop, pStart);
				if (newProp == NULL)
					propFree(prop);
				prop = newProp;
				break;
			}

			const wchar_t *pEndTrim = pEnd;
			unsigned int len = trimString(&pStart, &pEndTrim);
			if (len != 0 && propIsSubval_(prop, pStart, len) == NULL)
			{

				unsigned int subvalSize = sizeof(struct SubvalHandle) + (len + 1) * sizeof(wchar_t);
				subvalSize = (subvalSize + sizeof(void *) - 1) & (~(sizeof(void *) - 1)); // alignment
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
				wchar_t *pCh = (void *)prop + curSubvalOffset + sizeof(struct SubvalHandle);
				wcsncpy(pCh, pStart, len);
				pCh[len] = L'\0';
				++prop->blkCount;
				++prop->valCount;
				curSubvalOffset += subvalSize;
			}

			pStart = pEnd + 1;
		} while (*pStart != L'\0');
	}
	else
	{
		struct PropertyStruct *newProp = propAddSubval(prop, L"");
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

const wchar_t *propGetName(const struct PropertyStruct *prop)
{
	return ((void *)prop + sizeof(struct PropertyStruct));
}

int propIsEmpty(struct PropertyStruct *prop)
{
	return (prop->valCount == 1 && (propGetSubval(prop, 0, None))[0] == L'\0') ? 1 : 0;
}

int propIsEqualValue(struct PropertyStruct *prop1, struct PropertyStruct *prop2)
{
	const unsigned int valCnt = prop1->valCount;
	if (valCnt != prop2->valCount)
		return 0;

	unsigned int i = 0;
	for ( ; i < valCnt; ++i)
	{
		const wchar_t *valueStr = propGetSubval(prop1, i, None);
		if (!propIsSubval(prop2, valueStr))
			return 0;
	}
	return 1;
}

const wchar_t *propGetSubval(struct PropertyStruct *prop, unsigned int num, enum PropSubvalOrder order)
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

int propAddSubvalues(struct PropertyStruct **pp, const wchar_t *value)
{
	unsigned int len = wcslen(value);
	if (len == 0)
		return EXIT_FAILURE;

	struct PropertyStruct *prop = *pp;

	wchar_t *buff = malloc((len + 1) * sizeof(wchar_t));
	if (buff == NULL)
		return EXIT_FAILURE;
	wcscpy(buff, value);

	const wchar_t *pStart = buff;
	do
	{
		wchar_t *pEnd = wcschr(pStart, SUBVAL_SEPARATOR);
		if (pEnd != NULL)
			*pEnd = L'\0';

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
	} while (*pStart != L'\0');
	free(buff);
	return (prop == NULL) ? EXIT_FAILURE : EXIT_SUCCESS;
}

struct SubvalHandle *propIsSubval(const struct PropertyStruct *prop, const wchar_t *subval)
{
	return propIsSubval_(prop, subval, wcslen(subval));
}

struct PropertyStruct* propAddSubval(struct PropertyStruct *prop, const wchar_t *value)
{
	const wchar_t *valPtr = value;
	unsigned int valLen = trimString(&valPtr, NULL);

	if (prop->valCount != 0)
	{
		if (valLen == 0)
			return prop;
		if (propIsEmpty(prop))
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

	unsigned int subvalSize = sizeof(struct SubvalHandle) + (valLen + 1) * sizeof(wchar_t);
	subvalSize = (subvalSize + sizeof(void *) - 1) & (~(sizeof(void *) - 1)); // alignment
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
	wchar_t *pCh = (void *)pSubval + sizeof(struct SubvalHandle);
	wcsncpy(pCh, valPtr, valLen);
	pCh[valLen] = L'\0';

	propFreeIndexes(newProp);
	return newProp;
}

int propDelSubvalues(struct PropertyStruct **pp, const wchar_t *value)
{
	struct PropertyStruct *prop = *pp;

	unsigned int len = wcslen(value);
	if (len == 0)
		return EXIT_FAILURE;

	wchar_t *buff = malloc((len + 1) * sizeof(wchar_t));
	if (buff == NULL)
		return EXIT_FAILURE;
	wcscpy(buff, value);

	const wchar_t *pStart = buff;
	do
	{
		wchar_t *pEnd = wcschr(pStart, SUBVAL_SEPARATOR);
		if (pEnd != NULL)
			*pEnd = L'\0';

		struct SubvalHandle *pSubval = (void *)prop + prop->valuesOffset;
		unsigned int cnt = prop->valCount;
		while (cnt > 0)
		{
			if (pSubval->subvalStatus == Used)
			{
				wchar_t *val = (void *)pSubval + sizeof(struct SubvalHandle);
				if (wcscasecmp(val, pStart) == 0)
				{
					if (prop->valCount != 1)
					{
						pSubval->subvalStatus = NotUsed;
						--prop->valCount;
					}
					else
						val[0] = L'\0';
					break;
				}
				--cnt;
			}
			pSubval = (void *)pSubval + pSubval->subvalSize;
		}

		if (pEnd == NULL)
			break;
		pStart = pEnd + 1;
	} while (*pStart != L'\0');
	free(buff);
	propFreeIndexes(*pp);

	return EXIT_SUCCESS;
}

const wchar_t *subvalString(const struct SubvalHandle *subval)
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
	return wcscmp(subvalString(subval1), subvalString(subval2));
}

int subvalCompareByUser(const void *p1, const void *p2)
{
	const struct SubvalHandle *subval1 = *(const struct SubvalHandle **)p1;
	const struct SubvalHandle *subval2 = *(const struct SubvalHandle **)p2;
	return subval1->userData - subval2->userData;
}

struct SubvalHandle *propIsSubval_(const struct PropertyStruct *prop, const wchar_t *subval, unsigned int len)
{
	unsigned int cnt = prop->valCount;
	struct SubvalHandle *pSubval = (void *)prop + prop->valuesOffset;
	while (cnt != 0)
	{
		if (pSubval->subvalStatus == Used)
		{
			const wchar_t *pVal = (void *)pSubval + sizeof(struct SubvalHandle);
			if (wcsncasecmp(pVal, subval, len) == 0 && pVal[len] == L'\0')
				return pSubval;
			--cnt;
		}
		pSubval = (void *)pSubval + pSubval->subvalSize;
	}
	return NULL;
}

int trimString(const wchar_t **pStartChar, const wchar_t **pEndChar)
{
	const wchar_t *valStart = *pStartChar;
	const wchar_t *valEnd = NULL;
	if (pEndChar != NULL)
		valEnd = *pEndChar;
	while (valStart != valEnd)
	{
		const wchar_t ch = *valStart;
		if (ch != L' ' || (valEnd == NULL && ch == L'\0'))
			break;
		++valStart;
	}

	const wchar_t *valMid = valStart;
	const wchar_t *valMidTmp = valStart;
	while (valMidTmp != valEnd)
	{
		const wchar_t ch = *valMidTmp++;
		if (valEnd == NULL && ch == L'\0')
			break;
		if (ch != L' ')
			valMid = valMidTmp;
	}

	*pStartChar = valStart;
	if (pEndChar != NULL)
		*pEndChar = valMid;
	return valMid - valStart;
}
