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

#define READ_BUFFER_SIZE 4096

const char tagFileName[] = "tags.info";

int tagfileScanItemHeader(const char *str, size_t *pSz, char **pHash);
enum ErrorId tagfileWritingTail(struct TagFileStruct *tf);
int tagfileItemOutput(FILE *fd, const struct ItemStruct *item);
FILE *tagfileGetReadFd(const struct TagFileStruct *tf);
FILE *tagfileGetWriteFd(const struct TagFileStruct *tf);
struct TagFileStruct *tagfileInitStruct(enum TagFileMode mode);
enum ErrorId tagfileInitPath(struct TagFileStruct *tf, const char *dPath, const char *fName);
enum ErrorId tagfileInitReadBuffer(struct TagFileStruct *tf);
struct TagFileStruct *tagfileCloneForSubdir(const struct TagFileStruct *tf, const char *subdir);
enum ErrorId tagfileOpen(struct TagFileStruct *tf);
enum ErrorId tagfileReadHeader(struct TagFileStruct *tf);
void tagfileClose(struct TagFileStruct *tf);
enum ErrorId tagfileReadString(struct TagFileStruct *tf);
enum ErrorId tagfileWriteString(struct TagFileStruct *tf, const char *str);
enum ErrorId tagfileItemBodyLoad(struct TagFileStruct *tf, struct ItemStruct *item);
struct ItemStruct *tagfileGetNextItem(struct TagFileStruct *tf);


int tagfileCreateIndex()
{
	int res = EXIT_FAILURE;
	if (access(tagFileName, F_OK) == -1 && errno == ENOENT)
	{
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
	}
	else
		fprintf(stderr, "Error: file %s already exists\n", tagFileName);

	return res;
}

struct TagFileStruct *tagfileInit(const char* dPath, const char* fName, enum TagFileMode mode)
{
	struct TagFileStruct *tf = tagfileInitStruct(mode);
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

void tagfileFree(struct TagFileStruct *tf)
{
	tagfileClose(tf);
	if (tf->dirPath != NULL)
		free(tf->dirPath);
	if (tf->filePath != NULL)
		free(tf->filePath);
	free(tf);
}

int tagfileFindNextItemPosition(struct TagFileStruct *tf, size_t sz)
{
	if (feof(tagfileGetReadFd(tf)))
	{
		tf->lastError = ErrorEOF;
		return 0;
	}

	char *buff = tf->readBuffer;
	short fWrite = 0;
	if (tf->mode == ReadWrite)
		++fWrite;

	if (tf->curLineNum == 0 || tf->findFlag)
	{
		if (fWrite && tf->curLineNum != 0)
			if (tagfileWriteString(tf, buff) != ErrorNone)
				return 0;
		if (tagfileReadString(tf) != ErrorNone)
			return 0;
	}

	do
	{
		if (buff[0] == '[')
		{
			if (tagfileScanItemHeader(buff, &tf->curItemSize, &tf->curItemHash) != EXIT_SUCCESS)
			{
				tf->lastError = ErrorInvalidIndex;
				return 0;
			}
			if (sz == 0 || sz == tf->curItemSize)
			{
				tf->findFlag = 1;
				return 1;
			}
			if (sz < tf->curItemSize)
				return 0;
		}

		if (fWrite && tagfileWriteString(tf, buff) != ErrorNone)
			break;
	} while (tagfileReadString(tf) == ErrorNone);

	return 0;
}

struct ItemStruct *tagfileItemLoad(struct TagFileStruct *tf)
{
	struct ItemStruct *item = itemInit(tf->curItemHash, tf->curItemSize);
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
	int res = EXIT_SUCCESS;

