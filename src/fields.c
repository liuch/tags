/*
 * fields.c
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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "fields.h"
#include "utils.h"

#define FIELDS_INCREASE   10
#define FIELDS_SEPARATOR  L','

struct FileNameCache
{
	const wchar_t *fileName;
	unsigned int  fileNum;
};

struct FieldStruct *fieldsFind(const struct FieldListStruct *fields, const wchar_t *name, unsigned int len);
void fieldsResetCache(const struct FieldListStruct *fields);
struct FieldStruct *fldInit(const wchar_t *name, unsigned int len);
void fldFree(struct FieldStruct *fld);
enum FieldType fldGetType(const wchar_t *name, unsigned int len);
const wchar_t *fldGetValue(struct FieldStruct *fld, const struct ItemStruct *item, unsigned int fileNum);

struct FieldListStruct *fieldsInit(const wchar_t *fieldsList)
{
	struct FieldListStruct *fields = malloc(sizeof(struct FieldListStruct));
	if (fields == NULL)
		return NULL;

	bzero(fields, sizeof(struct FieldListStruct));

	fields->fieldsList = malloc(sizeof(struct FieldStruct *) * FIELDS_INCREASE);
	if (fields->fieldsList == NULL)
	{
		fieldsFree(fields);
		return NULL;
	}
	fields->fieldsMax = FIELDS_INCREASE;

	fields->columns = malloc(sizeof(struct FieldStruct *) * FIELDS_INCREASE);
	if (fields->columns == NULL)
	{
		fieldsFree(fields);
		return NULL;
	}
	fields->colMax = FIELDS_INCREASE;

	const wchar_t *pStart = fieldsList;
	while (pStart != L'\0')
	{
		if (fields->colCount == fields->colMax)
		{
			struct FieldStruct **pC = realloc(fields->columns, sizeof(struct FieldStruct *) * (fields->colMax + FIELDS_INCREASE));
			if (pC == NULL)
			{
				fieldsFree(fields);
				return NULL;
			}
			fields->columns = pC;
			fields->colMax += FIELDS_INCREASE;
		}

		unsigned int len;
		const wchar_t *pEnd = wcschr(pStart, FIELDS_SEPARATOR);
		if (pEnd == NULL)
			len = wcslen(pStart);
		else
			len = pEnd - pStart;

		struct FieldStruct *fld = NULL;
		if (len != 0)
		{
			fld = fieldsFind(fields, pStart, len);
			if (fld == NULL)
			{
				if (fields->fieldsCount == fields->fieldsMax)
				{
					struct FieldStruct **pF = realloc(fields->fieldsList, sizeof(struct FieldStruct *) * (fields->fieldsMax + FIELDS_INCREASE));
					if (pF == NULL)
					{
						fieldsFree(fields);
						return NULL;
					}
					fields->fieldsList = pF;
					fields->fieldsMax += FIELDS_INCREASE;
				}
				fld = fldInit(pStart, len);
				if (fld == NULL)
				{
					fieldsFree(fields);
					return NULL;
				}
				fields->fieldsList[fields->fieldsCount++] = fld;
			}
		}
		fields->columns[fields->colCount++] = fld;

		if (pEnd == NULL)
			break;
		pStart = pEnd + 1;
	}

	return fields;
}

void fieldsFree(struct FieldListStruct* fields)
{
	unsigned int cnt = fields->fieldsCount;
	unsigned int i;
	if (fields->fieldsList != NULL)
	{
		struct FieldStruct **pFld = fields->fieldsList;
		for (i = 0; i < cnt; ++i)
			free(pFld[i]);
		free(fields->fieldsList);
	}

	if (fields->columns != NULL)
		free(fields->columns);

	free(fields);
}

int fieldsPrintRow(const struct FieldListStruct *fields, const struct ItemStruct *item, const wchar_t *baseDir, FILE *fd)
{
	fieldsResetCache(fields);
	unsigned int fileNum = 0;
	do
	{
		int res = EXIT_FAILURE;
		unsigned int cnt = fields->colCount;
		unsigned int i;
		for (i = 0; ; ++i)
		{
			if (i == cnt)
			{
				if (fputwc(L'\n', fd) != WEOF)
					res = EXIT_SUCCESS;
				break;
			}

			if (i != 0 && fputwc(L'\t', fd) == WEOF)
				break;

			struct FieldStruct *fld = fields->columns[i];
			if (fld != NULL)
			{
				const wchar_t *pVal = fldGetValue(fld, item, fileNum);
				if (pVal == NULL)
					return EXIT_FAILURE;
				if (wcslen(pVal) == 0)
					pVal = L"-";
				if (fld->type == FileName && baseDir[0] != L'\0' && fputws(baseDir, fd) == EOF)
					break;
				if (fputws(pVal, fd) == EOF)
					break;
			}
			else if (fputwc(L'-', fd) == WEOF)
				break;
		}
		if (res != EXIT_SUCCESS)
		{
			perror("printRow");
			return EXIT_FAILURE;
		}
		++fileNum;
	} while (fileNum < item->fileNameCount);
	return EXIT_SUCCESS;
}

// ************* Private ***************

struct FieldStruct *fieldsFind(const struct FieldListStruct *fields, const wchar_t *name, unsigned int len)
{
	enum FieldType type = fldGetType(name, len);
	if (type == Error)
		return NULL;
	unsigned int count = fields->fieldsCount;
	struct FieldStruct **pFields = fields->fieldsList;
	unsigned int i;
	for (i = 0; i < count; ++i)
	{
		struct FieldStruct *fld = pFields[i];
		if (type == fld->type)
		{
			if (type != Property)
				return fld;
			const wchar_t *pName = fld->name;
			if (wcsncmp(pName, name, len) == 0 && pName[len] == L'\0')
				return fld;
		}
	}
	return NULL;
}

void fieldsResetCache(const struct FieldListStruct *fields)
{
	unsigned int cnt = fields->fieldsCount;
	struct FieldStruct **pFld = fields->fieldsList;
	while (cnt != 0)
	{
		struct FieldStruct *fld = *pFld++;
		fld->cache.empty = 1;
		--cnt;
	}
}

struct FieldStruct *fldInit(const wchar_t* name, unsigned int len)
{
	enum FieldType type = fldGetType(name, len);
	unsigned int nameSize;
	unsigned int chacheSize;
	switch (type)
	{
		case Property:
			nameSize   = len + 1;
			chacheSize = 2048;
			break;
		case FileName:
			nameSize   = 0;
			chacheSize = sizeof(struct FileNameCache);
			break;
		case FileSize:
			nameSize   = 0;
			chacheSize = 32;
			break;
		case Error:
			return NULL;
	}

	struct FieldStruct *fld = malloc(sizeof(struct FieldStruct) + (nameSize + chacheSize) * sizeof(wchar_t));
	if (fld != NULL)
	{
		fld->type = type;
		fld->cache.empty = 1;
		fld->cache.size = chacheSize; // in characters
		fld->cache.offset = sizeof(struct FieldStruct) + nameSize * sizeof(wchar_t);
		wchar_t *pName = fld->name;
		if (type == Property)
		{
			wcsncpy(pName, name, len);
			pName[len] = L'\0';
		}
		else
			pName[0] = L'\0';
	}
	return fld;
}

void fldFree(struct FieldStruct *fld)
{
	free(fld);
}

enum FieldType fldGetType(const wchar_t *name, unsigned int len)
{
	if (name[0] == L'@')
	{
		if (len == 9)
		{
			if (wcsncmp(name, L"@FileName", len) == 0)
				return FileName;
			if (wcsncmp(name, L"@FileSize", len) == 0)
				return FileSize;
		}
		return Error;
	}
	return Property;
}

const wchar_t *fldGetValue(struct FieldStruct *fld, const struct ItemStruct *item, unsigned int fileNum)
{
	wchar_t *pVal = (void *)fld + fld->cache.offset;
	if (!fld->cache.empty)
	{
		if (fld->type != FileName)
			return pVal;
		if (((struct FileNameCache *)pVal)->fileNum == fileNum)
			return ((struct FileNameCache *)pVal)->fileName;
	}

	if (fld->type == Property)
	{
		struct PropertyStruct **pProp = itemGetPropertyPosByName(item, fld->name);
		if (pProp == NULL)
		{
			*pVal = L'\0';
		}
		else
		{
			if (itemPropertyValueToString(item, pProp - item->props, pVal, fld->cache.size) != EXIT_SUCCESS)
				return NULL;
		}
	}
	else if (fld->type == FileName)
	{
		if (item->fileNameCount > fileNum)
		{
			const wchar_t *nm = itemGetFileName(item, fileNum);
			((struct FileNameCache *)pVal)->fileNum  = fileNum;
			((struct FileNameCache *)pVal)->fileName = nm;
			fld->cache.empty = 0;
			return nm;
		}
		*pVal = L'\0';
	}
	else if (fld->type == FileSize)
	{
		uitow(item->fileSize, pVal);
	}

	fld->cache.empty = 0;
	return pVal;
}
