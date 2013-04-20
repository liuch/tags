/*
 * tagfile.c
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

#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "tagfile.h"
#include "sha1.h"
#include "file.h"
#include "item.h"
#include "common.h"
#include "fields.h"
#include "where.h"

#define READ_BUFFER_SIZE 4096

const char tagFileName[] = "tags.info";
const char tmpFileNameTemplate[] = "tags_XXXXXX";


int tagfileScanStartItemString(const char *str, size_t *sz, char *hash);
int tagfileItemOutput(FILE *fd, const struct ItemStruct *item);
int tagfileShowProps(struct TagFileStruct *tf, struct ItemStruct *item);
int tagfileList(struct TagFileStruct *tf, struct FieldListStruct *fields, const struct WhereStruct *whr);

struct TagFileStruct *tagfileInit(const char *dPath, const char *fName, enum TagFileMode mode);
struct TagFileStruct *tagfileInitFd(FILE *fd);
struct TagFileStruct *tagfileInitStruct(enum TagFileType type, enum TagFileMode mode);
enum ErrorId tagfileInitPath(struct TagFileStruct *tf, const char *dPath, const char *fName);
enum ErrorId tagfileInitReadBuffer(struct TagFileStruct *tf);
struct TagFileStruct *tagfileCloneForSubdir(const struct TagFileStruct *tf, const char *subdir);
void tagfileFree(struct TagFileStruct *tf);
enum ErrorId tagfileOpen(struct TagFileStruct *tf);
enum ErrorId tagfileReadHeader(struct TagFileStruct *tf);
void tagfileClose(struct TagFileStruct *tf);
int tagfileApplyIndexFile(struct TagFileStruct *tf, FILE *fd);
enum ErrorId tagfileReadString(struct TagFileStruct *tf);
enum ErrorId tagfileNextStartItemString(struct TagFileStruct *tf);
enum ErrorId tagfileItemBodyLoad(struct TagFileStruct *tf, struct ItemStruct *item);
struct ItemStruct *tagfileItemLoad(struct TagFileStruct *tf);
struct ItemStruct *tagfileGetNextItem(struct TagFileStruct *tf);
struct ItemStruct *tagfileItemsScanBySize(struct TagFileStruct *tf, size_t sz);
struct ItemStruct *tagfileGetItemForFileName(struct TagFileStruct *tf, const char *fileName);


int tagfileCreate()
{
	fprintf(stdout, "Initialization...\n");
	if (access(tagFileName, F_OK) == -1 && errno == ENOENT)
	{
		int res = EXIT_FAILURE;
		FILE *fd = fopen(tagFileName, "w");
		if (fd != NULL)
		{
			if (fputs("!tags-info", fd) != EOF && fputs("\n!version=0.1\n!format=simple\n\n", fd) != EOF)
			{
				fprintf(stdout, "Ok\n");
				res = EXIT_SUCCESS;
			}
			fclose(fd);
		}
		if (res != EXIT_SUCCESS)
			perror("tagfile");
		return res;
	}
	else
		fprintf(stderr, "Error: file %s already exists\n", tagFileName);

	return EXIT_FAILURE;
}

int tagfileStatus(char **filesArray, unsigned int filesCount)
{
	int nameOffset = fileBaseNameOffset(filesArray, filesCount);
	if (nameOffset < 0)
		return EXIT_FAILURE;

	char sdir[PATH_MAX];
	if (nameOffset > 0)
		strncpy(&sdir[0], filesArray[0], nameOffset);
	sdir[nameOffset] = '\0';
	struct TagFileStruct *tf = tagfileInit(sdir, NULL, ReadOnly);
	if (tf == NULL)
		return EXIT_FAILURE;
	if (tf->lastError != ErrorNone)
	{
		tagfileFree(tf);
		return EXIT_FAILURE;
	}

	int res = EXIT_FAILURE;
	struct FileItemList *fil = fileitemsInitFromList(filesArray, filesCount, nameOffset, MaskFile);
	if (fil != NULL)
	{
		fileitemsSort(fil, SortBySize);

		if (tf->readBuffer[0] != '[')
			tagfileNextStartItemString(tf);

		unsigned int fiCnt      = fil->filesCount;
		struct FileItem **pFi   = fil->fileItems;
		unsigned int szCurPos   = 0;
		unsigned int szStartPos = 0;
		size_t szCur = pFi[szCurPos]->size;
		while (tf->lastError == ErrorNone)
		{
			size_t sz;
			if (tagfileScanStartItemString(tf->readBuffer, &sz, NULL) != EXIT_SUCCESS)
				break;

			if (sz > szCur)
			{
				for (++szCurPos; szCurPos < fiCnt && (szCur = pFi[szCurPos]->size) < sz; ++szCurPos)
					;
				if (szCurPos == fiCnt)
				{
					res = EXIT_SUCCESS;
					break;
				}
				szStartPos = szCurPos;
			}

			if (sz == szCur)
			{
				struct ItemStruct *item = tagfileItemLoad(tf);
				if (item == NULL)
					break;
				for ( ; szCurPos < fiCnt && pFi[szCurPos]->size == szCur; ++szCurPos)
				{
					const char *fName = pFi[szCurPos]->name;
					if (itemIsFileName(item, fName) != 0)
						pFi[szCurPos]->userData = item->propsCount + 1;
				}
				szCurPos = szStartPos;
				itemFree(item);
				continue;
			}

			tagfileNextStartItemString(tf);
		}
		if (tf->lastError == ErrorEOF)
			res = EXIT_SUCCESS;

		unsigned int i;
		for (i = 0; i < fiCnt; ++i)
		{
			if (res == EXIT_SUCCESS)
			{
				if (pFi[i]->userData != 0)
					fprintf(stdout, "[%2i] %s\n", pFi[i]->userData - 1, pFi[i]->path);
				else
					fprintf(stdout, "[--] %s\n", pFi[i]->path);
			}
		}
		fileitemsFree(fil);
	}
	tagfileFree(tf);
	return res;
}

FILE *tagfileAddFile(FILE *fdSou, struct FileItemList *fil, char *addPropStr, char *setPropStr)
{
	if (fileitemsCalculateHashes(fil) != EXIT_SUCCESS)
		return NULL;

	fileitemsSort(fil, SortBySizeHash);

	fseek(fdSou, 0L, SEEK_SET);
	struct TagFileStruct *tf = tagfileInitFd(fdSou);
	if (tf != NULL)
	{
		FILE *fdTmp = tmpfile();
		if (fdTmp != NULL)
		{
			unsigned int fiIdx = 0;
			unsigned int fCount = fil->filesCount;
			struct FileItem **pFi = fil->fileItems;
			struct FileItem *fi = pFi[fiIdx];
			char *buff = tf->readBuffer;
			while (tf->lastError == ErrorNone)
			{
				if (fiIdx >= fCount || buff[0] != '[')
				{
write_string_to_tmp_file:
					if (fputs(buff, fdTmp) == EOF || fputc('\n', fdTmp) == EOF)
					{
						perror("tempfile");
						goto free_resources;
					}
					tagfileReadString(tf);
					continue;
				}

				size_t size;
				char hash[41];
				if (tagfileScanStartItemString(buff, &size, hash) != EXIT_SUCCESS)
					goto free_resources;
				int hashCmpRes = -1;
				if (size < fi->size || (size == fi->size && (hashCmpRes = strcmp(hash, fi->hash)) < 0))
					goto write_string_to_tmp_file;

				struct ItemStruct *item;
				if (hashCmpRes == 0)
				{
					item = tagfileItemLoad(tf);
					if (item == NULL)
						goto free_resources;
					if (itemSetPropertiesRaw(item, setPropStr) != EXIT_SUCCESS)
					{
						fprintf(stderr, "itemSetPropertiesRaw error\n");
						goto free_resources;
					}
					if (itemAddPropertiesRaw(item, addPropStr) != EXIT_SUCCESS)
					{
						fprintf(stderr, "itemAddPropertiesRaw error\n");
						goto free_resources;
					}
				}
				else
				{
					item = itemInitFromRawData(fi->size, fi->hash, fi->name, addPropStr, setPropStr);
					if (item == NULL)
					{
						fprintf(stderr, "itemInitFromRawData error\n");
						goto free_resources;
					}
					++fiIdx;
				}

				for ( ; fiIdx < fCount; ++fiIdx)
				{
					fi = pFi[fiIdx];
					if (fi->size != item->fileSize || strcmp(fi->hash, item->hash) != 0)
						break;
					if (itemAddFileName(item, fi->name) != EXIT_SUCCESS)
					{
						fprintf(stderr, "itemAddFileName error\n");
						itemFree(item);
						goto free_resources;
					}
				}
				int tmpRes = tagfileItemOutput(fdTmp, item);
				itemFree(item);
				if (tmpRes != EXIT_SUCCESS)
					goto free_resources;
				fputc('\n', fdTmp);
			}

			int res = EXIT_FAILURE;
			if (tf->lastError == ErrorNone || tf->lastError == ErrorEOF)
			{
				res = EXIT_SUCCESS;
				if (fiIdx < fCount)
				{
					struct ItemStruct *item = NULL;
					for ( ; ; ++fiIdx)
					{
						struct FileItem *fi = NULL;
						if (fiIdx < fCount)
							fi = pFi[fiIdx];

						if (item != NULL)
						{
							if (fi != NULL && fi->size == item->fileSize && strcmp(fi->hash, item->hash) == 0)
							{
								if ((res = itemAddFileName(item, fi->name)) == EXIT_SUCCESS)
									continue;
								itemFree(item);
								fprintf(stderr, "itemAddFileName error\n");
								break;
							}
							res = tagfileItemOutput(fdTmp, item);
							itemFree(item);
							if (res != EXIT_SUCCESS)
								break;
							fputc('\n', fdTmp);
							if (fi == NULL)
								break;
						}
						item = itemInitFromRawData(fi->size, fi->hash, fi->name, addPropStr, setPropStr);
						if (item == NULL)
						{
							res = ErrorInternal;
							fprintf(stderr, "itemInitFromRawData error\n");
							break;
						}
					}
				}
			}
			if (res == EXIT_SUCCESS)
			{
				tagfileFree(tf);
				return fdTmp;
			}
free_resources:
			fclose(fdTmp);
		}
		else
			perror("tempfile");
		tagfileFree(tf);
	}
	return NULL;
}

int tagfileUpdateFileInfo(char **filesArray, int filesCount, char *addPropStr, char *delPropStr, char *setPropStr, const char *whrPropStr)
{
	int nameOffset = fileBaseNameOffset(filesArray, filesCount);
	if (nameOffset < 0)
		return EXIT_FAILURE;

	char sdir[PATH_MAX];
	if (nameOffset > 0)
		strncpy(&sdir[0], filesArray[0], nameOffset);
	sdir[nameOffset] = '\0';
	struct TagFileStruct *tf = tagfileInit(sdir, NULL, ReadOnly);
	if (tf == NULL)
		return EXIT_FAILURE;
	if (tf->lastError != ErrorNone)
	{
		tagfileFree(tf);
		return EXIT_FAILURE;
	}

	struct WhereStruct *whr = NULL;
	if (whrPropStr != NULL)
	{
		whr = whereInit(whrPropStr);
		if (whr == NULL)
		{
			fprintf(stderr, "Error: incorrect where conditions\n");
			tagfileFree(tf);
			return EXIT_FAILURE;
		}
	}

	int res = EXIT_FAILURE;
	struct FileItemList *fil = fileitemsInitFromList(filesArray, filesCount, nameOffset, MaskFile);
	if (fil != NULL)
	{
		unsigned int fCount = fil->filesCount;
		if (fCount != 0)
		{
			fileitemsSort(fil, SortBySize);
			struct FileItem **pFi = fil->fileItems;

			FILE *fdTmp = tmpfile();
			if (fdTmp != NULL)
			{
				short int removeMode = 0;
				if (addPropStr == NULL && setPropStr == NULL && strcmp(delPropStr, "-") == 0)
					++removeMode;
				unsigned int updatedCnt = 0;
				unsigned int removedCnt = 0;
				unsigned int fiStartIdx = 0;
				char *buff = tf->readBuffer;
				while (tf->lastError == ErrorNone)
				{
					if (fiStartIdx == fCount || buff[0] != '[')
					{
write_string_to_tmp_file:
						if (fputs(buff, fdTmp) == EOF || fputc('\n', fdTmp) == EOF)
						{
							perror("tempfile");
							goto free_resources;
						}
						tagfileReadString(tf);
						continue;
					}

					size_t size;
					if (tagfileScanStartItemString(buff, &size, NULL) != EXIT_SUCCESS)
					{
						fprintf(stderr, "Error: file %s, line %i - invalid format\n", tf->filePath, tf->curLineNum);
						goto free_resources;
					}

					struct FileItem *fi = pFi[fiStartIdx];
					if (fi == NULL || size > fi->size)
					{
						for (++fiStartIdx; ; ++fiStartIdx)
						{
							if (fiStartIdx == fCount)
								goto write_string_to_tmp_file;
							fi = pFi[fiStartIdx];
							if (fi != NULL)
							{
								if (size < fi->size)
									goto write_string_to_tmp_file;
								if (size == fi->size)
									break;
							}
						}
					}
					else if (size < fi->size)
						goto write_string_to_tmp_file;

					// size == fi->size
					struct ItemStruct *item = tagfileItemLoad(tf);
					if (item == NULL)
						goto free_resources;
					short int flt = -1;
					short int fnd = 0;
					unsigned int i;
					for (i = fiStartIdx; i < fCount; ++i)
					{
						fi = pFi[i];
						if (fi != NULL)
						{
							if (size != fi->size)
								break;
							if (flt == -1)
							{
								flt = 0;
								if (whr != NULL)
									flt = whereIsFiltered(whr, item);
							}
							if (itemIsFileName(item, fi->name) != 0)
							{
								if (!flt)
								{
									if (!removeMode)
									{
										if (!fnd)
										{
											int tmpRes = EXIT_SUCCESS;
											if (tmpRes == EXIT_SUCCESS && setPropStr != NULL)
												if ((tmpRes = itemSetPropertiesRaw(item, setPropStr)) != EXIT_SUCCESS)
													fprintf(stderr, "Error: itemSetPropertyRaw failed in %s\n", setPropStr);
											if (addPropStr != NULL)
												if ((tmpRes = itemAddPropertiesRaw(item, addPropStr)) != EXIT_SUCCESS)
													fprintf(stderr, "Error: itemAddPropertyRaw failed in %s\n", addPropStr);
											if (tmpRes == EXIT_SUCCESS && delPropStr != NULL)
												if ((tmpRes = itemDelPropertiesRaw(item, delPropStr)) != EXIT_SUCCESS)
													fprintf(stderr, "Error: itemDelPropertyRaw failed in %s\n", delPropStr);
											if (tmpRes != EXIT_SUCCESS)
											{
												itemFree(item);
												goto free_resources;
											}
											++fnd;
										}
										++updatedCnt;
									}
									else
									{
										itemRemoveFileName(item, fi->name);
										++removedCnt;
									}
								}
								fileitemsRemoveItem(fil, pFi + i, MaskFile);
							}
						}
					}
					int tmpRes = EXIT_SUCCESS;
					if (!removeMode || item->fileNameCount != 0)
					{
						tmpRes = tagfileItemOutput(fdTmp, item);
						fputc('\n', fdTmp);
					}
					itemFree(item);
					if (tmpRes != EXIT_SUCCESS)
						goto free_resources;
				}
				if (tf->lastError == ErrorEOF || tf->lastError == ErrorNone)
					res = EXIT_SUCCESS;
free_resources:
				if (res == EXIT_SUCCESS)
				{
					unsigned int addCnt = fil->filesCount;
					if (addCnt != 0)
					{
						if (addPropStr == NULL && setPropStr == NULL)
							addCnt = 0;
						else if (!removeMode && whrPropStr == NULL)
						{
							res = EXIT_FAILURE;
							FILE *fdTmp2 = tagfileAddFile(fdTmp, fil, addPropStr, setPropStr);
							if (fdTmp2 != NULL)
							{
								res = EXIT_SUCCESS;
								fclose(fdTmp);
								fdTmp = fdTmp2;
							}
						}
					}
					if (res == EXIT_SUCCESS)
						res = tagfileApplyIndexFile(tf, fdTmp);
					if (res == EXIT_SUCCESS)
					{
						if (addCnt != 0)
							fprintf(stdout, "Information was added for %i files\n", addCnt);
						if (updatedCnt != 0)
							fprintf(stdout, "Information was updated for %i files\n", updatedCnt);
						if (removedCnt != 0)
							fprintf(stdout, "Information was removed for %i files\n", removedCnt);
					}
				}
				fclose(fdTmp);
			}
			else
				perror("tempfile");
		}
		else
			fprintf(stderr, "Error: no files to process\n");
		fileitemsFree(fil);
	}
	tagfileFree(tf);
	return res;
}

int tagfileListStart(const char *fieldsStr, const char *whrPropStr)
{
	struct FieldListStruct *fields = NULL;
	if (fieldsStr == NULL)
	{
		fields = fieldsInit("@FileName");
		if (fields == NULL)
		{
			fprintf(stderr, "fieldsInit error\n");
			return EXIT_FAILURE;
		}
	}
	else if (strcmp(fieldsStr, "-") != 0)
	{
		fields = fieldsInit(fieldsStr);
		if (fields == NULL)
		{
			fprintf(stderr, "Error: incorrect list of fields\n");
			return EXIT_FAILURE;
		}
	}

	struct WhereStruct *whr = NULL;
	if (whrPropStr != NULL)
	{
		if ((whr = whereInit(whrPropStr)) == NULL)
		{
			if (fields != NULL)
				fieldsFree(fields);
			fprintf(stderr, "Error: incorrect where conditions\n");
			return EXIT_FAILURE;
		}
	}

	int res = EXIT_FAILURE;
	struct TagFileStruct *tf = tagfileInit(NULL, NULL, ReadOnly);
	if (tf != NULL)
	{
		res = tagfileList(tf, fields, whr);
		tagfileFree(tf);
	}
	else
		fprintf(stderr, "Error: tagfileListStart failed\n");

	if (whr != NULL)
		whereFree(whr);
	if (fields != NULL)
		fieldsFree(fields);
	return res;
}

int tagfileShowPropsStart(const char *path, struct ItemStruct *item)
{
	int res = EXIT_FAILURE;
	if (item == NULL)
	{
		res = EXIT_FAILURE;
		struct ItemStruct *item = itemInit("", 0);
		if (item == NULL)
		{
			fprintf(stderr, "Error: itemInit failed\n");
			return EXIT_FAILURE;
		}
		res = tagfileShowPropsStart(path, item);
		if (res == EXIT_SUCCESS)
		{
			unsigned int i;
			unsigned int propsCount = item->propsCount;
			for (i = 0; i < propsCount; ++i)
			{
				struct PropertyStruct **pp = itemGetPropArrayAddrByNum(item, i);
				struct PropertyStruct *prop = *pp;
				const char *propNm = propGetName(prop);
				fprintf(stdout, "%s\t%i\n", propNm, prop->userData + 1);
				unsigned int subCount = prop->valCount;
				struct SubvalHandle **ps = propGetValueIndex(prop, ByUser);
				unsigned int k;
				for (k = 0; k < subCount; ++k)
				{
					const struct SubvalHandle *subval = ps[k];
					fprintf(stdout, "  %s=%s\t%i\n", propNm, subvalString(subval), subval->userData + 1);
				}
			}
		}
		itemFree(item);
		return res;
	}

	struct TagFileStruct *tf = tagfileInit(path, NULL, ReadOnly);
	if (tf != NULL)
	{
		res = tagfileShowProps(tf, item);
	}
	tagfileFree(tf);

	return res;
}

/******************************* Private ******************************/

