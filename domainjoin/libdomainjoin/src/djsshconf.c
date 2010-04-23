/* Editor Settings: expandtabs and use 4 spaces for indentation
 * ex: set softtabstop=4 tabstop=8 expandtab shiftwidth=4: *
 * -*- mode: c, c-basic-offset: 4 -*- */

/*
 * Copyright Likewise Software    2004-2008
 * All rights reserved.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the license, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.  You should have received a copy
 * of the GNU Lesser General Public License along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 *
 * LIKEWISE SOFTWARE MAKES THIS SOFTWARE AVAILABLE UNDER OTHER LICENSING
 * TERMS AS WELL.  IF YOU HAVE ENTERED INTO A SEPARATE LICENSE AGREEMENT
 * WITH LIKEWISE SOFTWARE, THEN YOU MAY ELECT TO USE THE SOFTWARE UNDER THE
 * TERMS OF THAT SOFTWARE LICENSE AGREEMENT INSTEAD OF THE TERMS OF THE GNU
 * LESSER GENERAL PUBLIC LICENSE, NOTWITHSTANDING THE ABOVE NOTICE.  IF YOU
 * HAVE QUESTIONS, OR WISH TO REQUEST A COPY OF THE ALTERNATE LICENSING
 * TERMS OFFERED BY LIKEWISE SOFTWARE, PLEASE CONTACT LIKEWISE SOFTWARE AT
 * license@likewisesoftware.com
 */

#include "domainjoin.h"
#include "ctarray.h"
#include "djstr.h"
#include "djsshconf.h"

struct SshLine
{
    char *leadingWhiteSpace;
    CTParseToken name;
    /* trailing separator includes comment */
    CTParseToken value;
};

struct SshConf
{
    char *filename;
    struct SshLine *lines;
    int lineCount;
    DynamicArray private_data;
    int modified;
};

static int FindOption(struct SshConf *conf, int startLine, const char *name);

static CENTERROR SetOption(struct SshConf *conf, const char *name, const char *value);

static CENTERROR WriteSshConfiguration(const char *rootPrefix, struct SshConf *conf);

static void UpdatePublicLines(struct SshConf *conf)
{
    conf->lines = conf->private_data.data;
    conf->lineCount = conf->private_data.size;
}

static CENTERROR GetLineStrings(char ***dest, struct SshLine *line, int *destSize)
{
    if(*destSize < 5)
        return CENTERROR_OUT_OF_MEMORY;
    dest[0] = &line->leadingWhiteSpace;
    dest[1] = &line->name.value;
    dest[2] = &line->name.trailingSeparator;
    dest[3] = &line->value.value;
    dest[4] = &line->value.trailingSeparator;
    *destSize = 5;
    return CENTERROR_SUCCESS;
}

static void FreeSshLineContents(struct SshLine *line)
{
    char **strings[5];
    int stringCount = 5;
    int i;

    GetLineStrings(strings, line, &stringCount);
    for(i = 0; i < stringCount; i++)
    {
        CT_SAFE_FREE_STRING(*strings[i]);
    }
}

static int FindOption(struct SshConf *conf, int startLine, const char *name)
{
    int i;
    if(startLine == -1)
        return -1;
    for(i = startLine; i < conf->lineCount; i++)
    {
        if(conf->lines[i].name.value != NULL &&
                !strcmp(conf->lines[i].name.value, name))
        {
            return i;
        }
    }
    return -1;
}

/* Get the printed form of a line from the parsed form by concatenating all of the strings together */
static CENTERROR GetPrintedLine(DynamicArray *dest, struct SshConf *conf, int line)
{
    CENTERROR ceError = CENTERROR_SUCCESS;
    size_t len = 0;
    char **strings[5];
    size_t stringLengths[5];
    size_t pos;
    int stringCount = 5;
    int i;

    GetLineStrings(strings, &conf->lines[line], &stringCount);
    for(i = 0; i < stringCount; i++)
    {
        stringLengths[i] = strlen(*strings[i]);
        len += stringLengths[i];
    }
    //For the terminating NULL
    len++;

    if(len > dest->capacity)
        BAIL_ON_CENTERIS_ERROR(ceError = CTSetCapacity(dest, 1, len));
    pos = 0;
    for(i = 0; i < stringCount; i++)
    {
        memcpy((char *)dest->data + pos, *strings[i], stringLengths[i]);
        pos += stringLengths[i];
    }
    ((char *)dest->data)[pos] = '\0';
    dest->size = len;

error:
    return ceError;
}

static CENTERROR RemoveLine(struct SshConf *conf, int *line)
{
    CENTERROR ceError = CENTERROR_SUCCESS;
    BAIL_ON_CENTERIS_ERROR(ceError = CTArrayRemove(&conf->private_data, *line, sizeof(struct SshLine), 1));
    UpdatePublicLines(conf);
    conf->modified = 1;

    if(*line >= conf->lineCount)
        *line = -1;

error:
    return ceError;
}

static CENTERROR RemoveOption(struct SshConf *conf, const char *name)
{
    CENTERROR ceError = CENTERROR_SUCCESS;
    int line;

    for(line = 0; line < conf->lineCount; line++)
    {
        line = FindOption(conf, line, name);
        if(line == -1)
            break;

        BAIL_ON_CENTERIS_ERROR(ceError = RemoveLine(conf, &line));
        if(line > 0)
            line--;
    }

error:
    UpdatePublicLines(conf);
    return ceError;
}

