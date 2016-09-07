/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiHiiServicesLib.h>
#include <Library/Fonts.h>
#include <Library/DrawUI.h>
#include <Library/UpdateDeviceTree.h>
#include <Protocol/GraphicsOutput.h>

CONST EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutputProtocol = NULL;

STATIC CHAR16 *mFactorName[] = {
	[2] = SYSFONT_2x,
	[3] = SYSFONT_3x,
	[4] = SYSFONT_4x,
	[5] = SYSFONT_5x,
	[6] = SYSFONT_6x,
	[7] = SYSFONT_7x,
	[8] = SYSFONT_8x,
};

STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL mColors[] = {
	[BGR_WHITE]   = {0xff, 0xff, 0xff, 0x00},
	[BGR_BLACK]   = {0x00, 0x00, 0x00, 0x00},
	[BGR_ORANGE]  = {0x00, 0xa5, 0xff, 0x00},
	[BGR_YELLOW]  = {0x00, 0xff, 0xff, 0x00},
	[BGR_RED]     = {0x00, 0x00, 0x98, 0x00},
	[BGR_GREEN]   = {0x00, 0xff, 0x00, 0x00},
	[BGR_BLUE]    = {0xff, 0x00, 0x00, 0x00},
	[BGR_CYAN]    = {0xff, 0xff, 0x00, 0x00},
	[BGR_SILVER]  = {0xc0, 0xc0, 0xc0, 0x00},
};

STATIC UINT32 GetResolutionWidth()
{
	STATIC UINT32 Width;
	EFI_HANDLE    ConsoleHandle = (EFI_HANDLE)NULL;

	/* Get the width from the protocal at the first time */
	if (Width)
		return Width;

	if (GraphicsOutputProtocol == NULL) {
		ConsoleHandle = gST->ConsoleOutHandle;
		if (ConsoleHandle == NULL) {
			DEBUG((EFI_D_ERROR, "Failed to get the handle for the active console input device.\n"));
			return 0;
		}

		gBS->HandleProtocol (
			ConsoleHandle,
			&gEfiGraphicsOutputProtocolGuid,
			(VOID **) &GraphicsOutputProtocol
		);
		if (GraphicsOutputProtocol == NULL) {
			DEBUG((EFI_D_ERROR, "Failed to get the graphics output protocol.\n"));
			return 0;
		}
	}
	Width = GraphicsOutputProtocol->Mode->Info->HorizontalResolution;
	if (!Width)
		DEBUG((EFI_D_ERROR, "Failed to get the width of the screen.\n"));

	return Width;
}

STATIC UINT32 GetResolutionHeight()
{
	STATIC UINT32 Height;
	EFI_HANDLE    ConsoleHandle = (EFI_HANDLE)NULL;

	/* Get the height from the protocal at the first time */
	if (Height)
		return Height;

	if (GraphicsOutputProtocol == NULL) {
		ConsoleHandle = gST->ConsoleOutHandle;
		if (ConsoleHandle == NULL) {
			DEBUG((EFI_D_ERROR, "Failed to get the handle for the active console input device.\n"));
			return 0;
		}

		gBS->HandleProtocol (
			ConsoleHandle,
			&gEfiGraphicsOutputProtocolGuid,
			(VOID **) &GraphicsOutputProtocol
		);
		if (GraphicsOutputProtocol == NULL) {
			DEBUG((EFI_D_ERROR, "Failed to get the graphics output protocol.\n"));
			return 0;
		}
	}
	Height = GraphicsOutputProtocol->Mode->Info->VerticalResolution;
	if (!Height)
		DEBUG((EFI_D_ERROR, "Failed to get the height of the screen.\n"));

	return Height;
}

/* Get Max font count per row */
STATIC UINT32 GetMaxFontCount()
{
	EFI_STATUS Status;
	UINT32 FontBaseWidth = EFI_GLYPH_WIDTH;
	UINT32 max_count = 0;
	EFI_IMAGE_OUTPUT *Blt = NULL;

	Status = gHiiFont->GetGlyph(gHiiFont, "a", NULL, &Blt, NULL);
	if (!EFI_ERROR (Status) && (Status != EFI_WARN_UNKNOWN_GLYPH)) {
		if (Blt)
			FontBaseWidth = Blt->Width;
	}
	max_count = GetResolutionWidth()/FontBaseWidth;
	return max_count;
}