int tagfileScanStartItemString(const char *str, size_t *sz, char *hash)
{
	if (str[0] != '[')
		return EXIT_FAILURE;

	int len = strlen(str);
	if (len < 44 || str[--len] != ']')
		return EXIT_FAILURE;

	char *sep = strchr(str, ':');
	if (sep == NULL || sep - str < 2)
		return EXIT_FAILURE;

	int c = 0;
	const char *p = &str[1];
	do
	{
		if (isdigit(*p) == 0)
			return EXIT_FAILURE;
		++c;
	} while (++p != sep);

	if (c > 20)
		return EXIT_FAILURE;
	if (strlen(&sep[1]) != 41) // 40 + ']'
		return EXIT_FAILURE;

	if (sz != NULL)
	{
		char buff[21];
		strncpy(buff, &str[1], c);
		buff[c] = '\0';
		*sz = (size_t)atoll(buff);
	}

	if (hash != NULL)
	{
		strncpy(hash, &sep[1], 40);
		hash[40] = '\0';
	}

	return EXIT_SUCCESS;
}

int tagfileItemOutput(FILE *fd, const struct ItemStruct *item)
{
	if (fprintf(fd, "[%zu:%s]\n", item->fileSize, item->hash) >= 0)
	{
		unsigned int i = 0;
		char *fName;
		while ((fName = itemGetFileName(item, i++)) != NULL)
		{
			if (fprintf(fd, "!FileName=%s\n", fName) < 0)
			{
				perror("tagfile");
				return EXIT_FAILURE;
			}
		}
		unsigned int cnt = item->propsCount;
		if (cnt > 0)
		{
			unsigned int i;
			char tempValueBuff[2048];
			for (i = 0; i < cnt; ++i)
			{
				if (itemPropertyValueToString(item, i, tempValueBuff, sizeof(tempValueBuff)) != EXIT_SUCCESS)
				{
					fprintf(stderr, "Error: itemPropertyValueToString failed\n");
					return EXIT_FAILURE;
				}
				if (fprintf(fd, "%s=%s\n", itemPropertyGetName(item, i), tempValueBuff) < 0)
				{
					perror("tagfile");
					return EXIT_FAILURE;
				}
			}
		}
		return EXIT_SUCCESS;
	}
	else
		perror("tagfile");

	return EXIT_FAILURE;
}

