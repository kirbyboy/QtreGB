#include "file_dialog_i18n.h"

#include "i18n.h"
#include "ImGuiFileDialog.h"

void apply_file_dialog_language()
{
	IGFD::FileDialogStrings strings;
	strings.okButton = i18n::tr("OK");
	strings.cancelButton = i18n::tr("Cancel");
	strings.devicesButton = i18n::tr("Devices");
	strings.driveButton = i18n::tr("Devices");
	strings.search = i18n::tr("Search :");
	strings.dirEntry = i18n::tr("[Dir]");
	strings.linkEntry = i18n::tr("[Link]");
	strings.fileEntry = i18n::tr("[File]");
	strings.fileName = i18n::tr("File Name:");
	strings.dirName = i18n::tr("Directory Path:");
	strings.resetSearchButton = i18n::tr("Reset search");
	strings.editPathButtonHelp = i18n::tr(
		"Edit path\nYou can also right click on path buttons");
	strings.resetPathButton = i18n::tr("Reset to current directory");
	strings.createDirButtonHelp = i18n::tr("Create Directory");
	strings.headerFileName = i18n::tr("File name");
	strings.headerFileType = i18n::tr("Type");
	strings.headerFileSize = i18n::tr("Size");
	strings.headerFileDate = i18n::tr("Date");
	strings.fileSizeByte = i18n::tr("o");
	strings.fileSizeKiloByte = i18n::tr("Ko");
	strings.fileSizeMegaByte = i18n::tr("Mo");
	strings.fileSizeGigaByte = i18n::tr("Go");
	strings.overwriteTitle = i18n::tr("The selected file already exists!");
	strings.overwriteMessage =
		i18n::tr("Are you sure you want to overwrite it?");
	strings.overwriteConfirm = i18n::tr("Confirm");
	strings.overwriteCancel = i18n::tr("Cancel");
	strings.headerThumbnails = i18n::tr("Thumbnails");
	strings.displayModeFilesListHelp = i18n::tr("File List");
	strings.displayModeThumbnailsListHelp = i18n::tr("Thumbnails List");
	strings.displayModeThumbnailsGridHelp = i18n::tr("Thumbnails Grid");
	strings.placesButton = i18n::tr("Places");
	strings.placesButtonHelp = i18n::tr("Places");
	strings.placesBookmarksGroup = i18n::tr("Bookmarks");
	strings.placesDevicesGroup = i18n::tr("Devices");
	strings.validatePlaceButton = i18n::tr("ok");

	IGFD::FileDialog::SetStrings(strings);
}
