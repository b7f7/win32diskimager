/**********************************************************************
 *  This program is free software; you can redistribute it and/or     *
 *  modify it under the terms of the GNU General Public License       *
 *  as published by the Free Software Foundation; either version 2    *
 *  of the License, or (at your option) any later version.            *
 *                                                                    *
 *  This program is distributed in the hope that it will be useful,   *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of    *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the     *
 *  GNU General Public License for more details.                      *
 *                                                                    *
 *  You should have received a copy of the GNU General Public License *
 *  along with this program; if not, see http://gnu.org/licenses/     *
 *  ---                                                               *
 *  Copyright (C) 2009, Justin Davis <tuxdavis@gmail.com>             *
 *  Copyright (C) 2009-2017 ImageWriter developers                    *
 *                 https://sourceforge.net/projects/win32diskimager/  *
 **********************************************************************/

#ifndef WINVER
#define WINVER 0x0601
#endif

#include <QtWidgets>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include <winioctl.h>
#include "disk.h"
#include "mainwindow.h"
inline QString GetErrorMessage()
{
    wchar_t*  errormessage=nullptr;
    ::FormatMessageW(  FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER
                     , nullptr
                     , GetLastError()
                     , 0
                     , (LPWSTR)&errormessage
                     , 0
                     , nullptr);
    QString errText = QString::fromUtf16((const ushort *)errormessage);
    LocalFree(errormessage);
    return errText;
}

HANDLE getHandleOnFile(LPCWSTR filelocation, DWORD access)
{
    HANDLE hFile;
    hFile = CreateFileW(filelocation, access, (access == GENERIC_READ) ? FILE_SHARE_READ : 0, NULL, (access == GENERIC_READ) ? OPEN_EXISTING:CREATE_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        QMessageBox::critical(MainWindow::getInstance(), QObject::tr("File Error"), QObject::tr("An error occurred when attempting to get a handle on the file.\n"
                                                              "Error %1: %2").arg(GetLastError()).arg(GetErrorMessage() ) );
    }
    return hFile;
}
DWORD getDeviceID(HANDLE hVolume)
{
    VOLUME_DISK_EXTENTS sd;
    DWORD bytesreturned;
    if (!DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &sd, sizeof(sd), &bytesreturned, NULL))
    {
        QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Volume Error"),
                              QObject::tr("An error occurred when attempting to get information on volume.\n"
                                          "Error %1: %2").arg(GetLastError()).arg( GetErrorMessage() ) );
    }
    return sd.Extents[0].DiskNumber;
}

HANDLE getHandleOnDevice(int device, DWORD access)
{
    HANDLE hDevice;
    QString devicename = QString("\\\\.\\PhysicalDrive%1").arg(device);
    hDevice = CreateFile(devicename.toLatin1().data(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Device Error"),
                              QObject::tr("An error occurred when attempting to get a handle on the device.\n"
                                          "Error %1: %2").arg(GetLastError()).arg(GetErrorMessage() ) );
    }
    return hDevice;
}

HANDLE getHandleOnVolume(int volume, DWORD access)
{
    HANDLE hVolume;
    char volumename[] = "\\\\.\\A:";
    volumename[4] += volume;
    hVolume = CreateFile(volumename, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hVolume == INVALID_HANDLE_VALUE)
    {
        QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Volume Error"),
                              QObject::tr("An error occurred when attempting to get a handle on the volume.\n"
                                          "Error %1: %2").arg(GetLastError()).arg(GetErrorMessage() ) );
    }
    return hVolume;
}

bool getLockOnVolume(HANDLE handle)
{
    DWORD bytesreturned;
    BOOL bResult;
    bResult = DeviceIoControl(handle, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesreturned, NULL);
    if (!bResult)
    {
        QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Lock Error"),
                              QObject::tr("An error occurred when attempting to lock the volume.\n"
                                          "Error %1: %2").arg(GetLastError()).arg(GetErrorMessage() ) );
    }
    return (bResult);
}

bool removeLockOnVolume(HANDLE handle)
{
    DWORD junk;
    BOOL bResult;
    bResult = DeviceIoControl(handle, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &junk, NULL);
    if (!bResult)
    {
        QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Unlock Error"),
                              QObject::tr("An error occurred when attempting to unlock the volume.\n"
                                          "Error %1: %2").arg(GetLastError()).arg(GetErrorMessage() ) );
    }
    return (bResult);
}

