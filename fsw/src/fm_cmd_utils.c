/*
** Filename: fm_cmd_utils.c 
**
** NASA Docket No. GSC-18,475-1, identified as “Core Flight Software System (CFS)
** File Manager Application Version 2.5.3
**
** Copyright © 2020 United States Government as represented by the Administrator of
** the National Aeronautics and Space Administration. All Rights Reserved. 
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
**  
** You may obtain a copy of the License at 
** http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License. 
*
** Title: File Manager (FM) Command Utility Functions
**
** Purpose: Provides file manager utility function definitions for
**          processing file manager commands
**
** Notes:
**
*/

#include "cfe.h"
#include "fm_msg.h"
#include "fm_cmd_utils.h"
#include "fm_child.h"
#include "fm_perfids.h"
#include "fm_events.h"
#include "cfs_utils.h"

#include <string.h>
#include <ctype.h>

static uint32 OpenFileCount = 0;
static boolean FileIsOpen = FALSE;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- verify command packet length             */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

boolean FM_IsValidCmdPktLength(CFE_SB_MsgPtr_t CmdPacket, uint16 ExpectedLength,
                                uint32 EventID, char *CmdText)
{
    boolean FunctionResult = TRUE;
    uint16 ActualLength = 0;

    ActualLength = CFE_SB_GetTotalMsgLength(CmdPacket);

    /* Verify command packet length */
    if (ActualLength != ExpectedLength)
    {
        FunctionResult = FALSE;

        CFE_EVS_SendEvent(EventID, CFE_EVS_ERROR,
                         "%s error: invalid command packet length: expected = %d, actual = %d",
                          CmdText, ExpectedLength, ActualLength);
    }

    return(FunctionResult);

} /* FM_IsValidCmdPktLength */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- verify state is not invalid              */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

boolean FM_VerifyOverwrite(uint16 Overwrite, uint32 EventID, char *CmdText)
{
    boolean FunctionResult = TRUE;

    /* Acceptable values are TRUE (one) and FALSE (zero) */
    if ((Overwrite != TRUE) && (Overwrite != FALSE))
    {
        CFE_EVS_SendEvent(EventID, CFE_EVS_ERROR,
                         "%s error: invalid overwrite = %d", CmdText, Overwrite);

        FunctionResult = FALSE;
    }

    return(FunctionResult);

} /* End FM_VerifyOverwrite */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- get open files data                      */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


static void LoadOpenFileData(uint32 ObjId, void* CallbackArg)
{
    FM_OpenFilesEntry_t *OpenFilesData = (FM_OpenFilesEntry_t*)CallbackArg;
    CFE_ES_TaskInfo_t    TaskInfo;
    OS_file_prop_t       FdProp;

    if(OS_IdentifyObject(ObjId) == OS_OBJECT_TYPE_OS_STREAM) 
    {
        if(OpenFilesData != (FM_OpenFilesEntry_t*) NULL)
        {
            if(OS_FDGetInfo(ObjId, &FdProp) == OS_SUCCESS) 
            {
                strncpy(OpenFilesData[OpenFileCount].LogicalName, 
                        FdProp.Path,
                        OS_MAX_PATH_LEN);

                /* Get the name of the application that opened the file */
                memset(&TaskInfo, 0, sizeof(CFE_ES_TaskInfo_t));

                if(CFE_ES_GetTaskInfo(&TaskInfo, FdProp.User) == CFE_SUCCESS)
                {
                    strncpy(OpenFilesData[OpenFileCount].AppName,
                            (char*)TaskInfo.AppName,
                            OS_MAX_API_NAME);
                }
            }
        } 

        OpenFileCount++;
    }

} /* End LoadOpenFileData() */

uint32 FM_GetOpenFilesData(FM_OpenFilesEntry_t *OpenFilesData)
{
    OpenFileCount = 0;
    
    OS_ForEachObject(0, LoadOpenFileData, (void*)OpenFilesData);

    return(OpenFileCount);

} /* End FM_GetOpenFilesData */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- query filename state                     */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


