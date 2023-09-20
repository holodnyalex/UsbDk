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
#include "FilterStrategy.h"
#include "trace.h"
#include "FilterStrategy.tmh"
#include "FilterDevice.h"
#include "ControlDevice.h"
#include "WdfRequest.h"

NTSTATUS CUsbDkFilterStrategy::PNPPreProcess(PIRP Irp)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return WdfDeviceWdmDispatchPreprocessedIrp(m_Owner->WdfObject(), Irp);
}

void CUsbDkFilterStrategy::ForwardRequest(WDFREQUEST Request)
{
    CWdfRequest WdfRequest(Request);

    auto status = WdfRequest.SendAndForget(m_Owner->IOTarget());
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_USBDK_FILTERSTRATEGY, "%!FUNC! failed %!STATUS!", status);
    }
}

void CUsbDkFilterStrategy::IoInCallerContext(WDFDEVICE Device, WDFREQUEST Request)
{
    auto status = WdfDeviceEnqueueRequest(Device, Request);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_USBDK_FILTERSTRATEGY, "%!FUNC! failed to enqueue request: %!STATUS!", status);
        WdfRequestComplete(Request, status);
    }
}

NTSTATUS CUsbDkFilterStrategy::Create(CUsbDkFilterDevice *Owner)
{
    m_Owner = Owner;

    m_ControlDevice = CUsbDkControlDevice::Reference(Owner->GetDriverHandle());
    if (m_ControlDevice == nullptr)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

void CUsbDkFilterStrategy::Delete()
{
    if (m_ControlDevice != nullptr)
    {
        CUsbDkControlDevice::Release();
    }
}