	if (tf->lastError == ErrorNone)
	{
		struct ItemStruct *item2;
		while ((item2 = tagfileGetNextItem(tf)) != NULL)
		{
			itemClearFileNames(item2);
			if (itemMerge(item, item2) != EXIT_SUCCESS)
			{
				itemFree(item2);
				fputs("Error: itemMerge failed\n", stderr);
				return EXIT_FAILURE;
			}
			if (tf->lastError == ErrorEOF)
				break;
		}
		if (tf->lastError != ErrorEOF && tf->lastError != ErrorNone)
			return EXIT_FAILURE;
	}
	tagfileClose(tf);

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
				if (tf_ != NULL)
				{
					if (tf_->lastError != ErrorNone && tf_->lastError != ErrorNotFound)
						res = EXIT_FAILURE;
					else
						res = tagfileShowProps(tf_, item);
					tagfileFree(tf_);
				}
				else
					res = EXIT_FAILURE;
			}
			free(de);
		}
		free(dir);
	}

	return res;
}

enum ErrorId tagfileSetAppendMode(struct TagFileStruct *tf)
{
	if (tagfileWritingTail(tf) == ErrorNone)
	{
		if (fseek(tf->fdModif, 0L, SEEK_SET) == 0 && (tf->fdInsert = tmpfile()) != NULL)
		{
			tf->curLineNum = 0;
			tf->lastError = ErrorNone;
		}
		else
		{
			tf->lastError = ErrorOther;
			perror("tmpfile");
		}
	}
	return tf->lastError;
}

enum ErrorId tagfileInsertItem(struct TagFileStruct *tf, const struct ItemStruct *item)
{
	enum ErrorId res = ErrorOther;

	FILE *fdWrite = tagfileGetWriteFd(tf);
	if (tagfileItemOutput(fdWrite, item) == EXIT_SUCCESS)
		if (fputc('\n', fdWrite) != EOF)
			res = ErrorNone;

	tf->lastError = res;
	return res;
}

enum ErrorId tagfileApplyModifications(struct TagFileStruct *tf)
{
	if (tagfileWritingTail(tf) != ErrorNone)
		return tf->lastError;

	enum ErrorId res = ErrorOther;
	FILE *fd = tagfileGetWriteFd(tf);
	unsigned int pathLen = strlen(tf->filePath);
	if (pathLen + 4 + 1 <= PATH_MAX)
	{
		char tmpName[PATH_MAX];
		strcpy(tmpName, tf->filePath);
		strcpy(&tmpName[pathLen], ".tmp");
		FILE *fdTmp = fopen(&tmpName[0], "w");
		if (fdTmp != NULL)
		{
			fseek(fd, 0L, SEEK_SET);
			char buff[4096];
			while (1)
			{
				size_t cntRead = fread(buff, 1, sizeof(buff) * sizeof(char), fd);
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
					res = ErrorNone;
					fflush(fdTmp);
					break;
				}
			}
			if (fclose(fdTmp) == EOF)
				res = ErrorOther;

			if (res == ErrorNone)
			{
				tagfileClose(tf);
				res = ErrorOther;
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
						res = ErrorNone;
					}
					else
						perror("indexfile");
				}
				else
					perror("bakfile");
			}
			else
				perror("tmpfile");
			if (res != ErrorNone)
				unlink(tmpName);
		}
		else
			perror("tmpfile");
	}
	tf->lastError = res;
	return res;
}

/******************************* Private ******************************/

int tagfileScanItemHeader(const char *str, size_t *pSz, char **pHash)
{
	if (str[0] != '[')
		return EXIT_FAILURE;

	int len = strlen(str);
	if (len < FILE_HASH_LEN + 4 || str[--len] != ']')
		return EXIT_FAILURE;

	char *sep = strchr(str, ':');
	if (sep == NULL || sep - str < 2)
		return EXIT_FAILURE;

	int c = 0;
	const char *p = str + 1;
	do
	{
		if (isdigit(*p) == 0)
			return EXIT_FAILURE;
		++c;
	} while (++p != sep);

	if (c > 20)
		return EXIT_FAILURE;
	if (strlen(sep + 1) != FILE_HASH_LEN + 1) // 40 + ']'
		return EXIT_FAILURE;

	char buff[21];
	strncpy(buff, str + 1, c);
	buff[c] = '\0';
	*pSz = (size_t)atoll(buff);

	*pHash = sep + 1;

	return EXIT_SUCCESS;
}