/**
  Get Font's scale factor
  @param[in] ScaleFactorType The type of the scale factor.
  @retval    ScaleFactor     Get the suitable scale factor base on the
                             scale factor's type.
 **/
STATIC UINT32 GetFontScaleFactor(UINT32 ScaleFactorType)
{
	UINT32 scalefactor = GetMaxFontCount()/CHAR_NUM_PERROW;

	if (scalefactor < 2)
		scalefactor = 2;
	else if (scalefactor > ((ARRAY_SIZE(mFactorName) - 1)/MAX_FACTORTYPE))
		scalefactor = (ARRAY_SIZE(mFactorName) - 1)/MAX_FACTORTYPE;

	return scalefactor*ScaleFactorType;
}

/* Get factor name base on the scale factor type */
STATIC CHAR16 *GetFontFactorName(UINT32 ScaleFactorType)
{
	UINT32 ScaleFactor = GetFontScaleFactor(ScaleFactorType);

	if (ScaleFactor <= (ARRAY_SIZE(mFactorName) -1)) {
		return mFactorName[ScaleFactor];
	} else {
		return SYSFONT_3x;
	}
}

STATIC VOID SetBltBuffer(EFI_IMAGE_OUTPUT *BltBuffer)
{
	BltBuffer->Width        = (UINT16) GetResolutionWidth();
	BltBuffer->Height       = (UINT16) GetResolutionHeight();
	BltBuffer->Image.Screen = GraphicsOutputProtocol;
}

STATIC VOID SetDisplayInfo(MENU_MSG_INFO *TargetMenu,
	EFI_FONT_DISPLAY_INFO *FontDisplayInfo)
{
	/* Foreground */
	FontDisplayInfo->ForegroundColor.Blue = mColors[TargetMenu->FgColor].Blue;
	FontDisplayInfo->ForegroundColor.Green = mColors[TargetMenu->FgColor].Green;
	FontDisplayInfo->ForegroundColor.Red = mColors[TargetMenu->FgColor].Red;
	/* Background */
	FontDisplayInfo->BackgroundColor.Blue = mColors[TargetMenu->BgColor].Blue;
	FontDisplayInfo->BackgroundColor.Green = mColors[TargetMenu->BgColor].Green;
	FontDisplayInfo->BackgroundColor.Red = mColors[TargetMenu->BgColor].Red;

	/* Set font name */
	FontDisplayInfo->FontInfoMask = EFI_FONT_INFO_ANY_SIZE | EFI_FONT_INFO_ANY_STYLE;
	CopyMem(&FontDisplayInfo->FontInfo.FontName, GetFontFactorName(TargetMenu->ScaleFactorType),
		StrSize(GetFontFactorName(TargetMenu->ScaleFactorType)));
}

STATIC VOID StrAlignRight(CHAR8* Msg, CHAR8* FilledChar, UINT32 ScaleFactorType) {
	UINT32 i = 0;
	UINT32 diff = 0;
	CHAR8 StrSourceTemp[MAX_MSG_SIZE];
	UINT32 Max_x = GetMaxFontCount();
	UINT32 factor = GetFontScaleFactor(ScaleFactorType);

	SetMem(StrSourceTemp, sizeof(StrSourceTemp), 0);
	if (Max_x/factor > AsciiStrLen(Msg)) {
		diff = Max_x/factor - AsciiStrLen(Msg);
		for (i = 0; i < diff; i++) {
			AsciiStrnCat(StrSourceTemp, FilledChar, Max_x/factor);
		}
		AsciiStrnCat(StrSourceTemp, Msg, Max_x/factor);
		CopyMem(Msg, StrSourceTemp, AsciiStrSize(StrSourceTemp));
	}
}