/* Copy a ssh configuration line and add it below the old line. */
static CENTERROR SetOption(struct SshConf *conf, const char *name, const char *value)
{
    CENTERROR ceError = CENTERROR_SUCCESS;
    int line = -1;
    DynamicArray printedLine;
    struct SshLine lineObj;
    int found = 0;

    memset(&lineObj, 0, sizeof(struct SshLine));
    memset(&printedLine, 0, sizeof(printedLine));

    for(line = 0; line < conf->lineCount; line++)
    {
        line = FindOption(conf, line, name);
        if(line == -1)
            break;
        found++;
        if(!strcmp(conf->lines[line].value.value, value))
            continue;

        //Insert a commented out version of the line
        BAIL_ON_CENTERIS_ERROR(ceError = GetPrintedLine(&printedLine, conf, line));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup("",
            &lineObj.leadingWhiteSpace));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup("",
            &lineObj.name.value));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup("",
            &lineObj.name.trailingSeparator));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup("",
            &lineObj.value.value));
        CTAllocateStringPrintf(&lineObj.value.trailingSeparator, "#Overwritten by lwidentity: %s",
                printedLine.data);
        BAIL_ON_CENTERIS_ERROR(ceError = CTArrayInsert(&conf->private_data,
                    line, sizeof(struct SshLine), &lineObj, 1));
        memset(&lineObj, 0, sizeof(lineObj));
        UpdatePublicLines(conf);
        conf->modified = 1;
        line++;

        //Change the option value of the line
        CT_SAFE_FREE_STRING(conf->lines[line].value.value);
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup(value,
            &conf->lines[line].value.value));
    }

    /*If the option wasn't already in the file, search for comments that
      mention the option, and insert the line after the comment*/
    for(line = 0; !found && line < conf->lineCount; line++)
    {
        if(strstr(conf->lines[line].value.trailingSeparator, name) == NULL)
            continue;

        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup("",
            &lineObj.leadingWhiteSpace));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup(name,
            &lineObj.name.value));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup(" ",
            &lineObj.name.trailingSeparator));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup(value,
            &lineObj.value.value));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup("",
            &lineObj.value.trailingSeparator));
        BAIL_ON_CENTERIS_ERROR(ceError = CTArrayInsert(&conf->private_data,
                    line + 1, sizeof(struct SshLine), &lineObj, 1));
        memset(&lineObj, 0, sizeof(lineObj));
        conf->modified = 1;
        found++;
    }

    /*If the option wasn't even in a comment, just add the option at the
      end of the file
      */
    if(!found)
    {
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup("",
            &lineObj.leadingWhiteSpace));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup(name,
            &lineObj.name.value));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup(" ",
            &lineObj.name.trailingSeparator));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup(value,
            &lineObj.value.value));
        BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup("",
            &lineObj.value.trailingSeparator));
        BAIL_ON_CENTERIS_ERROR(ceError = CTArrayAppend(&conf->private_data,
                    sizeof(struct SshLine), &lineObj, 1));
        memset(&lineObj, 0, sizeof(lineObj));
        conf->modified = 1;
    }

error:
    UpdatePublicLines(conf);
    FreeSshLineContents(&lineObj);
    CTArrayFree(&printedLine);
    return ceError;
}

static CENTERROR AddFormattedLine(struct SshConf *conf, const char *filename, const char *linestr, const char **endptr)
{
    CENTERROR ceError = CENTERROR_SUCCESS;
    struct SshLine lineObj;
    const char *pos = linestr;
    const char *token_start = NULL;

    memset(&lineObj, 0, sizeof(lineObj));

    /* Find the leading whitespace in the line */
    token_start = pos;
    while(isblank(*pos)) pos++;
    BAIL_ON_CENTERIS_ERROR(ceError = CTStrndup(token_start, pos - token_start, &lineObj.leadingWhiteSpace));

    /* Find the option name in the line */
    token_start = pos;
    while(!isspace(*pos) && *pos != '\0' && *pos != '#')
    {
        pos++;
    }
    BAIL_ON_CENTERIS_ERROR(ceError = CTStrndup(token_start, pos - token_start, &lineObj.name.value));

    /* Find the blank space separating the option name and option value */
    token_start = pos;
    while(isblank(*pos)) pos++;
    BAIL_ON_CENTERIS_ERROR(ceError = CTStrndup(token_start, pos - token_start, &lineObj.name.trailingSeparator));

    /* Find the option value */
    token_start = pos;
    //This token can contain spaces and tabs
    while(*pos != '\0' && *pos != '#' && *pos != '\n' && *pos != '\r') pos++;
    //But not at the end, so trim off the trailing white space
    while(pos > token_start && isblank(pos[-1])) pos--;
    BAIL_ON_CENTERIS_ERROR(ceError = CTStrndup(token_start, pos - token_start, &lineObj.value.value));

    /* Find the line's trailing whitespace and comment */
    token_start = pos;
    while(*pos != '\0' && *pos != '\n' && *pos != '\r') pos++;
    BAIL_ON_CENTERIS_ERROR(ceError = CTStrndup(token_start, pos - token_start, &lineObj.value.trailingSeparator));

    BAIL_ON_CENTERIS_ERROR(ceError = CTArrayAppend(&conf->private_data, sizeof(lineObj), &lineObj, 1));
    memset(&lineObj, 0, sizeof(lineObj));
    UpdatePublicLines(conf);

    if(endptr != NULL)
        *endptr = pos;
    conf->modified = 1;

error:
    FreeSshLineContents(&lineObj);
    return ceError;
}

