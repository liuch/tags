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
#define FIELDS_SEPARATOR  ','

struct FileNameCache
{
	const char    *fileName;
	unsigned int  fileNum;
};

struct FieldStruct *fieldsFind(const struct FieldListStruct *fields, const char *name, unsigned int len);
void fieldsResetCache(const struct FieldListStruct *fields);
struct FieldStruct *fldInit(const char *name, unsigned int len);
void fldFree(struct FieldStruct *fld);
enum FieldType fldGetType(const char *name, unsigned int len);
const char *fldGetValue(struct FieldStruct *fld, const struct ItemStruct *item, unsigned int fileNum);

struct FieldListStruct *fieldsInit(const char *fieldsList)
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

	const char *pStart = fieldsList;
	while (pStart != '\0')
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
		const char *pEnd = strchr(pStart, FIELDS_SEPARATOR);
		if (pEnd == NULL)
			len = strlen(pStart);
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

int fieldsPrintRow(const struct FieldListStruct *fields, const struct ItemStruct *item, const char *baseDir, FILE *fd)
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
				if (fputc('\n', fd) != EOF)
					res = EXIT_SUCCESS;
				break;
			}

			if (i != 0 && fputc('\t', fd) == EOF)
				break;

			struct FieldStruct *fld = fields->columns[i];
			if (fld != NULL)
			{
				const char *pVal = fldGetValue(fld, item, fileNum);
				if (pVal == NULL)
					return EXIT_FAILURE;
				if (strlen(pVal) == 0)
					pVal = "-";
				if (fld->type == FileName && baseDir[0] != '\0' && fputs(baseDir, fd) == EOF)
					break;
				if (fputs(pVal, fd) == EOF)
					break;
			}
			else if (fputc('-', fd) == EOF)
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

struct FieldStruct *fieldsFind(const struct FieldListStruct *fields, const char *name, unsigned int len)
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
			const char *pName = fld->name;
			if (strncmp(pName, name, len) == 0 && pName[len] == '\0')
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

struct FieldStruct *fldInit(const char* name, unsigned int len)
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

	struct FieldStruct *fld = malloc(sizeof(struct FieldStruct) + (nameSize + chacheSize) * sizeof(char));
	if (fld != NULL)
	{
		fld->type = type;
		fld->cache.empty = 1;
		fld->cache.size = chacheSize; // in characters
		fld->cache.offset = sizeof(struct FieldStruct) + nameSize * sizeof(char);
		char *pName = fld->name;
		if (type == Property)
		{
			strncpy(pName, name, len);
			pName[len] = '\0';
		}
		else
			pName[0] = '\0';
	}
	return fld;
}

void fldFree(struct FieldStruct *fld)
{
	free(fld);
}

enum FieldType fldGetType(const char *name, unsigned int len)
{
	if (name[0] == '@')
	{
		if (len == 9)
		{
			if (strncmp(name, "@FileName", len) == 0)
				return FileName;
			if (strncmp(name, "@FileSize", len) == 0)
				return FileSize;
		}
		return Error;
	}
	return Property;
}

const char *fldGetValue(struct FieldStruct *fld, const struct ItemStruct *item, unsigned int fileNum)
{
	void *pVal = (void *)fld + fld->cache.offset;
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
			*((char *)pVal) = '\0';
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
			const char *nm = itemGetFileName(item, fileNum);
			((struct FileNameCache *)pVal)->fileNum  = fileNum;
			((struct FileNameCache *)pVal)->fileName = nm;
			fld->cache.empty = 0;
			return nm;
		}
		*((char *)pVal) = '\0';
	}
	else if (fld->type == FileSize)
	{
		uitoa(item->fileSize, pVal);
	}

	fld->cache.empty = 0;
	return pVal;
}