static void SearchOpenFileData(uint32 ObjId, void* CallbackArg)
{
    char* Fname = (char*)CallbackArg;
    OS_file_prop_t       FdProp;

    if(Fname == NULL) 
    {
        return;
    }

    if(OS_IdentifyObject(ObjId) == OS_OBJECT_TYPE_OS_STREAM) 
    {

        /* Get system info for each file descriptor table entry */
        /* If the FD table entry is valid - then the file is open */
        if (OS_FDGetInfo(ObjId, &FdProp) == OS_FS_SUCCESS)
        {
            if (strcmp(Fname, FdProp.Path) == 0)
            {
                FileIsOpen = TRUE;
            }
        }
    }

} /* End SearchOpenFileData() */


uint32 FM_GetFilenameState(char *Filename, uint32 BufferSize, boolean FileInfoCmd)
{
    os_fstat_t FileStatus;
    uint32     FilenameState = FM_NAME_IS_INVALID;
    boolean    FilenameIsValid = FALSE;
    int32      StringLength = 0;


    /* Search Filename for a string terminator */
    for (StringLength = 0; StringLength < BufferSize; StringLength++)
    {
        if (Filename[StringLength] == '\0')
        {
            break;
        }
    }

    /* Verify that Filename is not empty and has a terminator */
    if ((StringLength > 0) && (StringLength < BufferSize))
    {
        /* Verify that the string characters are OK for a filename */
        FilenameIsValid = CFS_IsValidFilename(Filename, StringLength);
    }

    /* If Filename is valid, then determine its state */
    if (FilenameIsValid)
    {
        /* Check to see if Filename is in use */
        if (OS_stat(Filename, &FileStatus) == OS_SUCCESS)
        {
            /* Filename is in use, is it also a directory? */
#ifdef OS_FILESTAT_ISDIR
            if (OS_FILESTAT_ISDIR(FileStatus))
#else
            if (S_ISDIR(FileStatus.st_mode))
#endif
            {
                /* Filename is a directory */
                FilenameState = FM_NAME_IS_DIRECTORY;
            }
            else
            {
                /* Filename is a file, but is it open? */
                FilenameState = FM_NAME_IS_FILE_CLOSED;
                FileIsOpen = FALSE;
                

                // Commented out as part of nos3#118 issue
                // Results in file always returning closed, but resolves crash
                //OS_ForEachObject(0, SearchOpenFileData, Filename);

                if(FileIsOpen == TRUE)
                {
                    FilenameState = FM_NAME_IS_FILE_OPEN;
                }
            }

            /* Save the last modify time and file size for File Info commands */
            if (FileInfoCmd)
            {
#ifdef OS_FILESTAT_TIME
                FM_GlobalData.FileStatTime = OS_FILESTAT_TIME(FileStatus);
#else
                FM_GlobalData.FileStatTime = FileStatus.st_mtime;
#endif
#ifdef OS_FILESTAT_SIZE
                FM_GlobalData.FileStatSize = OS_FILESTAT_SIZE(FileStatus);
#else
                FM_GlobalData.FileStatSize = FileStatus.st_size;
#endif
            }
        }
        else
        {
            /* Cannot get file stat - therefore does not exist */
            FilenameState = FM_NAME_IS_NOT_IN_USE;

            /* Save the last modify time and file size for File Info commands */
            if (FileInfoCmd)
            {
                FM_GlobalData.FileStatSize = 0;
                FM_GlobalData.FileStatTime = 0;
            }
        }
    }

    return(FilenameState);

} /* End FM_GetFilenameState */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- verify state is not invalid              */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

uint32 FM_VerifyNameValid(char *Name, uint32 BufferSize, uint32 EventID, char *CmdText)
{
    uint32 FilenameState = FM_NAME_IS_INVALID;

    /* Looking for filename state != FM_NAME_IS_INVALID */
    FilenameState = FM_GetFilenameState(Name, BufferSize, TRUE);

    if (FilenameState == FM_NAME_IS_INVALID)
    {
        /* Insert a terminator in case the invalid string did not have one */
        Name[BufferSize - 1] = '\0';
        CFE_EVS_SendEvent(EventID, CFE_EVS_ERROR,
                         "%s error: invalid name: name = %s", CmdText, Name);
    }

    return(FilenameState);

} /* End FM_VerifyNameValid */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- verify state is closed file              */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