static void FreeSshConfContents(struct SshConf *conf)
{
    int i;
    for(i = 0; i < conf->lineCount; i++)
    {
        FreeSshLineContents(&conf->lines[i]);
    }
    CTArrayFree(&conf->private_data);
    UpdatePublicLines(conf);
    CT_SAFE_FREE_STRING(conf->filename);
}

static CENTERROR ReadSshFile(struct SshConf *conf, const char *rootPrefix, const char *filename)
{
    CENTERROR ceError = CENTERROR_SUCCESS;
    FILE *file = NULL;
    PSTR buffer = NULL;
    char *fullPath = NULL;
    BOOLEAN endOfFile = FALSE;
    BOOLEAN exists;

    BAIL_ON_CENTERIS_ERROR(ceError = CTAllocateStringPrintf(
            &fullPath, "%s%s", rootPrefix, filename));
    DJ_LOG_INFO("Reading ssh file %s", fullPath);
    BAIL_ON_CENTERIS_ERROR(ceError = CTCheckFileOrLinkExists(fullPath, &exists));
    if(!exists)
    {
        DJ_LOG_INFO("File %s does not exist", fullPath);
        ceError = CENTERROR_INVALID_FILENAME;
        goto error;
    }

    BAIL_ON_CENTERIS_ERROR(ceError = CTStrdup(filename,
        &conf->filename));
    BAIL_ON_CENTERIS_ERROR(ceError = CTOpenFile(fullPath, "r", &file));
    CT_SAFE_FREE_STRING(fullPath);
    while(TRUE)
    {
        CT_SAFE_FREE_STRING(buffer);
        BAIL_ON_CENTERIS_ERROR(ceError = CTReadNextLine(file, &buffer, &endOfFile));
        if(endOfFile)
            break;
        BAIL_ON_CENTERIS_ERROR(ceError = AddFormattedLine(conf, filename, buffer, NULL));
    }
    BAIL_ON_CENTERIS_ERROR(ceError = CTCloseFile(file));
    file = NULL;

    return ceError;

error:
    if(file != NULL)
        CTCloseFile(file);
    CT_SAFE_FREE_STRING(fullPath);
    FreeSshConfContents(conf);
    CT_SAFE_FREE_STRING(buffer);
    return ceError;
}

static CENTERROR WriteSshConfiguration(const char *rootPrefix, struct SshConf *conf)
{
    CENTERROR ceError = CENTERROR_SUCCESS;
    DynamicArray printedLine;
    int i;
    char *tempName = NULL;
    char *finalName = NULL;
    char *prefixedPath = NULL;
    FILE *file = NULL;
    memset(&printedLine, 0, sizeof(printedLine));

    DJ_LOG_INFO("Writing ssh configuration for %s", conf->filename);

    BAIL_ON_CENTERIS_ERROR(ceError = CTAllocateStringPrintf(&prefixedPath, "%s%s", rootPrefix, conf->filename));

    ceError = CTGetFileTempPath(
                        prefixedPath,
                        &finalName,
                        &tempName);
    BAIL_ON_CENTERIS_ERROR(ceError);

    ceError = CTOpenFile(tempName, "w", &file);
    if(!CENTERROR_IS_OK(ceError))
    {
        DJ_LOG_ERROR("Unable to open '%s' for writing", tempName);
        BAIL_ON_CENTERIS_ERROR(ceError);
    }

    for(i = 0; i < conf->lineCount; i++)
    {
        BAIL_ON_CENTERIS_ERROR(ceError = GetPrintedLine(&printedLine, conf, i));
        BAIL_ON_CENTERIS_ERROR(ceError = CTFilePrintf(file, "%s\n", printedLine.data));
    }

    BAIL_ON_CENTERIS_ERROR(ceError = CTCloseFile(file));
    file = NULL;
    BAIL_ON_CENTERIS_ERROR(ceError = CTSafeReplaceFile(finalName, tempName));
    DJ_LOG_INFO("File moved into place");

error:
    if(file != NULL)
        CTCloseFile(file);
    CTArrayFree(&printedLine);
    CT_SAFE_FREE_STRING(prefixedPath);
    CT_SAFE_FREE_STRING(tempName);
    CT_SAFE_FREE_STRING(finalName);
    return ceError;
}