int tagfileList(struct TagFileStruct *tf, struct FieldListStruct *fields, const struct WhereStruct *whr)
{
	int res = EXIT_SUCCESS;
	if (tf->lastError == ErrorNone)
	{
		struct ItemStruct *item = NULL;
		while ((item = tagfileGetNextItem(tf)) != NULL)
		{
			int fltr = (whr == NULL) ? 0 : whereIsFiltered(whr, item);
			if (!fltr)
			{
				if (fields != NULL)
				{
					if (fieldsPrintRow(fields, item, tf->dirPath, stdout) != EXIT_SUCCESS)
					{
						itemFree(item);
						return EXIT_FAILURE;
					}
				}
				else
				{
					if (tagfileItemOutput(stdout, item) != EXIT_SUCCESS)
					{
						itemFree(item);
						return EXIT_FAILURE;
					}
				}
			}
			itemFree(item);
		}
		if (tf->lastError != ErrorEOF && tf->lastError != ErrorNone)
			res = EXIT_FAILURE;
	}
	tagfileClose(tf);

	if ((flags & RecurFlag) != 0)
	{
		DirEntry dir;
		int cnt = dirList(tf->dirPath, "*", &dir);
		if (cnt == -1)
			return EXIT_FAILURE;
		int i;
		for (i = 0; i < cnt; ++i)
		{
			struct dirent64 *de = dir[i];
			if (res == EXIT_SUCCESS && de->d_type == DT_DIR)
			{
				struct TagFileStruct *tf_ = tagfileCloneForSubdir(tf, de->d_name);
				if (tf_ == NULL)
					res = EXIT_FAILURE;
				else
				{
					if (tf_->lastError != ErrorNone && tf_->lastError != ErrorNotFound)
						res = EXIT_FAILURE;
					else
						res = tagfileList(tf_, fields, whr);
					tagfileFree(tf_);
				}
			}
			free(de);
		}
		free(dir);
	}

	return res;
}

