/** @file

  Copyright (c) 2008 - 2009, Apple Inc. All rights reserved.<BR>

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/


#ifndef __EBL_ADD_COMMAND_H__
#define __EBL_ADD_COMMAND_H__



//
// Protocol GUID
//
// AEDA2428-9A22-4637-9B21-545E28FBB829

#define EBL_ADD_COMMAND_PROTOCOL_GUID \
  { 0xaeda2428, 0x9a22, 0x4637, { 0x9b, 0x21, 0x54, 0x5e, 0x28, 0xfb, 0xb8, 0x29 } }


typedef struct _EBL_ADD_COMMAND_PROTOCOL  EBL_ADD_COMMAND_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *EBL_COMMMAND) (
  IN UINTN  Argc,
  IN CHAR8  **Argv
  );

typedef struct {
  CHAR8           *Name;
  CHAR8           *HelpSummary;
  CHAR8           *Help;
  EBL_COMMMAND    Command;
} EBL_COMMAND_TABLE;


/**
  Add a single command table entry.

  @param EntryArray     Pointer EBL_COMMAND_TABLE of the command that is being added

**/
typedef
VOID
(EFIAPI *EBL_ADD_COMMAND) (
  IN const EBL_COMMAND_TABLE   *Entry
  );


/**
  Add a multiple command table entry.

  @param EntryArray     Pointer EBL_COMMAND_TABLE of the commands that are being added

  @param ArrayCount     Number of commands in the EntryArray.

**/
typedef
VOID
(EFIAPI *EBL_ADD_COMMANDS) (
  IN const EBL_COMMAND_TABLE   *EntryArray,
  IN UINTN                     ArrayCount
  );


typedef
VOID
(EFIAPI *EBL_GET_CHAR_CALL_BACK) (
  IN  UINTN   ElapsedTime
  );

/**
  Return a keypress or optionally timeout if a timeout value was passed in.
  An optional callback function is called every second when waiting for a
  timeout.

  @param  Key           EFI Key information returned
  @param  TimeoutInSec  Number of seconds to wait to timeout
  @param  CallBack      Callback called every second during the timeout wait

  @return EFI_SUCCESS  Key was returned
  @return EFI_TIMEOUT  If the TimoutInSec expired

**/
typedef
EFI_STATUS
(EFIAPI *EBL_GET_CHAR_KEY) (
  IN OUT EFI_INPUT_KEY            *Key,
  IN     UINTN                    TimeoutInSec,
  IN     EBL_GET_CHAR_CALL_BACK   CallBack   OPTIONAL
  );


/**
  This routine is used prevent command output data from scrolling off the end
  of the screen. The global gPageBreak is used to turn on or off this feature.
  If the CurrentRow is near the end of the screen pause and print out a prompt
  If the use hits Q to quit return TRUE else for any other key return FALSE.
  PrefixNewline is used to figure out if a newline is needed before the prompt
  string. This depends on the last print done before calling this function.
  CurrentRow is updated by one on a call or set back to zero if a prompt is
  needed.

  @param  CurrentRow  Used to figure out if its the end of the page and updated
  @param  PrefixNewline  Did previous print issue a newline

  @return TRUE if Q was hit to quit, FALSE in all other cases.

**/
typedef
BOOLEAN
(EFIAPI *EBL_ANY_KEY_CONTINUE_Q_QUIT) (
  IN  UINTN   *CurrentRow,
  IN  BOOLEAN PrefixNewline
  );



struct _EBL_ADD_COMMAND_PROTOCOL {
  EBL_ADD_COMMAND     AddCommand;
  EBL_ADD_COMMANDS    AddCommands;

  // Commands to reuse EBL infrastructure
  EBL_GET_CHAR_KEY            EblGetCharKey;
  EBL_ANY_KEY_CONTINUE_Q_QUIT EblAnyKeyToContinueQtoQuit;
};

extern EFI_GUID gEfiEblAddCommandProtocolGuid;

#endif