BOOLEAN FindSshAndConfig(PCSTR rootPrefix, PCSTR sshOrSshd,
        PSTR *sshBinary, PSTR *sshConfig, LWException **exc)
{
    /* Mac OS X stores the configuration in /etc */
    /* CSW ssh on Solaris uses /opt/csw/etc/ssh */
    const char *sshConfigPath = "/etc/ssh:/opt/ssh/etc:/usr/local/etc:/etc:/etc/openssh:/usr/openssh/etc:/opt/csw/etc/ssh";

    /* Solaris Sparc 10 stores sshd in /usr/lib/ssh */
    /* Experian says their sshd is at /usr/openssh/sbin/sshd */
    /* CSW ssh on Solaris uses /opt/csw/sbin/sshd */
    const char *sshBinaryPath = "/usr/sbin:/opt/ssh/sbin:/usr/local/sbin:/usr/bin:/opt/ssh/bin:/usr/local/bin:/usr/lib/ssh:/usr/openssh/sbin:/usr/openssh/bin:/opt/csw/sbin:/opt/csw/bin";

    CENTERROR ceError = CENTERROR_SUCCESS;
    PSTR configFilename = NULL;
    PSTR binaryFilename = NULL;

    *sshBinary = NULL;
    *sshConfig = NULL;

    LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(
        &configFilename, "%s_config", sshOrSshd));
    LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(
        &binaryFilename, "%s", sshOrSshd));

    ceError = CTFindInPath(rootPrefix, configFilename, sshConfigPath, sshConfig);
    if(ceError == CENTERROR_FILE_NOT_FOUND)
        ceError = CENTERROR_SUCCESS;
    else if(ceError == CENTERROR_SUCCESS)
        DJ_LOG_INFO("Found config file %s", *sshConfig);
    LW_CLEANUP_CTERR(exc, ceError);

    ceError = CTFindInPath(rootPrefix, binaryFilename, sshBinaryPath, sshBinary);
    if(ceError == CENTERROR_FILE_NOT_FOUND)
        ceError = CENTERROR_SUCCESS;
    else if(ceError == CENTERROR_SUCCESS)
        DJ_LOG_INFO("Found binary %s", *sshBinary);
    LW_CLEANUP_CTERR(exc, ceError);

    if(*sshConfig != NULL && *sshBinary == NULL)
    {
        LW_RAISE_EX(exc, CENTERROR_INVALID_FILENAME,
                "Unable to find ssh binary",
"A %s config file was found at '%s', which indicates that %s is installed on your system. However the %s binary could not be found in the search path '%s'. In order to configure %s, please either symlink the %s binary into an existing search path, or ask Likewise support to extend the search path."
                , sshOrSshd, *sshConfig, sshOrSshd,
                sshOrSshd, sshBinaryPath, sshOrSshd, sshOrSshd);
        goto cleanup;
    }
    else if(*sshConfig == NULL && *sshBinary != NULL)
    {
        LW_RAISE_EX(exc, CENTERROR_INVALID_FILENAME,
                "Unable to find ssh config",
"A %s binary was found at '%s', which indicates that %s is installed on your system. However the %s config could not be found in the search path '%s'. In order to configure %s, please either symlink the %s config file into an existing search path, or ask Likewise support to extend the search path."
                , sshOrSshd, *sshBinary, sshOrSshd,
                sshOrSshd, sshConfigPath, sshOrSshd, sshOrSshd);
        goto cleanup;
    }

cleanup:
    if((*sshConfig == NULL) != (*sshBinary == NULL))
    {
        CT_SAFE_FREE_STRING(*sshConfig);
        CT_SAFE_FREE_STRING(*sshBinary);
    }
    CT_SAFE_FREE_STRING(configFilename);
    CT_SAFE_FREE_STRING(binaryFilename);
    return *sshConfig != NULL;
}

static BOOLEAN TestOption(PCSTR rootPrefix, struct SshConf *conf, PCSTR binary, PCSTR testFlag, PCSTR optionName, LWException **exc)
{
    CENTERROR ceError = CENTERROR_SUCCESS;
    BOOLEAN result = FALSE;
    PSTR command = NULL;
    PSTR commandOutput = NULL;
    int existingLine;

    if(rootPrefix == NULL)
        rootPrefix = "";
    DJ_LOG_INFO("Testing option %s", optionName);

    existingLine = FindOption(conf, 0, optionName);
    if(existingLine != -1)
    {
        if(!strcmp(conf->lines[existingLine].value.value, "yes"))
        {
            //The option is already enabled, so it must be supported
            result = TRUE;
            goto cleanup;
        }
    }

    /* Most versions of sshd support the -t option which runs it in test
       mode. Test mode is used to verify that a config file is correct, or
       in our case that the passed options are valid.

       The only version of sshd known to not support -t is the version that
       comes with Solaris 9. However, this version does not support the -o
       option, and it will error out if the -o option is used. The Solaris
       9 version of sshd does not support any of the options we'd like to
       enable, so it will correctly fail all of the option tests.

       Sshd will either complain about the first invalid option that is
       passed with -o, or it will complain about all invalid options. -o
       BadOption=yes is passed to verify that sshd understands -o, and to
       make doubly sure that it will not start listening on a port.

       The -v option can be used to test whether ssh (the client) supports
       given options.  Passing -v to ssh will cause it to print out its
       version number and not attempt to connect to a machine. All versions of
       ssh seem to parse the options passed with -o even when -v is passed.

       Ssh will either complain about the first invalid option that is
       passed with -o, or it will complain about all invalid options. -o
       BadOption=yes is passed to verify that ssh understands -o.
     */
    LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(
        &command, "%s%s %s -o %s=yes -o BadOption=yes 2>&1",
        rootPrefix, binary, testFlag, optionName));

    ceError = CTCaptureOutput(command, &commandOutput);
    /* Some versions of sshd will return an error code because an invalid
       option was passed, but not all will. */
    if(ceError == CENTERROR_COMMAND_FAILED)
        ceError = CENTERROR_SUCCESS;
    LW_CLEANUP_CTERR(exc, ceError);

    if(strstr(commandOutput, optionName) != NULL)
    {
        DJ_LOG_INFO("Option %s not supported", optionName);
        goto cleanup;
    }

    if(strstr(commandOutput, "BadOption") == NULL)
    {
        DJ_LOG_INFO("Sshd does not support -o");
        goto cleanup;
    }

    DJ_LOG_INFO("Option %s supported", optionName);
    result = TRUE;