int tagfileShowProps(struct TagFileStruct *tf, struct ItemStruct *item)
{
	if (tf->lastError == ErrorNone)
	{
		struct ItemStruct *item2;
		while ((item2 = tagfileGetNextItem(tf)) != NULL)
		{
			itemClearFileNames(item2);
			if (itemMerge(item, item2) != EXIT_SUCCESS)
			{
				fprintf(stderr, "Error: itemMerge failed\n");
				itemFree(item2);
				return EXIT_FAILURE;
			}
			if (tf->lastError == ErrorEOF)
				break;
		}
		if (tf->lastError != ErrorEOF && tf->lastError != ErrorNone)
			return EXIT_FAILURE;
	}
	tagfileClose(tf);

	int res = EXIT_SUCCESS;
	if ((flags & RecurFlag) != 0)
	{
		DirEntry dir;
		int cnt = dirList(tf->dirPath, "*", &dir);
		int i;
		for (i = 0; i < cnt; ++i)
		{
			struct dirent64 *de = dir[i];
			if (res == EXIT_SUCCESS && de->d_type == DT_DIR)
			{
				struct TagFileStruct *tf_ = tagfileCloneForSubdir(tf, de->d_name);
				if (tf_ == NULL)
					res = EXIT_FAILURE;
				else
				{
					if (tf_->lastError != ErrorNone && tf_->lastError != ErrorNotFound)
						res = EXIT_FAILURE;
					else
						res = tagfileShowProps(tf_, item);
					tagfileFree(tf_);
				}
			}
			free(de);
		}
		free(dir);
	}

	return res;
}