STATIC VOID StrAlignLeft(CHAR8* Msg, CHAR8* FilledChar, UINT32 ScaleFactorType) {
	UINT32 i = 0;
	UINT32 diff = 0;
	CHAR8 StrSourceTemp[MAX_MSG_SIZE];
	UINT32 Max_x = GetMaxFontCount();
	UINT32 factor = GetFontScaleFactor(ScaleFactorType);

	SetMem(StrSourceTemp, sizeof(StrSourceTemp), 0);
	if (Max_x/factor > AsciiStrLen(Msg)) {
		diff = Max_x/factor - AsciiStrLen(Msg);
		for (i = 0; i < diff; i++) {
			AsciiStrnCat(StrSourceTemp, FilledChar, Max_x/factor);
		}
		AsciiStrnCat(Msg, StrSourceTemp, Max_x/factor);
	}
}

/* Message string manipulation base on the attribute of the message
 * LINEATION:   Fill a string with "_", for drawing a line
 * ALIGN_RIGHT: String align right and fill this string with " "
 * ALIGN_LEFT:  String align left and fill this string with " "
 * OPTION_ITEM: String align left and fill this string with " ",
 *              for updating the whole line's background
 */
STATIC VOID ManipulateMenuMsg(MENU_MSG_INFO *TargetMenu) {
	switch (TargetMenu->Attribute) {
		case LINEATION:
			StrAlignLeft(TargetMenu->Msg, "_", TargetMenu->ScaleFactorType);
			break;
		case ALIGN_RIGHT:
			StrAlignRight(TargetMenu->Msg, " ", TargetMenu->ScaleFactorType);
			break;
		case ALIGN_LEFT:
		case OPTION_ITEM:
			StrAlignLeft(TargetMenu->Msg, " ", TargetMenu->ScaleFactorType);
			break;
	}
}

/**
  Draw menu on the screen
  @param[in] TargetMenu    The message info.
  @param[in, out] pHeight  The Pointer for increased height.
  @retval EFI_SUCCESS      The entry point is executed successfully.
  @retval other            Some error occurs when executing this entry point.
**/
EFI_STATUS DrawMenu(MENU_MSG_INFO *TargetMenu, UINT32 *pHeight)
{
	EFI_STATUS              Status = EFI_SUCCESS;
	EFI_FONT_DISPLAY_INFO   *FontDisplayInfo = NULL;
	EFI_IMAGE_OUTPUT        *BltBuffer = NULL;
	EFI_HII_ROW_INFO        *RowInfoArray = NULL;
	UINTN                   RowInfoArraySize;
	CHAR16                  FontMessage[MAX_MSG_SIZE];
	UINT32                  Height = GetResolutionHeight();
	UINT32                  Width = GetResolutionWidth();

	if (!Height || !Width) {
		Status = EFI_OUT_OF_RESOURCES;
		goto Exit;
	}

	if (TargetMenu->Location >= Height) {
		DEBUG((EFI_D_ERROR, "The Y-axis of the message is larger than the Y-max of the screen\n"));
		Status = EFI_ABORTED;
		goto Exit;
	}

	BltBuffer = AllocateZeroPool (sizeof (EFI_IMAGE_OUTPUT));
	if (BltBuffer == NULL) {
		DEBUG((EFI_D_ERROR, "Failed to allocate zero pool for BltBuffer.\n"));
		Status = EFI_OUT_OF_RESOURCES;
		goto Exit;
	}
	SetBltBuffer(BltBuffer);

	FontDisplayInfo = AllocateZeroPool(sizeof (EFI_FONT_DISPLAY_INFO) + 100);
	if (FontDisplayInfo == NULL) {
		DEBUG((EFI_D_ERROR, "Failed to allocate zero pool for FontDisplayInfo.\n"));
		Status = EFI_OUT_OF_RESOURCES;
		goto Exit;
	}
	SetDisplayInfo(TargetMenu, FontDisplayInfo);

	ManipulateMenuMsg(TargetMenu);
	AsciiStrToUnicodeStr(TargetMenu->Msg, FontMessage);
	if (FontMessage == NULL) {
		DEBUG((EFI_D_ERROR, "Failed to get font message.\n"));
		Status = EFI_OUT_OF_RESOURCES;
		goto Exit;
	}

	Status = gHiiFont->StringToImage(
		gHiiFont,
		/* Set to 0 for Bitmap mode */
		EFI_HII_DIRECT_TO_SCREEN | EFI_HII_OUT_FLAG_WRAP,
		FontMessage,
		FontDisplayInfo,
		&BltBuffer,
		0,                      /* BltX */
		TargetMenu->Location,   /* BltY */
		&RowInfoArray,
		&RowInfoArraySize,
		NULL
	);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Failed to render a string to the display: %r\n", Status));
		goto Exit;
	}

	if (pHeight && RowInfoArraySize && RowInfoArray) {
		*pHeight = RowInfoArraySize * RowInfoArray[0].LineHeight;
	}

	/* For Bitmap mode, use EfiBltBufferToVideo, and set DestX,DestY as needed */
	GraphicsOutputProtocol->Blt(
		GraphicsOutputProtocol,
		BltBuffer->Image.Bitmap,
		EfiBltVideoToVideo,
		0,    /* SrcX */
		0,    /* SrcY */
		0,    /* DestX */
		0,    /* DestY */
		BltBuffer->Width,
		BltBuffer->Height,
		BltBuffer->Width * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
	);