cleanup:
    CT_SAFE_FREE_STRING(command);
    CT_SAFE_FREE_STRING(commandOutput);
    return result;
}

typedef struct
{
    BOOLEAN isOpenSsh;
    // -1 denotes unknown
    long major;
    long minor;
    long secondMinor;
    long patch;
} SshdVersion;

static void GetSshVersion(PCSTR rootPrefix, SshdVersion *version, PCSTR binaryPath, LWException **exc)
{
    CENTERROR ceError = CENTERROR_SUCCESS;
    PSTR command = NULL;
    PSTR commandOutput = NULL;
    PCSTR versionStart;
    PSTR intEnd;

    version->isOpenSsh = FALSE;
    version->major = -1;
    version->minor = -1;
    version->secondMinor = -1;
    version->patch = -1;

    if(rootPrefix == NULL)
        rootPrefix = "";

    LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(
        &command, "%s%s -v 2>&1",
        rootPrefix, binaryPath));

    ceError = CTCaptureOutput(command, &commandOutput);
    if(ceError == CENTERROR_COMMAND_FAILED)
        ceError = CENTERROR_SUCCESS;
    LW_CLEANUP_CTERR(exc, ceError);

    // The version string is in the form OpenSSH_4.6p1
    // or OpenSSH_3.6.1p1
    versionStart = strstr(commandOutput, "OpenSSH_");
    if(versionStart == NULL)
    {
        goto cleanup;
    }
    version->isOpenSsh = TRUE;

    versionStart += strlen("OpenSSH_");
    version->major = strtoul(versionStart, &intEnd, 10);
    if(intEnd == versionStart)
        version->major = -1;
    if(intEnd == NULL || *intEnd != '.')
        goto cleanup;
    versionStart = intEnd + 1;

    version->minor = strtoul(versionStart, &intEnd, 10);
    if(intEnd == versionStart)
        version->minor = -1;
    if(intEnd == NULL || (*intEnd != '.' && *intEnd != 'p'))
        goto cleanup;
    versionStart = intEnd + 1;

    if(*intEnd == '.')
    {
        version->secondMinor = strtoul(versionStart, &intEnd, 10);
        if(intEnd == versionStart)
            version->secondMinor = -1;
        if(intEnd == NULL || *intEnd != 'p')
            goto cleanup;
        versionStart = intEnd + 1;
    }

    version->patch = strtoul(versionStart, &intEnd, 10);
    if(intEnd == versionStart)
        version->patch = -1;

cleanup:

    if(version->isOpenSsh)
    {
        DJ_LOG_INFO("Found open sshd version %d.%d.%dp%d", version->major,
                version->minor, version->secondMinor, version->patch);
    }
    else
    {
        DJ_LOG_INFO("Found non-openssh version of ssh");
    }

    CT_SAFE_FREE_STRING(command);
    CT_SAFE_FREE_STRING(commandOutput);
}

BOOLEAN IsNewerThanOrEq(const SshdVersion *version, int major, int minor, int secondMinor, int patch)
{
    if(!version->isOpenSsh)
        return FALSE;

    if(version->major == -1 || major == -1)
        return TRUE;
    if(version->major < major)
        return FALSE;
    if(version->major > major)
        return TRUE;

    if(version->minor == -1 || minor == -1)
        return TRUE;
    if(version->minor < minor)
        return FALSE;
    if(version->minor > minor)
        return TRUE;

    if(version->secondMinor == -1 || secondMinor == -1)
        return TRUE;
    if(version->secondMinor < secondMinor)
        return FALSE;
    if(version->secondMinor > secondMinor)
        return TRUE;

    if(version->patch == -1 || patch == -1)
        return TRUE;
    if(version->patch < patch)
        return FALSE;
    if(version->patch > patch)
        return TRUE;

    return TRUE;
}
 
BOOLEAN IsOlderThanOrEq(const SshdVersion *version, int major, int minor, int secondMinor, int patch)
{
    if(!version->isOpenSsh)
        return FALSE;

    if(version->major == -1 || major == -1)
        return TRUE;
    if(version->major > major)
        return FALSE;
    if(version->major < major)
        return TRUE;

    if(version->minor == -1 || minor == -1)
        return TRUE;
    if(version->minor > minor)
        return FALSE;
    if(version->minor < minor)
        return TRUE;

    if(version->secondMinor == -1 || secondMinor == -1)
        return TRUE;
    if(version->secondMinor > secondMinor)
        return FALSE;
    if(version->secondMinor < secondMinor)
        return TRUE;

    if(version->patch == -1 || patch == -1)
        return TRUE;
    if(version->patch > patch)
        return FALSE;
    if(version->patch < patch)
        return TRUE;

    return TRUE;
}
                    