struct TagFileStruct *tagfileInit(const char* dPath, const char* fName, enum TagFileMode mode)
{
	struct TagFileStruct *tf = tagfileInitStruct(TypeNormal, mode);
	if (tf != NULL)
	{
		enum ErrorId res = tagfileInitPath(tf, dPath, fName);
		if (res == ErrorNone)
		{
			res = tagfileOpen(tf);
			if (res == ErrorNone)
				res = tagfileReadHeader(tf);
		}
		if (res != ErrorNone && res != ErrorNotFound)
		{
			tagfileFree(tf);
			tf = NULL;
		}
	}
	else
		fprintf(stderr, "Error: tagfileInit failed");

	return tf;
}

struct TagFileStruct *tagfileInitFd(FILE *fd)
{
	struct TagFileStruct *tf = tagfileInitStruct(TypeFd, ReadWrite);
	if (tagfileInitReadBuffer(tf) == ErrorNone)
	{
		if (tagfileInitPath(tf, NULL, "tempfile") == ErrorNone)
		{
			tf->fd = fd;
			tagfileReadHeader(tf);
			return tf;
		}
	}
	tagfileFree(tf);
	return NULL;
}

struct TagFileStruct *tagfileInitStruct(enum TagFileType type, enum TagFileMode mode)
{
	struct TagFileStruct *tf = malloc(sizeof(struct TagFileStruct));
	if (tf != NULL)
	{
		tf->type = type;
		tf->fd = NULL;
		tf->dirPath = NULL;
		tf->dirLen = 0;
		tf->filePath = NULL;
		tf->fileName = NULL;
		tf->filePath = NULL;
		tf->fileName = NULL;
		tf->mode = mode;
		tf->lastError  = ErrorNone;
		tf->curLineNum = 0;
		tf->readBuffer = NULL;
	}
	return tf;
}

