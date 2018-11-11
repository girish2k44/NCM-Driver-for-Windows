// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#define INITGUID
#include <Modules.Library\DmfModules.Library.h>
#undef INITGUID
#include "trace.h"
#include <netadaptercx.h>

#include "buffers.h"
#include "buffers.tmh"

struct TxBufferRequestPoolEnumContext
{
    size_t BufferSize;
    NTSTATUS enumStatus;
};

PAGED
_Use_decl_annotations_
NTSTATUS TxBufferRequestPoolCreate(
    _In_ WDFDEVICE device,
    _In_ size_t bufferSize,
    _Out_ TX_BUFFER_REQUEST_POOL* handle)
{
    WDF_OBJECT_ATTRIBUTES objectAttributes;
    DMF_MODULE_ATTRIBUTES moduleAttributes;
    DMF_CONFIG_BufferQueue bufferQueueConfig;
    PVOID bufferContext = nullptr;
    DMFMODULE dmfModule;

    *handle = nullptr;

    WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
    objectAttributes.ParentObject = device;

    DMF_CONFIG_BufferQueue_AND_ATTRIBUTES_INIT(&bufferQueueConfig,
                                               &moduleAttributes);

    bufferQueueConfig.SourceSettings.BufferContextSize = 0;
    bufferQueueConfig.SourceSettings.BufferSize = (ULONG) (sizeof(TX_BUFFER_REQUEST) + bufferSize);
    bufferQueueConfig.SourceSettings.BufferCount = 128;
    bufferQueueConfig.SourceSettings.CreateWithTimer = FALSE;
    bufferQueueConfig.SourceSettings.EnableLookAside = FALSE;
    bufferQueueConfig.SourceSettings.PoolType = NonPagedPoolNx;

    NCM_LOG_IF_NOT_NT_SUCCESS_MSG(
        DMF_BufferQueue_Create(device,
                               &moduleAttributes,
                               &objectAttributes,
                               &dmfModule),
        "DMF_BufferQueue_Create failed");

    TX_BUFFER_REQUEST* bufferRequest = nullptr;

    while (STATUS_SUCCESS == DMF_BufferQueue_Fetch(dmfModule,
                                                   (PVOID*) &bufferRequest,
                                                   &bufferContext))
    {
        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = dmfModule;

        bufferRequest->BufferLength = bufferSize;

        NCM_LOG_IF_NOT_NT_SUCCESS_MSG(
            WdfMemoryCreatePreallocated(&attributes,
                                        bufferRequest->Buffer,
                                        bufferRequest->BufferLength,
                                        &bufferRequest->BufferWdfMemory),
            "WdfMemoryCreatePreallocated failed");

        NCM_LOG_IF_NOT_NT_SUCCESS_MSG(
            WdfRequestCreate(&attributes,
                             nullptr,
                             &bufferRequest->Request),
            "WdfRequestCreate failed");

        DMF_BufferQueue_Enqueue(dmfModule,
                                bufferRequest);

    }
    
    *handle = (TX_BUFFER_REQUEST_POOL) dmfModule;

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
TxBufferRequestPoolGetBufferRequest(_In_ TX_BUFFER_REQUEST_POOL handle,
                                    _Outptr_ TX_BUFFER_REQUEST** bufferRequest)
{
    PVOID bufferContext = nullptr;
    return DMF_BufferQueue_Dequeue((DMFMODULE) handle,
                                   (PVOID*) bufferRequest,
                                   &bufferContext);
}

_Use_decl_annotations_
void
TxBufferRequestPoolReturnBufferRequest(_In_ TX_BUFFER_REQUEST_POOL handle,
                                       _In_ TX_BUFFER_REQUEST* bufferRequest)
{
    WDF_REQUEST_REUSE_PARAMS reuseParams = { 0 };

    WDF_REQUEST_REUSE_PARAMS_INIT(&reuseParams,
                                  WDF_REQUEST_REUSE_NO_FLAGS,
                                  STATUS_SUCCESS);

    (void) WdfRequestReuse(bufferRequest->Request, &reuseParams);

    bufferRequest->TransferLength = 0;
    RtlZeroMemory(&bufferRequest->Stats, sizeof(bufferRequest->Stats));

    DMF_BufferQueue_Enqueue((DMFMODULE) handle,
                          bufferRequest);
}


PAGED
_Use_decl_annotations_
NTSTATUS RxBufferQueueCreate(_In_ WDFDEVICE device,
                             _Out_ RX_BUFFER_QUEUE* handle)
{
    WDF_OBJECT_ATTRIBUTES objectAttributes;
    DMF_MODULE_ATTRIBUTES moduleAttributes;
    DMF_CONFIG_BufferQueue bufferQueueConfig;
    DMFMODULE dmfModule;

    *handle = nullptr;

    WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
    objectAttributes.ParentObject = device;

    DMF_CONFIG_BufferQueue_AND_ATTRIBUTES_INIT(&bufferQueueConfig,
                                               &moduleAttributes);

    bufferQueueConfig.SourceSettings.BufferContextSize = 0;
    bufferQueueConfig.SourceSettings.BufferSize = sizeof(RX_BUFFER);
    bufferQueueConfig.SourceSettings.BufferCount = 128;
    bufferQueueConfig.SourceSettings.CreateWithTimer = FALSE;
    bufferQueueConfig.SourceSettings.EnableLookAside = TRUE;
    bufferQueueConfig.SourceSettings.PoolType = NonPagedPoolNx;

    NCM_LOG_IF_NOT_NT_SUCCESS_MSG(
        DMF_BufferQueue_Create(device,
                               &moduleAttributes,
                               &objectAttributes,
                               &dmfModule),
        "DMF_BufferQueue_Create failed");

    *handle = (RX_BUFFER_QUEUE) dmfModule;

    return STATUS_SUCCESS;
}   

_Use_decl_annotations_
NTSTATUS
RxBufferQueueEnqueueBuffer(_In_ RX_BUFFER_QUEUE handle,
                           _In_reads_opt_(bufferSize) PUCHAR buffer,
                           _In_opt_ size_t bufferSize,
                           _In_opt_ WDFMEMORY bufferMemory,
                           _In_opt_ WDFOBJECT returnContext)
{
    RX_BUFFER* rxBuffer;
    PVOID bufferContext = nullptr;

    NTSTATUS status = STATUS_SUCCESS;

    status = DMF_BufferQueue_Fetch((DMFMODULE) handle,
                                   (PVOID*) &rxBuffer,
                                   &bufferContext);

    if (NT_SUCCESS(status))
    {
        if (buffer == nullptr)
        {
            NT_FRE_ASSERT(bufferMemory != nullptr);
            NT_FRE_ASSERT(returnContext == nullptr);

            rxBuffer->UseContinuousRequestTarget = false;
            rxBuffer->ContinuousRequestTarget = nullptr;
            rxBuffer->BufferWdfMemory = bufferMemory;
            WdfObjectReference(rxBuffer->BufferWdfMemory);
            rxBuffer->Buffer = 
                (PUCHAR) WdfMemoryGetBuffer(rxBuffer->BufferWdfMemory,
                                            &rxBuffer->BufferSize);
        }
        else
        {
            NT_FRE_ASSERT(bufferMemory == nullptr);
            NT_FRE_ASSERT(returnContext != nullptr);

            rxBuffer->UseContinuousRequestTarget = true;
            rxBuffer->ContinuousRequestTarget = (DMFMODULE) returnContext;
            rxBuffer->BufferWdfMemory = nullptr;
            rxBuffer->Buffer = buffer;
            rxBuffer->BufferSize = bufferSize;
        }

        DMF_BufferQueue_Enqueue((DMFMODULE) handle,
                                rxBuffer);
    }

    return status;
}

_Use_decl_annotations_
NTSTATUS
RxBufferQueueDequeueBuffer(_In_ RX_BUFFER_QUEUE handle,
                           _Outptr_ RX_BUFFER** rxBuffer)
{
    PVOID bufferContext = nullptr;

    NTSTATUS status = DMF_BufferQueue_Dequeue((DMFMODULE) handle,
                                              (PVOID*) rxBuffer,
                                              &bufferContext);

    return status;
}

_Use_decl_annotations_
void
RxBufferQueueReturnBuffer(_In_ RX_BUFFER_QUEUE handle,
                          _In_ RX_BUFFER* rxBuffer)
{
    if (rxBuffer->UseContinuousRequestTarget)
    {
        DMF_ContinuousRequestTarget_BufferPut(
            (DMFMODULE) rxBuffer->ContinuousRequestTarget,
            rxBuffer->Buffer);
    }
    else
    {
        WdfObjectDereference(rxBuffer->BufferWdfMemory);
    }

    DMF_BufferQueue_Reuse((DMFMODULE) handle,
                          rxBuffer);
}