boolean FM_VerifyFileClosed(char *Filename, uint32 BufferSize, uint32 EventID, char *CmdText)
{
    boolean Result = FALSE;
    uint32 FilenameState = FM_NAME_IS_INVALID;

    /* Looking for filename state = file (closed) */
    FilenameState = FM_GetFilenameState(Filename, BufferSize, FALSE);

    if (FilenameState == FM_NAME_IS_INVALID)
    {
        /* Insert a terminator in case the invalid string did not have one */
        Filename[BufferSize - 1] = '\0';
        CFE_EVS_SendEvent((EventID + FM_FNAME_INVALID_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: filename is invalid: name = %s", CmdText, Filename);
    }
    else if (FilenameState == FM_NAME_IS_NOT_IN_USE)
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_DNE_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: file does not exist: name = %s", CmdText, Filename);
    }
    else if (FilenameState == FM_NAME_IS_FILE_OPEN)
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_ISOPEN_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: file is already open: name = %s", CmdText, Filename);
    }
    else if (FilenameState == FM_NAME_IS_FILE_CLOSED)
    {
        Result = TRUE;
    }
    else if (FilenameState == FM_NAME_IS_DIRECTORY)
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_ISDIR_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: filename is a directory: name = %s", CmdText, Filename);
    }
    else
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_UNKNOWN_EID_OFFSET), CFE_EVS_ERROR,
                          "%s error: filename has unknown state: name = %s, state = %d",
                          CmdText, Filename, FilenameState);
    }

    return(Result);

} /* End FM_VerifyFileClosed */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- verify state is open or closed file      */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

boolean FM_VerifyFileExists(char *Filename, uint32 BufferSize, uint32 EventID, char *CmdText)
{
    boolean Result = FALSE;
    uint32 FilenameState = FM_NAME_IS_INVALID;

    /* Looking for filename state = file (open or closed) */
    FilenameState = FM_GetFilenameState(Filename, BufferSize, FALSE);

    if (FilenameState == FM_NAME_IS_INVALID)
    {
        /* Insert a terminator in case the invalid string did not have one */
        Filename[BufferSize - 1] = '\0';
        CFE_EVS_SendEvent((EventID + FM_FNAME_INVALID_EID_OFFSET) , CFE_EVS_ERROR,
                         "%s error: filename is invalid: name = %s", CmdText, Filename);
    }
    else if (FilenameState == FM_NAME_IS_NOT_IN_USE)
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_DNE_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: file does not exist: name = %s", CmdText, Filename);
    }
    else if ((FilenameState == FM_NAME_IS_FILE_OPEN) ||
             (FilenameState == FM_NAME_IS_FILE_CLOSED))
    {
        Result = TRUE;
    }
    else if (FilenameState == FM_NAME_IS_DIRECTORY)
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_ISDIR_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: filename is a directory: name = %s", CmdText, Filename);
    }
    else
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_UNKNOWN_EID_OFFSET), CFE_EVS_ERROR,
                          "%s error: filename has unknown state: name = %s, state = %d",
                          CmdText, Filename, FilenameState);
    }

    return(Result);

} /* End FM_VerifyFileExists */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- verify state is unused                   */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

boolean FM_VerifyFileNoExist(char *Filename, uint32 BufferSize, uint32 EventID, char *CmdText)
{
    boolean Result = FALSE;
    uint32 FilenameState = FM_NAME_IS_INVALID;
 
    /* Looking for filename state = not in use */
    FilenameState = FM_GetFilenameState(Filename, BufferSize, FALSE);

    if (FilenameState == FM_NAME_IS_INVALID)
    {
        /* Insert a terminator in case the invalid string did not have one */
        Filename[BufferSize - 1] = '\0';
        CFE_EVS_SendEvent((EventID + FM_FNAME_INVALID_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: filename is invalid: name = %s", CmdText, Filename);
    }
    else if (FilenameState == FM_NAME_IS_NOT_IN_USE)
    {
        Result = TRUE;
    }
    else if ((FilenameState == FM_NAME_IS_FILE_OPEN) ||
             (FilenameState == FM_NAME_IS_FILE_CLOSED))
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_EXIST_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: file already exists: name = %s", CmdText, Filename);
    }
    else if (FilenameState == FM_NAME_IS_DIRECTORY)
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_ISDIR_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: filename is a directory: name = %s", CmdText, Filename);
    }
    else
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_UNKNOWN_EID_OFFSET), CFE_EVS_ERROR,
                          "%s error: directory name has unknown state: name = %s, state = %d",
                          CmdText, Filename, FilenameState);
    }

    return(Result);

} /* End FM_VerifyFileNoExist */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- verify state is unused or closed file    */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