enum ErrorId tagfileInitPath(struct TagFileStruct *tf, const char *dPath, const char *fName)
{
	unsigned int dirLen = 0;
	unsigned int slashLen = 0;
	if (dPath != NULL && dPath[0] != '\0')
	{
		dirLen = strlen(dPath);
		if (dPath[dirLen-1] != '/')
			++slashLen;
	}

	const char *fn = fName;
	if (fn == NULL)
		fn = tagFileName;
	unsigned int fileLen = strlen(fn);

	enum ErrorId res = ErrorOther;
	if (dirLen + slashLen + fileLen < PATH_MAX)
	{
		res = ErrorNone;
		if (tf->type == TypeNormal)
		{
			tf->dirPath = malloc(PATH_MAX);
			if (tf->dirPath != NULL)
			{
				if (dirLen != 0)
				{
					strcpy(tf->dirPath, dPath);
					if (slashLen != 0)
					{
						tf->dirPath[dirLen++] = '/';
						tf->dirPath[dirLen] = '\0';
					}
				}
				else
					tf->dirPath[0] = '\0';
			}
			else
				res = ErrorInternal;
		}
		tf->dirLen = dirLen;

		if (res == ErrorNone)
		{
			res = ErrorInternal;
			tf->filePath = malloc(PATH_MAX);
			if (tf->filePath != NULL)
			{
				if (dirLen != 0)
					strcpy(tf->filePath, tf->dirPath);
				tf->fileName = tf->filePath + dirLen;
				strcpy(tf->fileName, fn);
				res = ErrorNone;
			}
		}
	}
	tf->lastError = res;
	return res;
}

enum ErrorId tagfileInitReadBuffer(struct TagFileStruct *tf)
{
	if ((tf->readBuffer = malloc(READ_BUFFER_SIZE * sizeof(char))) == NULL)
	{
		tf->lastError = ErrorInternal;
		return ErrorInternal;
	}
	tf->readBuffer[0] = '\0';
	tf->lastError = ErrorNone;
	return ErrorNone;
}