enum ErrorId tagfileWritingTail(struct TagFileStruct *tf)
{
	FILE *fdRead = tagfileGetReadFd(tf);
	if (!feof(fdRead))
	{
		const char *buff = tf->readBuffer;
		do
		{
			if (tf->curLineNum != 0)
				tagfileWriteString(tf, buff);
		} while (tagfileReadString(tf) == ErrorNone);
	}

	if (tf->lastError == ErrorEOF)
		tf->lastError = ErrorNone;
	return tf->lastError;
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

struct TagFileStruct *tagfileInitStruct(enum TagFileMode mode)
{
	struct TagFileStruct *tf = malloc(sizeof(struct TagFileStruct));
	if (tf != NULL)
	{
		tf->fd          = NULL;
		tf->dirPath     = NULL;
		tf->dirLen      = 0;
		tf->filePath    = NULL;
		tf->fileName    = NULL;
		tf->filePath    = NULL;
		tf->fileName    = NULL;
		tf->mode        = mode;
		tf->lastError   = ErrorNone;
		tf->curLineNum  = 0;
		tf->readBuffer  = NULL;
		tf->curItemSize = 0;
		tf->curItemHash = NULL;
		tf->findFlag    = 0;
		tf->fdModif     = NULL;
		tf->fdInsert    = NULL;
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
	struct TagFileStruct *tfRes = tagfileInitStruct(tf->mode);
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

enum ErrorId tagfileOpen(struct TagFileStruct *tf)
{
	tf->curLineNum = 0;
	tf->eof        = EofNo;

	tf->fd = fopen(tf->filePath, "r");
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

	if (tf->mode == ReadWrite)
	{
		tf->fdModif = tmpfile();
		if (tf->fdModif == NULL)
		{
			tf->lastError = ErrorOther;
			perror("tmpfile");
		}
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
	if (tf->fd != NULL)
	{
		fclose(tf->fd);
		tf->fd = NULL;
	}
	if (tf->readBuffer != NULL)
	{
		free(tf->readBuffer);
		tf->readBuffer = NULL;
	}
	if (tf->fdModif != NULL)
	{
		fclose(tf->fdModif);
		tf->fdModif = NULL;
	}
	if (tf->fdInsert != NULL)
	{
		fclose(tf->fdInsert);
		tf->fdInsert = NULL;
	}
}

enum ErrorId tagfileReadString(struct TagFileStruct *tf)
{
	FILE *fdRead = tagfileGetReadFd(tf);
	++tf->curLineNum;
	tf->curItemSize = 0;
	tf->curItemHash = NULL;
	tf->findFlag    = 0;
	char *buff      = tf->readBuffer;
	if (fgets(buff, READ_BUFFER_SIZE, fdRead) != NULL)
	{
		unsigned int len = strlen(buff);
		if (len != 0 && buff[len-1] == '\n')
			buff[len-1] = '\0';

		tf->lastError = ErrorNone;
		return ErrorNone;
	}

	enum ErrorId res = ErrorOther;
	if (feof(fdRead) != 0)
	{
		res = ErrorEOF;
		tf->eof = EofYes;
	}
	else
		perror("tagfile");
	tf->lastError = res;
	return res;
}

enum ErrorId tagfileWriteString(struct TagFileStruct *tf, const char *str)
{
	enum ErrorId res = ErrorNone;
	FILE *fdWrite = tagfileGetWriteFd(tf);
	if (fputs(str, fdWrite) == EOF || fputs("\n", fdWrite) == EOF)
	{
		res = ErrorOther;
		perror("tmpfile");
	}
	tf->lastError = res;
	return res;
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

struct ItemStruct *tagfileGetNextItem(struct TagFileStruct *tf)
{
	if (!tagfileFindNextItemPosition(tf, 0))
		return NULL;

	return tagfileItemLoad(tf);
}

FILE *tagfileGetReadFd(const struct TagFileStruct *tf)
{
	FILE *fd = tf->fd;
	if (tf->fdInsert != NULL)
		fd = tf->fdModif;
	return fd;
}

FILE *tagfileGetWriteFd(const struct TagFileStruct *tf)
{
	FILE *fd = tf->fdInsert;
	if (fd == NULL)
		fd = tf->fdModif;
	return fd;
}