Exit:
	if (RowInfoArray) {
		FreePool(RowInfoArray);
		BltBuffer = NULL;
	}

	if (BltBuffer) {
		FreePool(BltBuffer);
		BltBuffer = NULL;
	}

	if (FontDisplayInfo) {
		FreePool(FontDisplayInfo);
		FontDisplayInfo = NULL;
	}
	return Status;
}

/**
  Set the message info
  @param[in]  Msg              Message.
  @param[in]  ScaleFactorType  The scale factor type of the message.
  @param[in]  FgColor          The foreground color of the message.
  @param[in]  BgColor          The background color of the message.
  @param[in]  Attribute        The attribute of the message.
  @param[in]  Location         The location of the message.
  @param[in]  Action           The action of the message.
  @param[out] MenuMsgInfo      The message info.
**/
VOID SetMenuMsgInfo(MENU_MSG_INFO *MenuMsgInfo, CHAR8* Msg, UINT32 ScaleFactorType,
	UINT32 FgColor, UINT32 BgColor, UINT32 Attribute, UINT32 Location, UINT32 Action)
{
	CopyMem(&MenuMsgInfo->Msg, Msg, AsciiStrSize(Msg));
	MenuMsgInfo->ScaleFactorType = ScaleFactorType;
	MenuMsgInfo->FgColor = FgColor;
	MenuMsgInfo->BgColor = BgColor;
	MenuMsgInfo->Attribute = Attribute;
	MenuMsgInfo->Location = Location;
	MenuMsgInfo->Action = Action;
}

/**
  Update the background color of the message
  @param[in]  MenuMsgInfo The message info.
  @param[in]  NewBgColor  The new background color of the message.
  @retval EFI_SUCCESS     The entry point is executed successfully.
  @retval other           Some error occurs when executing this entry point.
**/
EFI_STATUS UpdateMsgBackground(MENU_MSG_INFO *MenuMsgInfo, UINT32 NewBgColor)
{
	MENU_MSG_INFO *target_msg_info = NULL;

	target_msg_info = AllocateZeroPool(sizeof(MENU_MSG_INFO));
	if (target_msg_info == NULL) {
		DEBUG((EFI_D_ERROR, "Failed to allocate zero pool for message info.\n"));
		return EFI_OUT_OF_RESOURCES;
	}

	SetMenuMsgInfo(target_msg_info, MenuMsgInfo->Msg,
		MenuMsgInfo->ScaleFactorType,
		MenuMsgInfo->FgColor,
		NewBgColor,
		MenuMsgInfo->Attribute,
		MenuMsgInfo->Location,
		MenuMsgInfo->Action);
	DrawMenu(target_msg_info, 0);

	if (target_msg_info) {
		FreePool(target_msg_info);
		target_msg_info = NULL;
	}
	return EFI_SUCCESS;
}