struct TagFileStruct *tagfileCloneForSubdir(const struct TagFileStruct *tf, const char *subdir)
{
	struct TagFileStruct *tfRes = tagfileInitStruct(TypeNormal, tf->mode);
	if (tfRes != NULL)
	{
		if (tagfileInitPath(tfRes, tf->dirPath, tf->fileName) != ErrorNone)
		{
			tagfileFree(tfRes);
			return NULL;
		}

		unsigned int dirLen   = tfRes->dirLen;
		unsigned int fileLen  = strlen(tfRes->fileName);
		unsigned int subLen   = strlen(subdir);
		unsigned int slashLen = 0;
		if (subdir[subLen-1] != '/')
			++slashLen;
		if (dirLen + subLen + slashLen + fileLen >= PATH_MAX)
		{
			tagfileFree(tfRes);
			return NULL;
		}

		strcpy(tfRes->dirPath + dirLen, subdir);
		dirLen += subLen;
		if (slashLen != 0)
		{
			tfRes->dirPath[dirLen++] = '/';
			tfRes->dirPath[dirLen]   = '\0';
		}
		tfRes->dirLen = dirLen;

		strcpy(tfRes->filePath, tfRes->dirPath);
		tfRes->fileName = tfRes->filePath + dirLen;
		strcpy(tfRes->fileName, tf->fileName);

		if (tagfileOpen(tfRes) == ErrorNone)
			tagfileReadHeader(tfRes);
	}
	return tfRes;
}

void tagfileFree(struct TagFileStruct *tf)
{
	tagfileClose(tf);
	if (tf->dirPath != NULL)
		free(tf->dirPath);
	if (tf->filePath != NULL)
		free(tf->filePath);
	free(tf);
}

enum ErrorId tagfileOpen(struct TagFileStruct *tf)
{
	tf->curLineNum = 0;
	tf->eof = EofNo;

	char *sMode = "r";
	if (tf->mode == ReadWrite)
		sMode = "r+";
	tf->fd = fopen(tf->filePath, sMode);

	if (tf->fd == NULL)
	{
		if (errno == ENOENT)
		{
			tf->lastError = ErrorNotFound;
			fputs("Index file not found\n", stderr);
			return ErrorNotFound;
		}
		tf->lastError = ErrorOther;
		perror("index file");
		return ErrorOther;
	}

	if (tagfileInitReadBuffer(tf) != ErrorNone)
	{
		fclose(tf->fd);
		tf->fd = NULL;
		return tf->lastError;
	}

	return tf->lastError;
}

enum ErrorId tagfileReadHeader(struct TagFileStruct *tf)
{
	if (tagfileReadString(tf) == ErrorNone)
	{
		if (strcmp(tf->readBuffer, "!tags-info") != 0)
		{
			fprintf(stderr, "Error: file %s, line %i - invalid format\n", tf->filePath, tf->curLineNum);
			tf->lastError = ErrorInvalidIndex;
		}
	}
	return tf->lastError;
}

void tagfileClose(struct TagFileStruct *tf)
{
	if (tf->fd != NULL && tf->type == TypeNormal)
	{
		fclose(tf->fd);
		tf->fd = NULL;
	}
	if (tf->readBuffer != NULL)
	{
		free(tf->readBuffer);
		tf->readBuffer = NULL;
	}
}

int tagfileApplyIndexFile(struct TagFileStruct *tf, FILE *fd)
{
	unsigned int pathLen = strlen(tf->filePath);
	if (pathLen + 4 + 1 > PATH_MAX)
		return EXIT_FAILURE;

	char tmpName[PATH_MAX];
	strcpy(tmpName, tf->filePath);
	strcpy(&tmpName[pathLen], ".tmp");
	{
		FILE *fdTmp = fopen(&tmpName[0], "w");
		if (fdTmp == NULL)
		{
			perror("tempfile");
			return EXIT_FAILURE;
		}
		int res = EXIT_FAILURE;
		fseek(fd, 0L, SEEK_SET);
		char buff[4096];
		while (1)
		{
			size_t cntRead = fread(buff, sizeof(char), sizeof(buff), fd);
			if (ferror(fd))
				break;
			if (cntRead != 0)
			{
				size_t cntWrite = fwrite(buff, 1, cntRead, fdTmp);
				if (cntRead != cntWrite || ferror(fdTmp))
					break;
			}
			if (feof(fd))
			{
				res = EXIT_SUCCESS;
				fflush(fdTmp);
				break;
			}
		}
		fclose(fdTmp);
		if (res != EXIT_SUCCESS)
		{
			perror("tempfile");
			return EXIT_FAILURE;
		}
	}

	char bakName[PATH_MAX];
	strcpy(bakName, tf->filePath);
	strcpy(&bakName[pathLen], ".bak");
	if (rename(tf->filePath, bakName) == 0)
	{
		sync();
		if (rename(tmpName, tf->filePath) == 0)
		{
			sync();
			unlink(bakName);
			return EXIT_SUCCESS;
		}
		else
			perror("indexfile");
	}
	else
		perror("bakfile");
	unlink(tmpName);
	return EXIT_FAILURE;
}