bool unmountVolume(HANDLE handle)
{
    DWORD junk;
    BOOL bResult;
    bResult = DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &junk, NULL);
    if (!bResult)
    {
        QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Dismount Error"),
                              QObject::tr("An error occurred when attempting to dismount the volume.\n"
                                          "Error %1: %2").arg(GetLastError()).arg(GetErrorMessage() ));
    }
    return (bResult);
}

bool isVolumeUnmounted(HANDLE handle)
{
    DWORD junk;
    BOOL bResult;
    bResult = DeviceIoControl(handle, FSCTL_IS_VOLUME_MOUNTED, NULL, 0, NULL, 0, &junk, NULL);
    return (!bResult);
}

template <typename T>
void array_delete(T* v)
{
    delete[] v;
    v = nullptr;
}
QSharedPointer<char> readSectorDataFromHandle(HANDLE handle, unsigned long long startsector, unsigned long long numsectors, unsigned long long sectorsize)
{
    unsigned long bytesread;
    QSharedPointer<char> data(new char[sectorsize * numsectors], array_delete<char> );
    if( data.isNull() )
    {
        QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Read Error"),
                              QObject::tr("An error occurred when attempting to allocate %1 bytes.\n").arg(sectorsize * numsectors) );
    }
    else
    {
        LARGE_INTEGER li;

        li.QuadPart = startsector * sectorsize;
        SetFilePointer(handle, li.LowPart, &li.HighPart, FILE_BEGIN);
        if (!ReadFile(handle, data.data(), sectorsize * numsectors, &bytesread, NULL))
        {
            QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Read Error"),
                                  QObject::tr("An error occurred when attempting to read data from handle.\n"
                                              "Error %1: %2").arg(GetLastError()).arg(GetErrorMessage() ));

        }
        if (bytesread < (sectorsize * numsectors))
        {
                memset(data.data() + bytesread,0,(sectorsize * numsectors) - bytesread);
        }
    }
    return data;
}

bool writeSectorDataToHandle(HANDLE handle, const QSharedPointer<char> &data, unsigned long long startsector, unsigned long long numsectors, unsigned long long sectorsize)
{
    unsigned long byteswritten;
    BOOL bResult;
    LARGE_INTEGER li;
    li.QuadPart = startsector * sectorsize;
    SetFilePointer(handle, li.LowPart, &li.HighPart, FILE_BEGIN);
    bResult = WriteFile(handle, data.data(), sectorsize * numsectors, &byteswritten, NULL);
    if (!bResult)
    {
        QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Write Error"),
                              QObject::tr("An error occurred when attempting to write data to handle.\n"
                                          "Error %1: %2").arg(GetLastError()).arg(GetErrorMessage() ));
    }
    return (bResult);
}

unsigned long long getNumberOfSectors(HANDLE handle, unsigned long long *sectorsize)
{
    DWORD junk;
    DISK_GEOMETRY_EX diskgeometry;
    BOOL bResult;
    bResult = DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &diskgeometry, sizeof(diskgeometry), &junk, NULL);
    if (!bResult)
    {
        QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Device Error"),
                              QObject::tr("An error occurred when attempting to get the device's geometry.\n"
                                          "Error %1: %2").arg(GetLastError()).arg(GetErrorMessage() ));
        return 0;
    }
    if (sectorsize != NULL)
    {
        *sectorsize = (unsigned long long)diskgeometry.Geometry.BytesPerSector;
    }
    return (unsigned long long)diskgeometry.DiskSize.QuadPart / (unsigned long long)diskgeometry.Geometry.BytesPerSector;
}

unsigned long long getFileSizeInSectors(HANDLE handle, unsigned long long sectorsize)
{
    unsigned long long retVal = 0;
    if (sectorsize) // avoid divide by 0
    {
        LARGE_INTEGER filesize;
        if(GetFileSizeEx(handle, &filesize) == 0)
        {
            QMessageBox::critical(MainWindow::getInstance(), QObject::tr("File Error"),
                                  QObject::tr("An error occurred while getting the file size.\n"
                                              "Error %1: %2").arg(GetLastError()).arg(GetErrorMessage() ));
            retVal = 0;
        }
        else
        {
            retVal = ((unsigned long long)filesize.QuadPart / sectorsize ) + (((unsigned long long)filesize.QuadPart % sectorsize )?1:0);
        }
    }
    return(retVal);
}