static QueryResult UpdateSshdConf(struct SshConf *conf, PCSTR testPrefix,
        PCSTR binaryPath, BOOLEAN enable, PSTR *changeDescription,
        const JoinProcessOptions *options, LWException **exc)
{
    size_t i;
    BOOLEAN modified = conf->modified;
    PSTR requiredOptions = NULL;
    PSTR optionalOptions = NULL;
    BOOLEAN supported;
    PSTR temp = NULL;
    QueryResult result = NotConfigured;
    const char *requiredSshdOptions[] = {
        "ChallengeResponseAuthentication",
        "UsePAM", "PAMAuthenticationViaKBDInt",
        "KbdInteractiveAuthentication", NULL};
    const char *optionalSshdOptions[] = {"GSSAPIAuthentication",
        "GSSAPICleanupCredentials", NULL};
    SshdVersion version;
    BOOLEAN compromisedOptions = FALSE;

    if(changeDescription != NULL)
        *changeDescription = NULL;

    LW_TRY(exc, GetSshVersion("", &version, binaryPath, &LW_EXC));

    if(enable)
    {
        LW_CLEANUP_CTERR(exc, CTStrdup("", &requiredOptions));
        LW_CLEANUP_CTERR(exc, CTStrdup("", &optionalOptions));
        for(i = 0; requiredSshdOptions[i] != NULL; i++)
        {
            PCSTR option = requiredSshdOptions[i];
            LW_TRY(exc, supported = TestOption(testPrefix, conf, binaryPath, "-t", option, &LW_EXC));
            if(supported)
            {
                PCSTR value = "yes";

                if(IsNewerThanOrEq(&version, 2, 3, 1, -1) &&
                        IsOlderThanOrEq(&version, 3, 3, -1, -1) &&
                        (!strcmp(option, "ChallengeResponseAuthentication") ||
                         !strcmp(option, "PAMAuthenticationViaKBDInt")))
                {
                    value = "no";
                    compromisedOptions = TRUE;
                }
                conf->modified = FALSE;
                LW_CLEANUP_CTERR(exc, SetOption(conf, option, value));
                if(conf->modified)
                {
                    modified = TRUE;
                    temp = requiredOptions;
                    requiredOptions = NULL;
                    CTAllocateStringPrintf(&requiredOptions, "%s\t%s\n", temp, option);
                    CT_SAFE_FREE_STRING(temp);
                }
            }
        }
        for(i = 0; optionalSshdOptions[i] != NULL; i++)
        {
            PCSTR option = optionalSshdOptions[i];
            LW_TRY(exc, supported = TestOption(testPrefix, conf, binaryPath, "-t", option, &LW_EXC));
            if(supported)
            {
                conf->modified = FALSE;
                LW_CLEANUP_CTERR(exc, SetOption(conf, option, "yes"));
                if(conf->modified)
                {
                    modified = TRUE;
                    temp = optionalOptions;
                    optionalOptions = NULL;
                    LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(&optionalOptions, "%s\t%s\n", temp, option));
                    CT_SAFE_FREE_STRING(temp);
                }
            }
        }
        result = FullyConfigured;
        if(strlen(optionalOptions) > 0)
        {
            temp = optionalOptions;
            optionalOptions = NULL;
            LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(
                        &optionalOptions,
                        "The following options will be enabled in %s to provide a better user experience:\n%s",
                        conf->filename, temp));
            CT_SAFE_FREE_STRING(temp);
            result = SufficientlyConfigured;
        }
        if(strlen(requiredOptions) > 0)
        {
            temp = requiredOptions;
            requiredOptions = NULL;
            LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(
                        &requiredOptions,
                        "The following options must be enabled in %s:\n%s",
                        conf->filename, temp));
            CT_SAFE_FREE_STRING(temp);
            result = NotConfigured;
        }
        if(changeDescription != NULL)
        {
            LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(
                        changeDescription,
                        "%s%s",
                        requiredOptions, optionalOptions));
        }
    }
    else
    {
        conf->modified = FALSE;
        LW_CLEANUP_CTERR(exc, RemoveOption(conf, "GSSAPIAuthentication"));
        if(conf->modified)
        {
            if(changeDescription != NULL)
            {
                LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(
                            changeDescription,
                            "In %s, GSSAPIAuthentication may optionally be removed.\n",
                            conf->filename));
            }
            result = SufficientlyConfigured;
        }
        else
            result = FullyConfigured;
    }
    if(changeDescription != NULL && *changeDescription == NULL)
        LW_CLEANUP_CTERR(exc, CTStrdup("", changeDescription));

    if(options != NULL)
    {
        ModuleState *state = DJGetModuleStateByName(options, "ssh");
        if(state->moduleData == (void *)-1)
        {
            //We already showed a warning
        }
        else if(compromisedOptions)
        {
            state->moduleData = (void *)-1;
            if (options->warningCallback != NULL)
            {
                options->warningCallback(options, "Unpatched version of SSH",
                        "The version of your sshd daemon indicates that it is susceptible to the remote exploit described at http://www.openssh.com/txt/preauth.adv . To avoid exposing your system to this exploit, the 'ChallengeResponseAuthentication' and 'PAMAuthenticationViaKBDInt' options will be set to 'no' instead of 'yes'. As a side effect, all login error messages will appear as 'permission denied' instead of more detailed messages like 'account disabled'. Additionally, password changes on login may not work.\n\
\n\
Even when those options are disabled, your system is still vulnerable to http://www.cert.org/advisories/CA-2003-24.html . We recommend upgrading your version of SSH.");
            }
        }
        else if(IsOlderThanOrEq(&version, 3, 7, 0, -1))
        {
            state->moduleData = (void *)-1;
            if (options->warningCallback != NULL)
            {
                options->warningCallback(options, "Unpatched version of SSH",
                        "The version of your sshd daemon indicates that it is susceptible to the security vulnerability described at http://www.cert.org/advisories/CA-2003-24.html . We recommend upgrading your version of SSH.");
            }
        }
    }

