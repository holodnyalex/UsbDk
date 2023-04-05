/**********************************************************************
* Copyright (c) 2013-2014  Red Hat, Inc.
*
* Developed by Daynix Computing LTD.
*
* Authors:
*     Dmitry Fleytman <dmitry@daynix.com>
*     Pavel Gurvich <pavel@daynix.com>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
**********************************************************************/

#include "stdafx.h"
#include "Installer.h"
#include "Public.h"
#include "DeviceMgr.h"
#include <iterator>


#define SYSTEM32_DRIVERS    TEXT("\\System32\\Drivers\\")

#define UPPER_FILTER_REGISTRY_SUBTREE TEXT("System\\CurrentControlSet\\Control\\Class\\{36FC9E60-C465-11CF-8056-444553540000}\\")
#define UPPER_FILTER_REGISTRY_KEY TEXT("UpperFilters")

using namespace std;

UsbDkInstaller::UsbDkInstaller()
{
    validatePlatform();

    m_regAccess.SetPrimaryKey(HKEY_LOCAL_MACHINE);
}

bool UsbDkInstaller::Install(bool &NeedRollBack)
{
    NeedRollBack = false;
    auto driverLocation = CopyDriver();
    NeedRollBack = true;
    auto infFilePath = buildInfFilePath();

#if USE_WDFCOINSTALLER
    auto rebootRequired = !m_wdfCoinstaller.PreDeviceInstallEx(infFilePath);
#else   // !USE_WDFCOINSTALLER
    auto rebootRequired = false;
#endif  // USE_WDFCOINSTALLER

    m_scManager.CreateServiceObject(USBDK_DRIVER_NAME, driverLocation.c_str());

    verifyDriverCanStart();

#if USE_WDFCOINSTALLER
    m_wdfCoinstaller.PostDeviceInstall(infFilePath);
#endif  // USE_WDFCOINSTALLER
    addUsbDkToRegistry();

    return rebootRequired ? false : DeviceMgr::ResetDeviceByClass(GUID_DEVINTERFACE_USB_HOST_CONTROLLER);
}

void UsbDkInstaller::Uninstall()
{
    removeUsbDkFromRegistry();

    DeviceMgr::ResetDeviceByClass(GUID_DEVINTERFACE_USB_HOST_CONTROLLER);

    DeleteDriver();

    auto infFilePath = buildInfFilePath();

#if USE_WDFCOINSTALLER
    m_wdfCoinstaller.PreDeviceRemove(infFilePath);
#endif  // USE_WDFCOINSTALLER

    m_scManager.DeleteServiceObject(USBDK_DRIVER_NAME);

#if USE_WDFCOINSTALLER
    m_wdfCoinstaller.PostDeviceRemove(infFilePath);
#endif  // USE_WDFCOINSTALLER
}

tstring UsbDkInstaller::CopyDriver()
{
    TCHAR currDirectory[MAX_PATH];
    if (!GetCurrentDirectory(MAX_PATH, currDirectory))
    {
        throw UsbDkInstallerFailedException(TEXT("GetCurrentDirectory failed!"));
    }

    tstring driverOrigLocationStr(tstring(currDirectory) + TEXT("\\viusbdk\\") USBDK_DRIVER_FILE_NAME);

    auto driverDestLocation = buildDriverPath(USBDK_DRIVER_FILE_NAME);

    if (!CopyFile(driverOrigLocationStr.c_str(), driverDestLocation.c_str(), TRUE))
    {
        throw UsbDkInstallerFailedException(tstring(TEXT("CopyFile from ")) + driverOrigLocationStr + TEXT(" to ") + driverDestLocation + TEXT(" failed."));
    }

    return driverDestLocation;
}