enum ErrorId tagfileReadString(struct TagFileStruct *tf)
{
	++tf->curLineNum;
	if (fgets(tf->readBuffer, READ_BUFFER_SIZE, tf->fd) != NULL)
	{
		char *buff = tf->readBuffer;
		unsigned int len = strlen(buff);
		if (len != 0 && buff[len-1] == '\n')
			buff[len-1] = '\0';

		tf->lastError = ErrorNone;
		return ErrorNone;
	}

	enum ErrorId res = ErrorOther;
	if (feof(tf->fd) != 0)
	{
		res = ErrorEOF;
		tf->eof = EofYes;
	}
	else
		perror("tagfile");
	tf->lastError = res;
	return res;
}

enum ErrorId tagfileNextStartItemString(struct TagFileStruct *tf)
{
	char *buff = tf->readBuffer;
	while (tagfileReadString(tf) == ErrorNone)
		if (buff[0] == '[')
			break;
	return tf->lastError;
}

enum ErrorId tagfileItemBodyLoad(struct TagFileStruct *tf, struct ItemStruct *item)
{
	char *buff = tf->readBuffer;
	while (tagfileReadString(tf) == ErrorNone)
	{
		if (buff[0] == '[')
		{
			tf->lastError = ErrorNone;
			return ErrorNone;
		}

		if (buff[0] != '\0' && buff[0] != '#')
		{

			char *valPos = strchr(buff, '=');
			if (valPos == NULL || valPos == buff)
			{
				fprintf(stderr, "Error: file %s, line %i - invalid format\n", tf->filePath, tf->curLineNum);
				tf->lastError = ErrorInvalidIndex;
				return ErrorInvalidIndex;
			}

			*valPos++ = '\0';
			char *namePos = buff;
			if (namePos[0] == '!')
			{
				++namePos;
				if (strcmp(namePos, "FileName") == 0 && strlen(valPos) != 0)
				{
					if (itemAddFileName(item, valPos) != EXIT_SUCCESS)
					{
						fprintf(stderr, "Error: tagsItemAddFileName failed\n");
						tf->lastError = ErrorInternal;
						return ErrorInternal;
					}
				}
				else
				{
					fprintf(stderr, "Error: file %s, line %i - invalid format\n", tf->filePath, tf->curLineNum);
					tf->lastError = ErrorInvalidIndex;
					return ErrorInvalidIndex;
				}
			}
			else
			{
				if (itemSetProperty(item, namePos, valPos) != EXIT_SUCCESS)
				{
					tf->lastError = ErrorInternal;
					fprintf(stderr, "ItemSetProperty error\n");
					return EXIT_FAILURE;
				}
			}
		}
	}
	return tf->lastError;
}

struct ItemStruct *tagfileItemLoad(struct TagFileStruct *tf)
{
	size_t sz;
	char hash[41];
	if (tagfileScanStartItemString(tf->readBuffer, &sz, hash) != EXIT_SUCCESS)
	{
		tf->lastError = ErrorInvalidIndex;
		fprintf(stderr, "Error: line %i - invalid format\n", tf->curLineNum);
		return NULL;
	}

	struct ItemStruct *item = itemInit(hash, sz);
	if (item == NULL)
	{
		tf->lastError = ErrorInvalidIndex;
		fprintf(stderr, "Error: tagsItemInit failed\n");
		return NULL;
	}

	if (tagfileItemBodyLoad(tf, item) == ErrorNone || tf->lastError == ErrorEOF)
		return item;

	itemFree(item);
	return NULL;
}

struct ItemStruct *tagfileGetNextItem(struct TagFileStruct *tf)
{
	char *buff = tf->readBuffer;
	tf->lastError = ErrorOther;
	while (buff[0] != '[')
	{
		if (tagfileReadString(tf) != ErrorNone)
			return NULL;
	}

	return tagfileItemLoad(tf);
}

struct ItemStruct *tagfileItemsScanBySize(struct TagFileStruct *tf, size_t sz)
{
	char *buff = tf->readBuffer;
	do
	{
		if (buff[0] == '[')
		{
			size_t sz_;
			if (tagfileScanStartItemString(buff, &sz_, NULL) != EXIT_SUCCESS)
			{
				fprintf(stderr, "Error: file %s, line %i - invalid format\n", tf->filePath, tf->curLineNum);
				tf->lastError = ErrorInvalidIndex;
				return NULL;
			}
			if (sz == sz_)
			{
				return tagfileItemLoad(tf);
			}
			else if (sz > sz_)
			{
				fprintf(stderr, "Error: file is not present in the index\n");
				return NULL;
			}
		}
	} while (tagfileReadString(tf) == ErrorNone);
	return NULL;
}

struct ItemStruct *tagfileGetItemForFileName(struct TagFileStruct *tf, const char *fileName)
{
	size_t sz;
	enum ErrorId err = fileInfo(fileName, &sz, NULL, 0);
	if (err == ErrorNone)
	{
		for ( ; ; )
		{
			struct ItemStruct *item = tagfileItemsScanBySize(tf, sz);
			if (item == NULL)
				break;

			if (itemIsFileName(item, fileName) != 0)
				return item;

			itemFree(item);
		}
	}
	else
		fileInfoError(fileName, err);
	return NULL;
}