boolean FM_VerifyFileNotOpen(char *Filename, uint32 BufferSize, uint32 EventID, char *CmdText)
{
    boolean Result = FALSE;
    uint32 FilenameState = FM_NAME_IS_INVALID;

    /* Looking for filename state = file (closed) or name not in use */
    FilenameState = FM_GetFilenameState(Filename, BufferSize, FALSE);

    if (FilenameState == FM_NAME_IS_INVALID)
    {
        /* Insert a terminator in case the invalid string did not have one */
        Filename[BufferSize - 1] = '\0';
        CFE_EVS_SendEvent((EventID + FM_FNAME_INVALID_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: filename is invalid: name = %s", CmdText, Filename);
    }
    else if (FilenameState == FM_NAME_IS_NOT_IN_USE)
    {
        Result = TRUE;
    }
    else if (FilenameState == FM_NAME_IS_FILE_OPEN)
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_ISOPEN_EID_OFFSET), CFE_EVS_ERROR,
                          "%s error: file exists as an open file: name = %s", 
                          CmdText, Filename);
    }
    else if (FilenameState == FM_NAME_IS_FILE_CLOSED)
    {
        Result = TRUE;
    }
    else if (FilenameState == FM_NAME_IS_DIRECTORY)
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_ISDIR_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: filename is a directory: name = %s", CmdText, Filename);
    }
    else
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_UNKNOWN_EID_OFFSET), CFE_EVS_ERROR,
                          "%s error: filename has unknown state: name = %s, state = %d",
                          CmdText, Filename, FilenameState);

    }

    return(Result);

} /* End FM_VerifyFileNotOpen */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- verify state is directory                */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

boolean FM_VerifyDirExists(char *Directory, uint32 BufferSize, uint32 EventID, char *CmdText)
{
    boolean Result = FALSE;
    uint32 FilenameState = FM_NAME_IS_INVALID;

    /* Looking for filename state = directory */
    FilenameState = FM_GetFilenameState(Directory, BufferSize, FALSE);

    if (FilenameState == FM_NAME_IS_INVALID)
    {
        /* Insert a terminator in case the invalid string did not have one */
        Directory[BufferSize - 1] = '\0';
        CFE_EVS_SendEvent((EventID + FM_FNAME_INVALID_EID_OFFSET), CFE_EVS_ERROR,
                          "%s error: directory name is invalid: name = %s", 
                          CmdText, Directory);
    }
    else if (FilenameState == FM_NAME_IS_NOT_IN_USE)
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_DNE_EID_OFFSET), CFE_EVS_ERROR,
                           "%s error: directory does not exist: name = %s", 
                           CmdText, Directory);
    }
    else if ((FilenameState == FM_NAME_IS_FILE_OPEN) ||
             (FilenameState == FM_NAME_IS_FILE_CLOSED))
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_ISFILE_EID_OFFSET), CFE_EVS_ERROR,
                          "%s error: directory name exists as a file: name %s", 
                          CmdText, Directory);
    }
    else if (FilenameState == FM_NAME_IS_DIRECTORY)
    {
        Result = TRUE;
    }
    else {
        CFE_EVS_SendEvent((EventID + FM_FNAME_UNKNOWN_EID_OFFSET), CFE_EVS_ERROR,
                          "%s error: directory name has unknown state: name = %s, state = %d",
                          CmdText, Directory, FilenameState);
    }

    return(Result);

} /* End FM_VerifyDirExists */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- verify state is unused                   */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

