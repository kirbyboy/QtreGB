#pragma once

#ifdef __cplusplus

// Local extension to ImGuiFileDialog v0.6.8: runtime UI strings.
//
// This is not upstream ImGuiFileDialog code. It is part of the vendored copy
// in QtreGB and is covered by the same MIT license as the rest of this
// directory (see LICENSE and LOCAL_CHANGES.md).

#include <string>

namespace IGFD
{
struct FileDialogStrings
{
    std::string createDirButton = "+";
    std::string okButton = "OK";
    std::string cancelButton = "Cancel";
    std::string resetButton = "R";
    std::string devicesButton = "Devices";
    std::string editPathButton = "E";
    std::string search = "Search :";
    std::string dirEntry = "[Dir]";
    std::string linkEntry = "[Link]";
    std::string fileEntry = "[File]";
    std::string fileName = "File Name:";
    std::string dirName = "Directory Path:";
    std::string resetSearchButton = "Reset search";
    std::string driveButton = "Devices";
    std::string editPathButtonHelp = "Edit path\nYou can also right click on path buttons";
    std::string resetPathButton = "Reset to current directory";
    std::string createDirButtonHelp = "Create Directory";
    std::string sortAscendingIcon = "A|";
    std::string sortDescendingIcon = "D|";
    std::string headerFileName = "File name";
    std::string headerFileType = "Type";
    std::string headerFileSize = "Size";
    std::string headerFileDate = "Date";
    std::string fileSizeByte = "o";
    std::string fileSizeKiloByte = "Ko";
    std::string fileSizeMegaByte = "Mo";
    std::string fileSizeGigaByte = "Go";
    std::string overwriteTitle = "The selected file already exists!";
    std::string overwriteMessage = "Are you sure you want to overwrite it?";
    std::string overwriteConfirm = "Confirm";
    std::string overwriteCancel = "Cancel";
    std::string headerThumbnails = "Thumbnails";
    std::string displayModeFilesList = "FL";
    std::string displayModeFilesListHelp = "File List";
    std::string displayModeThumbnailsList = "TL";
    std::string displayModeThumbnailsListHelp = "Thumbnails List";
    std::string displayModeThumbnailsGrid = "TG";
    std::string displayModeThumbnailsGridHelp = "Thumbnails Grid";
    std::string placesButton = "Places";
    std::string placesButtonHelp = "Places";
    std::string placesBookmarksGroup = "Bookmarks";
    std::string placesDevicesGroup = "Devices";
    std::string addPlaceButton = "+";
    std::string removePlaceButton = "-";
    std::string validatePlaceButton = "ok";
    std::string editPlaceButton = "E";
};
} // namespace IGFD

#endif  // __cplusplus