cleanup:
    conf->modified |= modified;
    CT_SAFE_FREE_STRING(requiredOptions);
    CT_SAFE_FREE_STRING(optionalOptions);
    CT_SAFE_FREE_STRING(temp);
    return result;
}

static QueryResult UpdateSshConf(struct SshConf *conf, PCSTR testPrefix,
        PCSTR binaryPath, BOOLEAN enable, PSTR *changeDescription,
        LWException **exc)
{
    BOOLEAN modified = conf->modified;
    size_t i;
    BOOLEAN supported;
    PSTR optionalOptions = NULL;
    PSTR temp = NULL;
    QueryResult result = NotConfigured;
    const char *optionalSshOptions[] = {"GSSAPIAuthentication",
        "GSSAPIDelegateCredentials", NULL};

    if(changeDescription != NULL)
        *changeDescription = NULL;

    if(enable)
    {
        LW_CLEANUP_CTERR(exc, CTStrdup("", &optionalOptions));
        for(i = 0; optionalSshOptions[i] != NULL; i++)
        {
            PCSTR option = optionalSshOptions[i];
            LW_TRY(exc, supported = TestOption(testPrefix, conf, binaryPath, "-v", option, &LW_EXC));
            if(supported)
            {
                conf->modified = FALSE;
                LW_CLEANUP_CTERR(exc, SetOption(conf, option, "yes"));
                if(conf->modified)
                {
                    modified = TRUE;
                    temp = optionalOptions;
                    optionalOptions = NULL;
                    LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(&optionalOptions, "%s\t%s\n", temp, option));
                    CT_SAFE_FREE_STRING(temp);
                }
            }
        }
        result = FullyConfigured;
        if(strlen(optionalOptions) > 0)
        {
            if(changeDescription != NULL)
            {
                LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(
                            changeDescription,
                            "The following options will be enabled in %s to provide a better user experience:\n%s",
                            conf->filename, optionalOptions));
            }
            result = SufficientlyConfigured;
        }
    }
    else
    {
        conf->modified = FALSE;
        LW_CLEANUP_CTERR(exc, RemoveOption(conf, "GSSAPIAuthentication"));
        if(conf->modified)
        {
            if(changeDescription != NULL)
            {
                LW_CLEANUP_CTERR(exc, CTAllocateStringPrintf(
                            changeDescription,
                            "In %s, GSSAPIAuthentication may optionally be removed.\n",
                            conf->filename));
            }
            result = SufficientlyConfigured;
        }
        else
            result = FullyConfigured;
    }
    if(changeDescription != NULL && *changeDescription == NULL)
        LW_CLEANUP_CTERR(exc, CTStrdup("", changeDescription));

cleanup:
    conf->modified |= modified;
    CT_SAFE_FREE_STRING(optionalOptions);
    CT_SAFE_FREE_STRING(temp);
    return result;
}

void DJConfigureSshForADLogin(
    const char * testPrefix,
    BOOLEAN enable,
    JoinProcessOptions *options,
    LWException **exc
    )
{
    struct SshConf conf;
    char *configPath = NULL;
    char *binaryPath = NULL;
    BOOLEAN exists;
    pid_t sshdPid;

    if(testPrefix == NULL)
        testPrefix = "";
    memset(&conf, 0, sizeof(conf));

    LW_TRY(exc, exists = FindSshAndConfig(testPrefix, "sshd",
        &binaryPath, &configPath, &LW_EXC));

    if(exists)
    {
        LW_CLEANUP_CTERR(exc, ReadSshFile(&conf, testPrefix, configPath));
        LW_TRY(exc, UpdateSshdConf(&conf, testPrefix,
            binaryPath, enable, NULL, options, &LW_EXC));

        if(conf.modified)
        {
            WriteSshConfiguration(testPrefix, &conf);
            LW_CLEANUP_CTERR(exc, CTIsProgramRunning("/var/run/sshd.pid",
                    "sshd", &sshdPid, NULL));
            if(sshdPid != -1)
            {
                DJ_LOG_INFO("Restaring sshd (pid %d)", sshdPid);
                LW_CLEANUP_CTERR(exc, CTSendSignal(sshdPid, SIGHUP));
            }
        }
        else
            DJ_LOG_INFO("sshd_config not modified");
        FreeSshConfContents(&conf);
    }

    CT_SAFE_FREE_STRING(configPath);
    CT_SAFE_FREE_STRING(binaryPath);
    LW_TRY(exc, exists = FindSshAndConfig(testPrefix, "ssh",
        &binaryPath, &configPath, &LW_EXC));
    if(exists)
    {
        LW_CLEANUP_CTERR(exc, ReadSshFile(&conf, testPrefix, configPath));
        LW_TRY(exc, UpdateSshConf(&conf, testPrefix,
            binaryPath, enable, NULL, &LW_EXC));

        if(conf.modified)
            WriteSshConfiguration(testPrefix, &conf);
        else
            DJ_LOG_INFO("ssh_config not modified");
        FreeSshConfContents(&conf);
    }

cleanup:
    FreeSshConfContents(&conf);
    CT_SAFE_FREE_STRING(configPath);
    CT_SAFE_FREE_STRING(binaryPath);
}