boolean FM_VerifyDirNoExist(char *Name, uint32 BufferSize, uint32 EventID, char *CmdText)
{
    boolean Result = FALSE;
    uint32 FilenameState = FM_NAME_IS_INVALID;

    /* Looking for filename state = unused */
    FilenameState = FM_GetFilenameState(Name, BufferSize, FALSE);

    if (FilenameState == FM_NAME_IS_INVALID)
    {
        /* Insert a terminator in case the invalid string did not have one */
        Name[BufferSize - 1] = '\0';
        CFE_EVS_SendEvent((EventID + FM_FNAME_INVALID_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: directory name is invalid: name = %s", CmdText, Name);
    }
    else if (FilenameState == FM_NAME_IS_NOT_IN_USE)
    {
        Result = TRUE;
    }
    else if ((FilenameState == FM_NAME_IS_FILE_OPEN) ||
             (FilenameState == FM_NAME_IS_FILE_CLOSED))
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_DNE_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: directory name exists as a file: name = %s", CmdText, Name);
    }
    else if (FilenameState == FM_NAME_IS_DIRECTORY)
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_ISDIR_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: directory already exists: name = %s", CmdText, Name);
    }
    else
    {
        CFE_EVS_SendEvent((EventID + FM_FNAME_UNKNOWN_EID_OFFSET), CFE_EVS_ERROR,
                          "%s error: directory name has unknown state: name = %s, state = %d",
                          CmdText, Name, FilenameState);
    }

    return(Result);

} /* End FM_VerifyDirNoExist */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- verify child task interface is alive     */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

boolean FM_VerifyChildTask(uint32 EventID, char *CmdText)
{
    boolean Result = FALSE;

    /* Copy of child queue count that child task cannot change */
    uint8 LocalQueueCount = FM_GlobalData.ChildQueueCount;

    /* Verify child task is active and queue interface is healthy */
    if (FM_GlobalData.ChildSemaphore == FM_CHILD_SEM_INVALID)
    {
        CFE_EVS_SendEvent((EventID + FM_CHILD_DISABLED_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: child task is disabled", CmdText);

        /* Child task disabled - cannot add another command */
        Result = FALSE;
    }
    else if (LocalQueueCount == FM_CHILD_QUEUE_DEPTH)
    {
        CFE_EVS_SendEvent((EventID + FM_CHILD_Q_FULL_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: child task queue is full", CmdText);

        /* Queue full - cannot add another command */
        Result = FALSE;
    }
    else if ((LocalQueueCount > FM_CHILD_QUEUE_DEPTH) ||
             (FM_GlobalData.ChildWriteIndex >= FM_CHILD_QUEUE_DEPTH))
    {
        CFE_EVS_SendEvent((EventID + FM_CHILD_BROKEN_EID_OFFSET), CFE_EVS_ERROR,
                         "%s error: child task interface is broken: count = %d, index = %d",
                          CmdText, LocalQueueCount, FM_GlobalData.ChildWriteIndex);

        /* Queue broken - cannot add another command */
        Result = FALSE;
    }
    else
    {
        CFE_PSP_MemSet(&FM_GlobalData.ChildQueue[FM_GlobalData.ChildWriteIndex],
                       0, sizeof(FM_ChildQueueEntry_t));

        /* OK to add another command to the queue */
        Result = TRUE;
    }

    return(Result);

} /* End FM_VerifyChildTask */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- invoke child task command processor      */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void FM_InvokeChildTask(void)
{
    /* Update callers queue index */
    FM_GlobalData.ChildWriteIndex++;

    if (FM_GlobalData.ChildWriteIndex >= FM_CHILD_QUEUE_DEPTH)
    {
        FM_GlobalData.ChildWriteIndex = 0;
    }

    /* Prevent parent/child updating queue counter at same time */
    OS_MutSemTake(FM_GlobalData.ChildQueueCountSem);
    FM_GlobalData.ChildQueueCount++;
    OS_MutSemGive(FM_GlobalData.ChildQueueCountSem);

    /* Does the child task still have a semaphore? */
    if (FM_GlobalData.ChildSemaphore != FM_CHILD_SEM_INVALID)
    {
        /* Signal child task to call command handler */
        OS_CountSemGive(FM_GlobalData.ChildSemaphore);
    }

    return;

} /* End of FM_InvokeChildTask */


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* FM utility function -- add path separator to directory name     */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void FM_AppendPathSep(char *Directory, uint32 BufferSize)
{
    /*
    **  Previous verification tests ensure that the length of
    **   the string is both non-zero and less than the size
    **   of the string buffer.
    */
    uint32 StringLength = 0;
    
    StringLength = strlen(Directory);

    /* Do nothing if string already ends with a path separator */
    if (Directory[StringLength - 1] != '/')
    {
        /* Verify that string buffer has room for a path separator */
        if (StringLength < (BufferSize - 1))
        {
            strncat(Directory, "/", 1);
        }
    }

    return;

} /* End of FM_AppendPathSep */


/************************/
/*  End of File Comment */
/************************/