bool spaceAvailable(char *location, unsigned long long spaceneeded)
{
    ULARGE_INTEGER freespace;
    BOOL bResult;
    bResult = GetDiskFreeSpaceEx(location, NULL, NULL, &freespace);
    if (!bResult)
    {
        QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Free Space Error"),
                              QObject::tr("Failed to get the free space on drive %1.\n"
                                          "Error %2: %3\n"
                                          "Checking of free space will be skipped.").arg(location).arg(GetLastError()).arg(GetErrorMessage( ) ) );
        return true;
    }
    return (spaceneeded <= freespace.QuadPart);
}

// given a drive letter (ending in a slash), return the label for that drive
// TODO make this more robust by adding input verification
QString getDriveLabel(const char *drv)
{
    QString retVal;
    int szNameBuf = MAX_PATH + 1;
    char *nameBuf = NULL;
    if( (nameBuf = (char *)calloc(szNameBuf, sizeof(char))) != 0 )
    {
        ::GetVolumeInformationA(drv, nameBuf, szNameBuf, NULL,
                                        NULL, NULL, NULL, 0);
    }

    // if malloc fails, nameBuf will be NULL.
    // if GetVolumeInfo fails, nameBuf will contain empty string
    // if all succeeds, nameBuf will contain label
    if(nameBuf == NULL)
    {
        retVal = QString("");
    }
    else
    {
        retVal = QString(nameBuf);
        free(nameBuf);
    }

    return(retVal);
}

BOOL GetDisksProperty(HANDLE hDevice, PSTORAGE_DEVICE_DESCRIPTOR pDevDesc,
                      DEVICE_NUMBER *devInfo)
{
    STORAGE_PROPERTY_QUERY Query; // input param for query
    DWORD dwOutBytes; // IOCTL output length
    BOOL bResult; // IOCTL return val
    BOOL retVal = true;
    DWORD cbBytesReturned;

    // specify the query type
    Query.PropertyId = StorageDeviceProperty;
    Query.QueryType = PropertyStandardQuery;

    // Query using IOCTL_STORAGE_QUERY_PROPERTY
    bResult = ::DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
                &Query, sizeof(STORAGE_PROPERTY_QUERY), pDevDesc,
                pDevDesc->Size, &dwOutBytes, nullptr);
    if (bResult)
    {
        bResult = ::DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER,
                    nullptr, 0, devInfo, sizeof(DEVICE_NUMBER), &dwOutBytes,
                    nullptr);
        if (!bResult)
        {
            retVal = false;
            if( MainWindow::isAlive() )
            {
                QMessageBox::critical(MainWindow::getInstance(), QObject::tr("File Error"),
                                      QObject::tr("An error occurred while getting the device number.\n"
                                                  "This usually means something is currently accessing the device;"
                                                  "please close all applications and try again.\n\nError %1: %2").arg(GetLastError()).arg(GetErrorMessage() ));
            }
        }
    }
    else
    {
        bResult = DeviceIoControl(hDevice, IOCTL_STORAGE_CHECK_VERIFY2, NULL, 0, NULL, 0, &cbBytesReturned,
                            (LPOVERLAPPED) NULL);
        if (bResult && GetLastError() == ERROR_INVALID_FUNCTION)
        {
            retVal = false;
        }
        else
        {
            if( MainWindow::isAlive())
            {
                QMessageBox::critical(MainWindow::getInstance(), QObject::tr("File Error"),
                                      QObject::tr("An error occurred while querying the properties.\n"
                                                  "This usually means something is currently accessing the device;"
                                                  " please close all applications and try again.\n\nError %1: %2").arg(GetLastError()).arg(GetErrorMessage() ) );
            }
        }
            retVal = false;
    }

    return(retVal);
}