void UsbDkInstaller::DeleteDriver()
{
    auto driverDestLocation = buildDriverPath(USBDK_DRIVER_FILE_NAME);

    if (!DeleteFile(driverDestLocation.c_str()))
    {
        auto errCode = GetLastError();
        if (errCode != ERROR_FILE_NOT_FOUND)
        {
            throw UsbDkInstallerFailedException(TEXT("DeleteFile failed."), errCode);
        }
        return;
    }
}

tstring UsbDkInstaller::buildDriverPath(const tstring &DriverFileName)
{
    TCHAR    driverDestPath[MAX_PATH];
    GetWindowsDirectory(driverDestPath, MAX_PATH);

    return tstring(driverDestPath) + SYSTEM32_DRIVERS + DriverFileName;
}

tstring UsbDkInstaller::buildInfFilePath()
{
    TCHAR currDir[MAX_PATH];
    if (GetCurrentDirectory(MAX_PATH, currDir) == 0)
    {
        throw UsbDkInstallerFailedException(TEXT("GetCurrentDirectory failed!"));
    }

    return tstring(currDir) + TEXT("\\viusbdk\\") + USBDK_DRIVER_INF_NAME;
}

void UsbDkInstaller::addUsbDkToRegistry()
{
    LPCTSTR upperFilterString = UPPER_FILTER_REGISTRY_KEY;
    LPCTSTR upperFiltersKeyStr = UPPER_FILTER_REGISTRY_SUBTREE;

    // check for value size
    DWORD valLen = 0;
    auto errCode = m_regAccess.ReadMultiString(upperFilterString, nullptr, 0, valLen, upperFiltersKeyStr);

    if (errCode != ERROR_FILE_NOT_FOUND && errCode != ERROR_SUCCESS)
    {
        throw UsbDkInstallerFailedException(TEXT("addUsbDkToRegistry failed in ReadMultiString!"), errCode);
    }

    vector<TCHAR> valVector;
    tstringlist newfiltersList;
    if (valLen)
    {
        // get the value
        valVector.resize(valLen);
        errCode = m_regAccess.ReadMultiString(upperFilterString, &valVector[0], valLen, valLen, upperFiltersKeyStr);

        if (errCode != ERROR_FILE_NOT_FOUND && errCode != ERROR_SUCCESS)
        {
            throw UsbDkInstallerFailedException(TEXT("addUsbDkToRegistry failed in ReadMultiString!"), errCode);
        }

        tstringlist filtersList;
        buildStringListFromVector(filtersList, valVector);
        buildNewListWithoutEement(newfiltersList, filtersList, USBDK_DRIVER_NAME);
    }

    newfiltersList.push_back(USBDK_DRIVER_NAME);

    valVector.clear();
    buildMultiStringVectorFromList(valVector, newfiltersList);

    // set new value to registry
    if (!m_regAccess.WriteMultiString(upperFilterString,
                                      &valVector[0],
                                      static_cast<DWORD>(sizeof(TCHAR) * valVector.size()),
                                      upperFiltersKeyStr))
    {
        throw UsbDkInstallerFailedException(TEXT("addUsbDkToRegistry failed in WriteMultiString."));
    }
}