static QueryResult QueryDescriptionConfigSsh(const JoinProcessOptions *options,
        PCSTR testPrefix, PSTR *changeDescription, LWException **exc)
{
    struct SshConf conf;
    char *configPath = NULL;
    char *binaryPath = NULL;
    BOOLEAN exists;
    PSTR message = NULL;
    PSTR temp = NULL;
    PSTR subDescription = NULL;
    LWException *caught = NULL;
    QueryResult result1 = FullyConfigured, result2 = FullyConfigured;

    if(testPrefix == NULL)
        testPrefix = "";
    memset(&conf, 0, sizeof(conf));

    LW_CLEANUP_CTERR(exc, CTStrdup("", &message));

    exists = FindSshAndConfig(testPrefix, "sshd",
        &binaryPath, &configPath, &caught);
    if(caught != NULL && caught->code == CENTERROR_INVALID_FILENAME)
    {
        //Show a warning instead of an exception
        temp = message;
        message = NULL;
        CTAllocateStringPrintf(&message, "%s%s\n\n", temp, caught->longMsg);
        CT_SAFE_FREE_STRING(temp);
        LW_HANDLE(&caught);
        exists = FALSE;
        result1 = SufficientlyConfigured;
    }
    LW_CLEANUP(exc, caught);

    if(exists)
    {
        LW_CLEANUP_CTERR(exc, ReadSshFile(&conf, testPrefix, configPath));
        LW_TRY(exc, result1 = UpdateSshdConf(&conf, testPrefix,
            binaryPath, options->joiningDomain, &subDescription, options, &LW_EXC));
        temp = message;
        message = NULL;
        CTAllocateStringPrintf(&message, "%s%s", temp, subDescription);
        CT_SAFE_FREE_STRING(temp);
        CT_SAFE_FREE_STRING(subDescription);

        FreeSshConfContents(&conf);
    }

    exists = FindSshAndConfig(testPrefix, "ssh",
        &binaryPath, &configPath, &caught);
    if(caught != NULL && caught->code == CENTERROR_INVALID_FILENAME)
    {
        //Show a warning instead of an exception
        temp = message;
        message = NULL;
        CTAllocateStringPrintf(&message, "%s%s\n\n", temp, caught->longMsg);
        CT_SAFE_FREE_STRING(temp);
        LW_HANDLE(&caught);
        exists = FALSE;
        result2 = SufficientlyConfigured;
    }
    LW_CLEANUP(exc, caught);

    if(exists)
    {
        LW_CLEANUP_CTERR(exc, ReadSshFile(&conf, testPrefix, configPath));
        LW_TRY(exc, result2 = UpdateSshConf(&conf, testPrefix,
            binaryPath, options->joiningDomain, &subDescription, &LW_EXC));
        temp = message;
        message = NULL;
        CTAllocateStringPrintf(&message, "%s%s", temp, subDescription);
        CT_SAFE_FREE_STRING(temp);
        CT_SAFE_FREE_STRING(subDescription);

        FreeSshConfContents(&conf);
    }
    if(result2 < result1)
        result1 = result2;

    CTStripTrailingWhitespace(message);
    if(strlen(message) == 0)
    {
        CT_SAFE_FREE_STRING(message);
        LW_CLEANUP_CTERR(exc, CTStrdup("Fully configured", &message));
    }
    if(changeDescription != NULL)
    {
        *changeDescription = message;
        message = NULL;
    }

cleanup:
    FreeSshConfContents(&conf);
    CT_SAFE_FREE_STRING(configPath);
    CT_SAFE_FREE_STRING(binaryPath);
    CT_SAFE_FREE_STRING(message);
    CT_SAFE_FREE_STRING(temp);
    CT_SAFE_FREE_STRING(subDescription);
    LW_HANDLE(&caught);
    return result1;
}

static QueryResult QuerySsh(const JoinProcessOptions *options, LWException **exc)
{
    return QueryDescriptionConfigSsh(options, NULL, NULL, exc);
}

static PSTR GetSshDescription(const JoinProcessOptions *options, LWException **exc)
{
    PSTR ret = NULL;
    QueryDescriptionConfigSsh(options, NULL, &ret, exc);
    return ret;
}

static void DoSsh(JoinProcessOptions *options, LWException **exc)
{
    DJConfigureSshForADLogin(NULL, options->joiningDomain, options, exc);
}

const JoinModule DJSshModule = { TRUE, "ssh", "configure ssh and sshd", QuerySsh, DoSsh, GetSshDescription };