// some routines fail if there's no trailing slash in a name,
// 		others fail if there is.  So this routine takes a name (trailing
// 		slash or no), and creates 2 versions - one with the slash, and one w/o
//
// 		CALLER MUST FREE THE 2 RETURNED STRINGS
bool slashify(const std::string &name, std::string &slash, std::string &noSlash)
{
    if ( !name.empty() )
    {
        if ( *name.rbegin() == '\\' )
        {
            slash = name;
            noSlash = name.substr(0, name.size()-1);
        }
        else
        {
            slash = name+'\\';
            noSlash = name;
        }
        return true;
    }
    return false;
}

bool GetMediaType(HANDLE hDevice)
{
    DISK_GEOMETRY diskGeo;
    DWORD cbBytesReturned;
    if (DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY,NULL, 0, &diskGeo, sizeof(diskGeo), &cbBytesReturned, NULL))
    {
        if ((diskGeo.MediaType == FixedMedia) || (diskGeo.MediaType == RemovableMedia))
        {
            return true; // Not a floppy
        }
    }
    return false;
}

bool checkDriveType(const std::string &name, ULONG *pid)
{
    HANDLE hDevice;
    PSTORAGE_DEVICE_DESCRIPTOR pDevDesc;
    DEVICE_NUMBER deviceInfo;
    bool retVal = false;
    std::string nameWithSlash;
    std::string nameNoSlash;
    unsigned int driveType;
    DWORD cbBytesReturned;

    // some calls require no tailing slash, some require a trailing slash...
    if ( !(slashify(name, nameWithSlash, nameNoSlash)) )
    {
        return(retVal);
    }

    driveType = GetDriveType(nameWithSlash.c_str() );
    switch( driveType )
    {
    case DRIVE_REMOVABLE: // The media can be removed from the drive.
    case DRIVE_FIXED:     // The media cannot be removed from the drive. Some USB drives report as this.
        hDevice = CreateFile(nameNoSlash.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, nullptr);
        if (hDevice == INVALID_HANDLE_VALUE)
        {
            if(MainWindow::isAlive() )
            {
                wchar_t *errormessage=nullptr;
                FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, nullptr, GetLastError(), 0, (LPWSTR)&errormessage, 0, nullptr);
                QString errText = QString::fromUtf16((const ushort *)errormessage);
                QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Volume Error"),
                                      QObject::tr("An error occurred when attempting to get a handle on %3.\n"
                                                  "Error %1: %2").arg(GetLastError()).arg(errText).arg(QString::fromStdString(nameWithSlash)));
                LocalFree(errormessage);
            }
        }
        else
        {
            int arrSz = sizeof(STORAGE_DEVICE_DESCRIPTOR) + 512 - 1;
            pDevDesc = (PSTORAGE_DEVICE_DESCRIPTOR)new BYTE[arrSz];
            pDevDesc->Size = arrSz;

            // get the device number if the drive is
            // removable or (fixed AND on the usb bus, SD, or MMC (undefined in XP/mingw))
            if(GetMediaType(hDevice) && GetDisksProperty(hDevice, pDevDesc, &deviceInfo) &&
                    ( ((driveType == DRIVE_REMOVABLE) && (pDevDesc->BusType != BusTypeSata))
                      || ( (driveType == DRIVE_FIXED) && ((pDevDesc->BusType == BusTypeUsb)
                      || (pDevDesc->BusType == BusTypeSd ) || (pDevDesc->BusType == BusTypeMmc )) ) ) )
            {
                // ensure that the drive is actually accessible
                // multi-card hubs were reporting "removable" even when empty
                if(DeviceIoControl(hDevice, IOCTL_STORAGE_CHECK_VERIFY2, NULL, 0, NULL, 0, &cbBytesReturned, (LPOVERLAPPED) NULL))
                {
                    *pid = deviceInfo.DeviceNumber;
                    retVal = true;
                }
                else
                // IOCTL_STORAGE_CHECK_VERIFY2 fails on some devices under XP/Vista, try the other (slower) method, just in case.
                {
                    CloseHandle(hDevice);
                    hDevice = CreateFile(nameNoSlash.c_str(), FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
                    if(DeviceIoControl(hDevice, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, NULL, 0, &cbBytesReturned, (LPOVERLAPPED) NULL))
                    {
                        *pid = deviceInfo.DeviceNumber;
                        retVal = true;
                    }
                }
            }

            delete[] pDevDesc;
            CloseHandle(hDevice);
        }

        break;
    default:
        retVal = false;
    }
    return(retVal);
}