void UsbDkInstaller::removeUsbDkFromRegistry()
{
    // If key exists, and value (Multiple string) includes "UsbDk", remove it from value.

    LPCTSTR upperFilterString = UPPER_FILTER_REGISTRY_KEY;
    LPCTSTR upperFiltersKeyStr = UPPER_FILTER_REGISTRY_SUBTREE;

    // check for value size
    DWORD valLen = 0;
    auto errCode = m_regAccess.ReadMultiString(upperFilterString, nullptr, 0, valLen, upperFiltersKeyStr);

    if (errCode != ERROR_FILE_NOT_FOUND && errCode != ERROR_SUCCESS)
    {
        throw UsbDkInstallerFailedException(TEXT("addUsbDkToRegistry failed in ReadMultiString!"), errCode);
    }

    if (valLen)
    {
        // get the value
        vector<TCHAR> valVector(valLen);
        errCode = m_regAccess.ReadMultiString(upperFilterString, &valVector[0], valLen, valLen, upperFiltersKeyStr);

        if (errCode != ERROR_FILE_NOT_FOUND && errCode != ERROR_SUCCESS)
        {
            throw UsbDkInstallerFailedException(TEXT("addUsbDkToRegistry failed in ReadMultiString!"), errCode);
        }

        if (!valLen)
        {
            return;
        }

        tstringlist filtersList;
        buildStringListFromVector(filtersList, valVector);

        tstringlist newfiltersList;
        buildNewListWithoutEement(newfiltersList, filtersList, USBDK_DRIVER_NAME);

        valVector.clear();
        buildMultiStringVectorFromList(valVector, newfiltersList);

        // set new value to registry
        if (!m_regAccess.WriteMultiString(upperFilterString,
                                          &valVector[0],
                                          static_cast<DWORD>(sizeof(TCHAR) * valVector.size()),
                                          upperFiltersKeyStr))
        {
            return;
        }
    }
}

void UsbDkInstaller::buildMultiStringVectorFromList(vector<TCHAR> &valVector, tstringlist &newfiltersList)
{
    for (auto filter : newfiltersList)
    {
        std::copy(filter.begin(), filter.end(), std::back_inserter(valVector));
        valVector.push_back(TEXT('\0'));
    }
    if (valVector.empty())
    {
        valVector.push_back(TEXT('\0'));
    }
    valVector.push_back(TEXT('\0'));
}

void UsbDkInstaller::buildStringListFromVector(tstringlist &filtersList, vector<TCHAR> &valVector)
{
    tstring currFilter;
    tstring::size_type currPos = 0;

    do
    {
        currFilter = &valVector[currPos];

        if (!currFilter.empty())
        {
            filtersList.push_back(currFilter);
        }

        currPos += currFilter.size() + 1;
        if (currPos >= valVector.size())
        {
            break;
        }

    } while (!currFilter.empty());
}

void UsbDkInstaller::buildNewListWithoutEement(tstringlist &newfiltersList, tstringlist &filtersList, tstring element)
{
    for (auto filter : filtersList)
    {
        if (filter != USBDK_DRIVER_NAME)
        {
            newfiltersList.push_back(filter);
        }
    }
}

void UsbDkInstaller::validatePlatform()
{
    if (isWow64B())
    {
        throw UsbDkInstallerFailedException(TEXT("Running 32Bit package on 64Bit OS not supported."),
                                            ERROR_EXE_MACHINE_TYPE_MISMATCH);
    }
}

bool UsbDkInstaller::isWow64B()
{
    BOOL bIsWow64 = FALSE;

    typedef BOOL(WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);

    LPFN_ISWOW64PROCESS  fnIsWow64Process = reinterpret_cast<LPFN_ISWOW64PROCESS>(GetProcAddress(GetModuleHandle(TEXT("kernel32")), "IsWow64Process"));
    if (nullptr != fnIsWow64Process)
    {
        if (!fnIsWow64Process(GetCurrentProcess(), &bIsWow64))
        {
            return false;
        }
    }
    return bIsWow64 ? true : false;
}

void UsbDkInstaller::verifyDriverCanStart()
{
    try
    {
        m_scManager.StartServiceObject(USBDK_DRIVER_NAME);
        m_scManager.StopServiceObject(USBDK_DRIVER_NAME);
    }
    catch (const UsbDkServiceManagerFailedException &e)
    {
        auto err = e.GetErrorCode();
       /* ERROR_SERVICE_DISABLED occurs in case we are attempting to start the
        * driver without associating it with a device.
        */
        if (err != ERROR_SERVICE_DISABLED)
        {
            try
            {
                m_scManager.DeleteServiceObject(USBDK_DRIVER_NAME);
            }
            catch (const exception &e)
            {
                UNREFERENCED_PARAMETER(e);
            }
            throw UsbDkInstallerAbortedException();
        }
    }
}
