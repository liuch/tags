/*
 * main.c
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
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <wchar.h>

#include "common.h"
#include "tags.h"
#include "utils.h"

#define VERSION_STRING "0.0.1"

enum WarnMode { WarnNone, WarnOptions, WarnFiles, WarnOther };

enum {
	MoveFileOption = CHAR_MAX + 1
};

struct option long_options[] = {
	{ "append-value", required_argument, NULL, 'a' },
	{ "create-index", no_argument,       NULL, 'c' },
	{ "remove-value", required_argument, NULL, 'd' },
	{ "fields-list",  required_argument, NULL, 'f' },
	{ "help",         no_argument,       NULL, 'h' },
	{ "file-info",    no_argument,       NULL, 'i' },
	{ "file-list",    no_argument,       NULL, 'l' },
	{ "value-list",   no_argument,       NULL, 'p' },
	{ "recursive",    no_argument,       NULL, 'r' },
	{ "set-value",    required_argument, NULL, 's' },
	{ "version",      no_argument,       NULL, 'v' },
	{ "where",        required_argument, NULL, 'w' },
	{ "move-file",    no_argument,       NULL, MoveFileOption },
	{ NULL,           0,                 NULL, 0   }
};

void showHelp();
void showVersion();
void showWarning(enum WarnMode mode);
void freeResources(void);

wchar_t *addOptArg  = NULL;
wchar_t *delOptArg  = NULL;
wchar_t *setOptArg  = NULL;
wchar_t *whrOptArg  = NULL;
wchar_t *fieldsList = NULL;

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "");
	if (argc == 1)
	{
		showWarning(WarnOptions);
		return EXIT_FAILURE;
	}

	int res = EXIT_SUCCESS;
	int opt, oi = -1;
	flags = NoneFlag;
	while (res == EXIT_SUCCESS && (opt = getopt_long(argc, argv, "a:cd:f:hilprs:vw:", long_options, &oi)) != -1)
	{
		switch (opt)
		{
			case 'a':
				addOptArg = makeWideCharString(optarg, 0);
				break;
			case 'c':
				flags |= InitFlag;
				break;
			case 'd':
				delOptArg = makeWideCharString(optarg, 0);
				break;
			case 'f':
				fieldsList = makeWideCharString(optarg, 0);
				break;
			case 'h':
				showHelp();
				return EXIT_SUCCESS;
			case 'i':
				flags |= InfoFlag;
				break;
			case 'l':
				flags |= ListFlag;
				break;
			case 'p':
				flags |= PropFlag;
				break;
			case 'r':
				flags |= RecurFlag;
				break;
			case 's':
				setOptArg = makeWideCharString(optarg, 0);
				break;
			case 'v':
				flags |= VersionFlag;
				break;
			case 'w':
				whrOptArg = makeWideCharString(optarg, 0);
				break;
			case MoveFileOption:
				flags |= MoveFileFlag;
				break;
			default:
				showWarning(WarnOther);
				res = EXIT_FAILURE;
				break;
		}
	}
	if (res != EXIT_SUCCESS)
		return res;

	res = EXIT_FAILURE;
	enum WarnMode warn = WarnOptions;
	if (addOptArg == NULL && delOptArg == NULL && setOptArg == NULL)
	{
		int filesCnt = argc - optind;
		if (flags == InitFlag) // -c option
		{
			if (filesCnt == 0 && whrOptArg == NULL && fieldsList == NULL)
			{
				res = tagsCreateIndex();
				warn = WarnNone;
			}
		}
		else if (flags == InfoFlag) // -i option
		{
			if (filesCnt > 0)
			{
				if (whrOptArg == NULL && fieldsList == NULL)
				{
					res = tagsStatus(&argv[optind], filesCnt);
					warn = WarnNone;
				}
			}
			else
				warn = WarnFiles;
		}
		else if ((flags & ListFlag) != 0) // -l option
		{
			if ((flags & ~(ListFlag | RecurFlag)) == 0 && filesCnt == 0)
			{
				res = tagsList(fieldsList, whrOptArg);
				warn = WarnNone;
			}
		}
		else if ((flags & PropFlag) != 0) // -p option
		{
			if ((flags & ~(PropFlag | RecurFlag)) == 0 && filesCnt == 0 && whrOptArg == NULL && fieldsList == NULL)
			{
				res = tagsShowProps();
				warn = WarnNone;
			}
		}
		else if (flags == VersionFlag) // -v option
		{
			if (filesCnt == 0 && whrOptArg == NULL && fieldsList == NULL)
			{
				showVersion();
				res = EXIT_SUCCESS;
				warn = WarnNone;
			}
		}
		else if (flags == MoveFileFlag) // --rename-file option
		{
			if (filesCnt == 2 && whrOptArg == NULL && fieldsList == NULL)
			{
				res = moveFile(&argv[optind]);
				warn = WarnNone;
			}
		}
	}
	else  // -a -d -s options
	{
		if (flags == NoneFlag)
		{
			warn = WarnFiles;
			int filesCnt = argc - optind;
			if (filesCnt > 0)
			{
				res = tagsUpdateFileInfo(&argv[optind], filesCnt, addOptArg, delOptArg, setOptArg, whrOptArg);
				warn = WarnNone;
			}
		}
	}

	freeResources();
	if (warn != WarnNone)
		showWarning(warn);
	return res;
}

void showHelp()
{
	fputs("Usage: tags <keys> [<files>]\n"
		"Sets and displays the properties of a file, that stored in a special index file in the same folder.\n"
		"The program does not modify any files except its index file!\n"
		"\nKeys:\n"
		"  -a, --append-value APPEND_LIST\n"
		"          adds the value of the parameters for the specified files\n"
		"  -c, --create-index\n"
		"          creates empty index file in the current directory and exit.\n"
		"          The index file is created only if it is not present\n"
		"  -d, --remove-value DELETE_LIST\n"
		"          removes information about the specified files, their parameters or\n"
		"          the individual values of parameters from the index\n"
		"  -f, --fields-list FIELDS_LIST\n"
		"          fields list for -l key\n"
		"  -h, --help\n"
		"          show this help\n"
		"  -i, --file-info\n"
		"          output short information about the specified files\n"
		"  -l, --file-list\n"
		"          displays fields separated by tabs, one line for each file in the index.\n"
		"          List of fields you specify in the -f key.\n"
		"          If the -f key is not specified, displayed a list of files\n"
		"  -p, --value-list\n"
		"          output summary information about a properties\n"
		"  -r, --recursive\n"
		"          the recursive flag. Can be used with -l and -p keys\n"
		"  -s, --set-value SET_LIST\n"
		"          sets the parameters and their values for the specified files\n"
		"  -v, --version\n"
		"          output version information and exit\n"
		"  -w, --where WHERE_LIST\n"
		"          list of a conditions. Use with -a, -s, -d and -l keys\n"
		"  --move-file OLD_FILE_NAME NEW_FILE_NAME\n"
		"          change a file name within an index or transfer data to another index.\n"
		"          OLD_FILE_NAME and NEW_FILE_NAME can contain the path to the index file\n"
		"  Note: when using the -a, -d, -i and -s keys, you must specify one or more files\n"
		"  Note: keys -a, -d, and -s can be used simultaneously\n"
		"\nAPPEND_LIST, DELETE_LIST, SET_LIST specification:\n"
		"  param_name=[subval,subval,...][@param_name=...]\n"
		"  <param_name> can contain letters, numbers, and other characters. But it can not contain a '='.\n"
		"  The characters after the '=' is a list of values separated by commas.\n"
		"\nFIELDS_LIST specification:\n"
		"  field[,field...]\n"
		"  <field> this is the name of the property or one of the special properties.\n"
		"  If instead of a list of fields specify '-', will display the internal structure of the index\n"
		"\nWHERE_LIST specification:\n"
		"  see SET_LIST specification\n"
		"  'param_name=value1,value2' equivalently 'param_name' contains 'value1' OR 'value2' or both.\n"
		"  'param_name=value1@param_name=value2' equivalently 'param_name' contains 'value1' AND 'value2'.\n"
		"  'param_name=' means that the parameter is missing or empty\n"
		"  'param_name'  means that the parameter is defined, including the empty\n"
		"\nSpecial properties:\n"
		"  '@FileSize' - file size\n"
		"  '@FileName' - file name\n"
		"  Note: this is parameters is read only\n"
		"\nExamples:\n"
		"  tags -c\n"
		"    Creates empty index file in the current directory\n"
		"  tags -s type=photo@format=jpg *.jpg\n"
		"    Assigns a parameter of 'type' value 'photo' and a parameter of 'format' value 'jpg' for\n"
		"    all files with the extension jpg. If the file is not in the index, it is added.\n"
		"  tags -a tag=pets -w tag=dog,cat@type=photo *\n"
		"    Adds to the property 'tag' value 'pet', for files that have this property is the value of 'dog' or 'cat'.\n"
		"  tags -d - temp.jpg\n"
		"    Deletes all data about the file temp.jpg from the index. The file is not deleted.\n"
		"  tags -l -f @FileName,@FileSize,tag -w tag=pets\n"
		"    Displays file name, file size and property 'tag' for files that have property to 'tag' is the value of 'pets'.\n"
		, stdout);
}

void showVersion()
{
	fputs("Tags " VERSION_STRING "\n", stdout);
}

void showWarning(enum WarnMode mode)
{
	switch (mode)
	{
		case WarnOptions:
			fputs("Invalid combination of options\n", stderr);
			break;
		case WarnFiles:
			fputs("List of files is not specified\n", stderr);
			break;
		default:
			;
	}
	fputs("Run `tags -h` for get help.\n", stderr);
}

void freeResources(void)
{
	if (addOptArg != NULL)
		free(addOptArg);
	if (delOptArg != NULL)
		free(delOptArg);
	if (setOptArg != NULL)
		free(setOptArg);
	if (whrOptArg != NULL)
		free(whrOptArg);
	if (fieldsList != NULL)
		free(fieldsList);
}
