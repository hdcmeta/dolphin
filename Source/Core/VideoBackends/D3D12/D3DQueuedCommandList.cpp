// Copyright hdcmeta
// Dual-Licensed under MIT and GPLv2+
// Refer to the license.txt/license_mit.txt files included.

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DQueuedCommandList.h"

namespace DX12
{
#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DQueuedCommandList.h"


	DWORD WINAPI ID3D12QueuedCommandList::BackgroundThreadFunction(LPVOID lpParam)
	{
		ID3D12QueuedCommandList* parentQueuedCommandList = static_cast<ID3D12QueuedCommandList*>(lpParam);
		ID3D12GraphicsCommandList* pCommandList = parentQueuedCommandList->_pCommandList;

		byte* queueArray = parentQueuedCommandList->_queueArray;

		while (true)
		{
			WaitForSingleObject(parentQueuedCommandList->_hBeginExecutionEvent, INFINITE);

			UINT queueArrayFront = parentQueuedCommandList->_queueArrayFront;

			byte *item = &queueArray[queueArrayFront];

			while (true)
			{
				switch (reinterpret_cast<D3DQueueItem*>(item)->Type)
				{
					case D3DQueueItemType::ClearDepthStencilView:
					{
						pCommandList->ClearDepthStencilView(reinterpret_cast<D3DQueueItem*>(item)->ClearDepthStencilView.DepthStencilView, D3D12_CLEAR_FLAG_DEPTH, 0.f, 0, NULL, 0);
						
						item += sizeof(ClearDepthStencilViewArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::ClearRenderTargetView:
					{
						float clearColor[4] = { 0.f, 0.f, 0.f, 1.f };
						pCommandList->ClearRenderTargetView(reinterpret_cast<D3DQueueItem*>(item)->ClearRenderTargetView.RenderTargetView, clearColor, NULL, 0);
						
						item += sizeof(ClearRenderTargetViewArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::CopyBufferRegion:
					{

						pCommandList->CopyBufferRegion(
							reinterpret_cast<D3DQueueItem*>(item)->CopyBufferRegion.pDstBuffer,
							reinterpret_cast<D3DQueueItem*>(item)->CopyBufferRegion.DstOffset,
							reinterpret_cast<D3DQueueItem*>(item)->CopyBufferRegion.pSrcBuffer,
							reinterpret_cast<D3DQueueItem*>(item)->CopyBufferRegion.SrcOffset,
							reinterpret_cast<D3DQueueItem*>(item)->CopyBufferRegion.NumBytes
							);

						item += sizeof(CopyBufferRegionArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::CopyTextureRegion:
					{
						// If box is completely empty, assume that the original API call has a NULL box (which means
						// copy from the entire resource.

						D3D12_BOX *pSrcBox = &reinterpret_cast<D3DQueueItem*>(item)->CopyTextureRegion.srcBox;

						// Front/Back never used, so don't need to check.
						bool emptyBox =
							pSrcBox->bottom == 0 &&
							pSrcBox->left == 0 &&
							pSrcBox->right == 0 &&
							pSrcBox->top == 0;

						pCommandList->CopyTextureRegion(
							&reinterpret_cast<D3DQueueItem*>(item)->CopyTextureRegion.dst,
							0, // item->CopyTextureRegion.DstX, (always zero, checked at command enqueing time)
							0, // item->CopyTextureRegion.DstY, (always zero, checked at command enqueing time)
							0, // item->CopyTextureRegion.DstZ, (always zero, checked at command enqueing time)
							&reinterpret_cast<D3DQueueItem*>(item)->CopyTextureRegion.src,
							emptyBox ?
							  nullptr : pSrcBox
							);

						item += sizeof(CopyTextureRegionArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::DrawIndexedInstanced:
					{
						pCommandList->DrawIndexedInstanced(
							reinterpret_cast<D3DQueueItem*>(item)->DrawIndexedInstanced.IndexCount,
							1,
							reinterpret_cast<D3DQueueItem*>(item)->DrawIndexedInstanced.StartIndexLocation,
							reinterpret_cast<D3DQueueItem*>(item)->DrawIndexedInstanced.BaseVertexLocation,
							0
							);

						item += sizeof(DrawIndexedInstancedArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::DrawInstanced:
					{
						pCommandList->DrawInstanced(
							reinterpret_cast<D3DQueueItem*>(item)->DrawInstanced.VertexCount,
							1,
							reinterpret_cast<D3DQueueItem*>(item)->DrawInstanced.StartVertexLocation,
							0
							);

						item += sizeof(DrawInstancedArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::IASetPrimitiveTopology:
					{
						pCommandList->IASetPrimitiveTopology(reinterpret_cast<D3DQueueItem*>(item)->IASetPrimitiveTopology.PrimitiveTopology);

						item += sizeof(IASetPrimitiveTopologyArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::ResourceBarrier:
					{
						pCommandList->ResourceBarrier(1, &reinterpret_cast<D3DQueueItem*>(item)->ResourceBarrier.barrier);

						item += sizeof(ResourceBarrierArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::RSSetScissorRects:
					{
						D3D12_RECT rect = {
							reinterpret_cast<D3DQueueItem*>(item)->RSSetScissorRects.left,
							reinterpret_cast<D3DQueueItem*>(item)->RSSetScissorRects.top,
							reinterpret_cast<D3DQueueItem*>(item)->RSSetScissorRects.right,
							reinterpret_cast<D3DQueueItem*>(item)->RSSetScissorRects.bottom
						};

						pCommandList->RSSetScissorRects(1, &rect);
						item += sizeof(RSSetScissorRectsArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::RSSetViewports:
					{
						D3D12_VIEWPORT viewport = {
							reinterpret_cast<D3DQueueItem*>(item)->RSSetViewports.TopLeftX,
							reinterpret_cast<D3DQueueItem*>(item)->RSSetViewports.TopLeftY,
							reinterpret_cast<D3DQueueItem*>(item)->RSSetViewports.Width,
							reinterpret_cast<D3DQueueItem*>(item)->RSSetViewports.Height,
							D3D11_MIN_DEPTH,
							D3D11_MAX_DEPTH
						};

						pCommandList->RSSetViewports(1, &viewport);
						item += sizeof(RSSetViewportsArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::SetDescriptorHeaps:
					{
						pCommandList->SetDescriptorHeaps(
							reinterpret_cast<D3DQueueItem*>(item)->SetDescriptorHeaps.NumDescriptorHeaps,
							reinterpret_cast<D3DQueueItem*>(item)->SetDescriptorHeaps.ppDescriptorHeap
							);

						item += sizeof(SetDescriptorHeapsArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::SetGraphicsRootConstantBufferView:
					{
						pCommandList->SetGraphicsRootConstantBufferView(
							reinterpret_cast<D3DQueueItem*>(item)->SetGraphicsRootConstantBufferView.RootParameterIndex,
							reinterpret_cast<D3DQueueItem*>(item)->SetGraphicsRootConstantBufferView.BufferLocation
							);

						item += sizeof(SetGraphicsRootConstantBufferViewArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::SetGraphicsRootDescriptorTable:
					{
						pCommandList->SetGraphicsRootDescriptorTable(
							reinterpret_cast<D3DQueueItem*>(item)->SetGraphicsRootDescriptorTable.RootParameterIndex,
							reinterpret_cast<D3DQueueItem*>(item)->SetGraphicsRootDescriptorTable.BaseDescriptor
							);

						item += sizeof(SetGraphicsRootDescriptorTableArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::SetGraphicsRootSignature:
					{
						pCommandList->SetGraphicsRootSignature(
							reinterpret_cast<D3DQueueItem*>(item)->SetGraphicsRootSignature.pRootSignature
							);

						item += sizeof(SetGraphicsRootSignatureArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::SetIndexBuffer:
					{
						pCommandList->IASetIndexBuffer(
							&reinterpret_cast<D3DQueueItem*>(item)->SetIndexBuffer.desc
							);

						item += sizeof(SetIndexBufferArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::SetVertexBuffers:
					{
						pCommandList->IASetVertexBuffers(
							0,
							1,
							&reinterpret_cast<D3DQueueItem*>(item)->SetVertexBuffers.desc
							);

						item += sizeof(SetVertexBuffersArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::SetPipelineState:
					{
						pCommandList->SetPipelineState(reinterpret_cast<D3DQueueItem*>(item)->SetPipelineState.pPipelineStateObject);
						item += sizeof(SetPipelineStateArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::SetRenderTargets:
					{
						pCommandList->OMSetRenderTargets(
							1,
							&reinterpret_cast<D3DQueueItem*>(item)->SetRenderTargets.RenderTargetDescriptor,
							FALSE,
							reinterpret_cast<D3DQueueItem*>(item)->SetRenderTargets.DepthStencilDescriptor.ptr == NULL ?
							nullptr :
							&reinterpret_cast<D3DQueueItem*>(item)->SetRenderTargets.DepthStencilDescriptor
							);

						item += sizeof(SetRenderTargetsArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::ResolveSubresource:
					{
						pCommandList->ResolveSubresource(
							reinterpret_cast<D3DQueueItem*>(item)->ResolveSubresource.pDstResource,
							reinterpret_cast<D3DQueueItem*>(item)->ResolveSubresource.DstSubresource,
							reinterpret_cast<D3DQueueItem*>(item)->ResolveSubresource.pSrcResource,
							reinterpret_cast<D3DQueueItem*>(item)->ResolveSubresource.SrcSubresource,
							reinterpret_cast<D3DQueueItem*>(item)->ResolveSubresource.Format
							);

						item += sizeof(ResolveSubresourceArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::CloseCommandList:
					{
						CheckHR(pCommandList->Close());

						item += sizeof(CloseCommandListArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::ExecuteCommandList:
					{
						parentQueuedCommandList->_pCommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&pCommandList));

						item += sizeof(ExecuteCommandListArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::Present:
					{
						CheckHR(reinterpret_cast<D3DQueueItem*>(item)->Present.pSwapChain->Present(reinterpret_cast<D3DQueueItem*>(item)->Present.SyncInterval, reinterpret_cast<D3DQueueItem*>(item)->Present.Flags));
						
						item += sizeof(PresentArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::ResetCommandList:
					{
						CheckHR(pCommandList->Reset(reinterpret_cast<D3DQueueItem*>(item)->ResetCommandList.pAllocator, nullptr));
						
						item += sizeof(ResetCommandListArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::ResetCommandAllocator:
					{
						CheckHR(reinterpret_cast<D3DQueueItem*>(item)->ResetCommandAllocator.pAllocator->Reset());
						
						item += sizeof(ResetCommandAllocatorArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::FenceGpuSignal:
					{
						CheckHR(parentQueuedCommandList->_pCommandQueue->Signal(reinterpret_cast<D3DQueueItem*>(item)->FenceGpuSignal.pFence, reinterpret_cast<D3DQueueItem*>(item)->FenceGpuSignal.fenceValue));
						
						item += sizeof(FenceGpuSignalArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}
				
					case D3DQueueItemType::FenceCpuSignal:
					{
						CheckHR(reinterpret_cast<D3DQueueItem*>(item)->FenceCpuSignal.pFence->Signal(reinterpret_cast<D3DQueueItem*>(item)->FenceCpuSignal.fenceValue));
						
						item += sizeof(FenceCpuSignalArguments) + sizeof(D3DQueueItemType) * 2;
						break;
					}

					case D3DQueueItemType::Stop:

						// Use a goto to break out of the loop, since we can't exit the loop from
						// within a switch statement. We could use a separate 'if' after the switch,
						// but that was the highest source of overhead in the function after profiling.
						// http://stackoverflow.com/questions/1420029/how-to-break-out-of-a-loop-from-inside-a-switch

						item += sizeof(D3DQueueItemType) * 2;

						if (item - queueArray > s_queueArraySize / 3)
						{
							item = queueArray;
						}

						goto exitLoop;
				}
			}

			exitLoop:

			parentQueuedCommandList->_queueArrayFront = static_cast<UINT>(item - queueArray);
		}
	}

	ID3D12QueuedCommandList::ID3D12QueuedCommandList(ID3D12GraphicsCommandList* backingCommandList, ID3D12CommandQueue* backingCommandQueue) :
		_pCommandList(backingCommandList),
		_pCommandQueue(backingCommandQueue),
		_ref(1)
	{
		memset(_queueArray, 0, sizeof(_queueArray));

		_queueArrayBack = _queueArray;
		_queueArrayFront = 0;

		_hBeginExecutionEvent = CreateSemaphore(NULL, 0, 256, NULL);

		_hBackgroundThread = CreateThread(NULL, 0, BackgroundThreadFunction, this, 0, &_backgroundThreadId);
}

	ID3D12QueuedCommandList::~ID3D12QueuedCommandList()
	{
		TerminateThread(_hBackgroundThread, 0);
		CloseHandle(_hBackgroundThread);

		CloseHandle(_hBeginExecutionEvent);
	}

	void ID3D12QueuedCommandList::QueueExecute()
	{
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::ExecuteCommandList;

		_queueArrayBack += sizeof(ExecuteCommandListArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void ID3D12QueuedCommandList::QueueFenceGpuSignal(ID3D12Fence *pFenceToSignal, UINT64 fenceValue)
	{
		D3DQueueItem item = {};

		item.Type = D3DQueueItemType::FenceGpuSignal;
		item.FenceGpuSignal.pFence = pFenceToSignal;
		item.FenceGpuSignal.fenceValue = fenceValue;

		*reinterpret_cast<D3DQueueItem*>(_queueArrayBack) = item;

		_queueArrayBack += sizeof(FenceGpuSignalArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void ID3D12QueuedCommandList::QueueFenceCpuSignal(ID3D12Fence *pFenceToSignal, UINT64 fenceValue)
	{
		D3DQueueItem item = {};

		item.Type = D3DQueueItemType::FenceCpuSignal;
		item.FenceCpuSignal.pFence = pFenceToSignal;
		item.FenceCpuSignal.fenceValue = fenceValue;

		*reinterpret_cast<D3DQueueItem*>(_queueArrayBack) = item;

		_queueArrayBack += sizeof(FenceCpuSignalArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void ID3D12QueuedCommandList::QueuePresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
	{
		D3DQueueItem item = {};

		item.Type = D3DQueueItemType::Present;
		item.Present.pSwapChain = pSwapChain;
		item.Present.Flags = Flags;
		item.Present.SyncInterval = SyncInterval;

		*reinterpret_cast<D3DQueueItem*>(_queueArrayBack) = item;

		_queueArrayBack += sizeof(PresentArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void ID3D12QueuedCommandList::ClearQueue()
	{
		// Drain semaphore to ensure no new previously queued work executes (though inflight work may continue).
		while (WaitForSingleObject(_hBeginExecutionEvent, 0) != WAIT_TIMEOUT) { } 
		
		// Assume that any inflight queued work will complete within 100ms. This is a safe assumption.
		Sleep(100); 

		memset(_queueArray, 0, sizeof(_queueArray));

		_queueArrayBack = _queueArray;
		_queueArrayFront = 0;
	}

	void ID3D12QueuedCommandList::ProcessQueuedItems()
	{
		D3DQueueItem item = {};

		item.Type = D3DQueueItemType::Stop;
		*reinterpret_cast<D3DQueueItem*>(_queueArrayBack) = item;

		_queueArrayBack += sizeof(D3DQueueItemType) * 2;

		if (_queueArrayBack - _queueArray > s_queueArraySize / 3)
		{
			_queueArrayBack = _queueArray;
		}

		ReleaseSemaphore(_hBeginExecutionEvent, 1, NULL);
	}

	ULONG ID3D12QueuedCommandList::AddRef()
	{
		return ++_ref;
	}

	ULONG ID3D12QueuedCommandList::Release()
	{
		ULONG ref = --_ref;
		if (!ref)
		{
			delete this;
		}

		return ref;
	}

	HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::QueryInterface(
		_In_ REFIID riid,
		_COM_Outptr_ void** ppvObject
		)
	{
		*ppvObject = nullptr;
		HRESULT hr = S_OK;

		if (riid == __uuidof(ID3D12GraphicsCommandList))
		{
			*ppvObject = reinterpret_cast<ID3D12GraphicsCommandList*>(this);
	}
		else  if (riid == __uuidof(ID3D12CommandList))
		{
			*ppvObject = reinterpret_cast<ID3D12CommandList*>(this);
		}
		else if (riid == __uuidof(ID3D12DeviceChild))
		{
			*ppvObject = reinterpret_cast<ID3D12DeviceChild*>(this);
		}
		else if (riid == __uuidof(ID3D12Object))
		{
			*ppvObject = reinterpret_cast<ID3D12Object*>(this);
		}
		else
		{
			hr = E_NOINTERFACE;
		}

		if (*ppvObject != nullptr)
		{
			AddRef();
		}

		return hr;
		}

	// ID3D12Object

	HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::GetPrivateData(
		_In_  REFGUID guid,
		_Inout_  UINT *pDataSize,
		_Out_writes_bytes_opt_(*pDataSize)  void *pData
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
		return E_FAIL;
	}

	HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::SetPrivateData(
		_In_  REFGUID guid,
		_In_  UINT DataSize,
		_In_reads_bytes_opt_(DataSize)  const void *pData
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
		return E_FAIL;
	}

	HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::SetPrivateDataInterface(
		_In_  REFGUID guid,
		_In_opt_  const IUnknown *pData
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
		return E_FAIL;
	}

	HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::SetName(
		_In_z_  LPCWSTR pName
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
		return E_FAIL;
	}

	// ID3D12DeviceChild

	D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE ID3D12QueuedCommandList::GetType()
	{
		return D3D12_COMMAND_LIST_TYPE_DIRECT;
	}

	// ID3D12CommandList

	HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::GetDevice(
		REFIID riid,
		_Out_  void **ppDevice
		)
	{
		return _pCommandList->GetDevice(riid, ppDevice);
	}

	HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::Close() {

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::CloseCommandList;

		_queueArrayBack += sizeof(CloseCommandListArguments) + sizeof(D3DQueueItemType) * 2;

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::Reset(
		_In_  ID3D12CommandAllocator *pAllocator,
		_In_opt_  ID3D12PipelineState *pInitialState
		)
	{
		DEBUGCHECK(pInitialState == nullptr, "Error: Invalid assumption in ID3D12QueuedCommandList.");

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::ResetCommandList;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->ResetCommandList.pAllocator = pAllocator;
		
		_queueArrayBack += sizeof(ResetCommandListArguments) + sizeof(D3DQueueItemType) * 2;

		return S_OK;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::ClearState(
		_In_  ID3D12PipelineState *pPipelineState
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::DrawInstanced(
		_In_  UINT VertexCountPerInstance,
		_In_  UINT InstanceCount,
		_In_  UINT StartVertexLocation,
		_In_  UINT StartInstanceLocation
		)
	{
		DEBUGCHECK(InstanceCount == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(StartInstanceLocation == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::DrawInstanced;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->DrawInstanced.StartVertexLocation = StartVertexLocation;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->DrawInstanced.VertexCount = VertexCountPerInstance;

		_queueArrayBack += sizeof(DrawInstancedArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::DrawIndexedInstanced(
		_In_  UINT IndexCountPerInstance,
		_In_  UINT InstanceCount,
		_In_  UINT StartIndexLocation,
		_In_  INT BaseVertexLocation,
		_In_  UINT StartInstanceLocation
		)
	{
		DEBUGCHECK(InstanceCount == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(StartInstanceLocation == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");

		D3DQueueItem* item = reinterpret_cast<D3DQueueItem*>(_queueArrayBack);

		item->Type = D3DQueueItemType::DrawIndexedInstanced;
		item->DrawIndexedInstanced.BaseVertexLocation = BaseVertexLocation;
		item->DrawIndexedInstanced.IndexCount = IndexCountPerInstance;
		item->DrawIndexedInstanced.StartIndexLocation = StartIndexLocation;

		_queueArrayBack += sizeof(DrawIndexedInstancedArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::Dispatch(
		_In_  UINT ThreadGroupCountX,
		_In_  UINT ThreadGroupCountY,
		_In_  UINT ThreadGroupCountZ
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::DispatchIndirect(
		_In_  ID3D12Resource *pBufferForArgs,
		_In_  UINT AlignedByteOffsetForArgs
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::CopyBufferRegion(
		_In_  ID3D12Resource *pDstBuffer,
		UINT64 DstOffset,
		_In_  ID3D12Resource *pSrcBuffer,
		UINT64 SrcOffset,
		UINT64 NumBytes
		)
	{
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::CopyBufferRegion;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->CopyBufferRegion.pDstBuffer = pDstBuffer;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->CopyBufferRegion.DstOffset = static_cast<UINT>(DstOffset);
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->CopyBufferRegion.pSrcBuffer = pSrcBuffer;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->CopyBufferRegion.SrcOffset = static_cast<UINT>(SrcOffset);
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->CopyBufferRegion.NumBytes = static_cast<UINT>(NumBytes);

		_queueArrayBack += sizeof(CopyBufferRegionArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::CopyTextureRegion(
		_In_  const D3D12_TEXTURE_COPY_LOCATION *pDst,
		UINT DstX,
		UINT DstY,
		UINT DstZ,
		_In_  const D3D12_TEXTURE_COPY_LOCATION *pSrc,
		_In_opt_  const D3D12_BOX *pSrcBox
		)
	{
		DEBUGCHECK(DstX == 0 && DstY == 0 && DstZ == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::CopyTextureRegion;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->CopyTextureRegion.dst = *pDst;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->CopyTextureRegion.src = *pSrc;

		if (pSrcBox)
			reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->CopyTextureRegion.srcBox = *pSrcBox;
		else
			reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->CopyTextureRegion.srcBox = {};

		_queueArrayBack += sizeof(CopyTextureRegionArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::CopyResource(
		_In_  ID3D12Resource *pDstResource,
		_In_  ID3D12Resource *pSrcResource
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::CopyTiles(
		_In_  ID3D12Resource *pTiledResource,
		_In_  const D3D12_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate,
		_In_  const D3D12_TILE_REGION_SIZE *pTileRegionSize,
		_In_  ID3D12Resource *pBuffer,
		UINT64 BufferStartOffsetInBytes,
		D3D12_TILE_COPY_FLAGS Flags
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::ResolveSubresource(
		_In_  ID3D12Resource *pDstResource,
		_In_  UINT DstSubresource,
		_In_  ID3D12Resource *pSrcResource,
		_In_  UINT SrcSubresource,
		_In_  DXGI_FORMAT Format
		)
	{
		// No ignored parameters, no assumptions to DEBUGCHECK.

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::ResolveSubresource;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->ResolveSubresource.pDstResource = pDstResource;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->ResolveSubresource.DstSubresource = DstSubresource;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->ResolveSubresource.pSrcResource = pSrcResource;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->ResolveSubresource.SrcSubresource = SrcSubresource;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->ResolveSubresource.Format = Format;

		_queueArrayBack += sizeof(ResolveSubresourceArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::IASetPrimitiveTopology(
		_In_  D3D11_PRIMITIVE_TOPOLOGY PrimitiveTopology
		)
	{
		// No ignored parameters, no assumptions to DEBUGCHECK.

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::IASetPrimitiveTopology;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->IASetPrimitiveTopology.PrimitiveTopology = PrimitiveTopology;

		_queueArrayBack += sizeof(IASetPrimitiveTopologyArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::RSSetViewports(
		_In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT Count,
		_In_reads_(Count)  const D3D12_VIEWPORT *pViewports
		)
	{
		DEBUGCHECK(Count == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::RSSetViewports;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->RSSetViewports.Height = pViewports->Height;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->RSSetViewports.Width = pViewports->Width;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->RSSetViewports.TopLeftX = pViewports->TopLeftX;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->RSSetViewports.TopLeftY = pViewports->TopLeftY;

		_queueArrayBack += sizeof(RSSetViewportsArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::RSSetScissorRects(
		_In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT Count,
		_In_reads_(Count)  const D3D12_RECT *pRects
		)
	{
		DEBUGCHECK(Count == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::RSSetScissorRects;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->RSSetScissorRects.bottom = pRects->bottom;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->RSSetScissorRects.left = pRects->left;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->RSSetScissorRects.right = pRects->right;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->RSSetScissorRects.top = pRects->top;

		_queueArrayBack += sizeof(RSSetScissorRectsArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::OMSetBlendFactor(
		_In_opt_  const FLOAT BlendFactor[4]
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::OMSetStencilRef(
		_In_  UINT StencilRef
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetPipelineState(
		_In_  ID3D12PipelineState *pPipelineState
		)
	{
		// No ignored parameters, no assumptions to DEBUGCHECK.

		D3DQueueItem* item = reinterpret_cast<D3DQueueItem*>(_queueArrayBack);

		item->Type = D3DQueueItemType::SetPipelineState;
		item->SetPipelineState.pPipelineStateObject = pPipelineState;

		_queueArrayBack += sizeof(SetPipelineStateArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::ResourceBarrier(
		_In_  UINT NumBarriers,
		_In_reads_(NumBarriers)  const D3D12_RESOURCE_BARRIER *pBarriers
		)
	{
		DEBUGCHECK(NumBarriers == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::ResourceBarrier;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->ResourceBarrier.barrier = *pBarriers;

		_queueArrayBack += sizeof(ResourceBarrierArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::ExecuteBundle(
		/* [annotation] */
		_In_  ID3D12GraphicsCommandList *pCommandList
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::BeginQuery(
		_In_  ID3D12QueryHeap *pQueryHeap,
		_In_  D3D12_QUERY_TYPE Type,
		_In_  UINT Index
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::EndQuery(
		_In_  ID3D12QueryHeap *pQueryHeap,
		_In_  D3D12_QUERY_TYPE Type,
		_In_  UINT Index
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::ResolveQueryData(
		_In_  ID3D12QueryHeap *pQueryHeap,
		_In_  D3D12_QUERY_TYPE Type,
		_In_  UINT StartElement,
		_In_  UINT ElementCount,
		_In_  ID3D12Resource *pDestinationBuffer,
		_In_  UINT64 AlignedDestinationBufferOffset
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetPredication(
		_In_opt_  ID3D12Resource *pBuffer,
		_In_  UINT64 AlignedBufferOffset,
		_In_  D3D12_PREDICATION_OP Operation
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetDescriptorHeaps(
		_In_  UINT NumDescriptorHeaps,
		_In_reads_(NumDescriptorHeaps)  ID3D12DescriptorHeap **pDescriptorHeaps
		)
	{
		// No ignored parameters, no assumptions to DEBUGCHECK.

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::SetDescriptorHeaps;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->SetDescriptorHeaps.ppDescriptorHeap = pDescriptorHeaps;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->SetDescriptorHeaps.NumDescriptorHeaps = NumDescriptorHeaps;

		_queueArrayBack += sizeof(SetDescriptorHeapsArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRootSignature(
		_In_  ID3D12RootSignature *pRootSignature
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRootSignature(
		_In_  ID3D12RootSignature *pRootSignature
		)
	{
		// No ignored parameters, no assumptions to DEBUGCHECK.

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::SetGraphicsRootSignature;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->SetGraphicsRootSignature.pRootSignature = pRootSignature;

		_queueArrayBack += sizeof(SetGraphicsRootSignatureArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRootDescriptorTable(
		_In_  UINT RootParameterIndex,
		_In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRootDescriptorTable(
		_In_  UINT RootParameterIndex,
		_In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor
		)
	{
		// No ignored parameters, no assumptions to DEBUGCHECK.

		D3DQueueItem* item = reinterpret_cast<D3DQueueItem*>(_queueArrayBack);

		item->Type = D3DQueueItemType::SetGraphicsRootDescriptorTable;
		item->SetGraphicsRootDescriptorTable.RootParameterIndex = RootParameterIndex;
		item->SetGraphicsRootDescriptorTable.BaseDescriptor = BaseDescriptor;

		_queueArrayBack += sizeof(SetGraphicsRootDescriptorTableArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRoot32BitConstant(
		_In_  UINT RootParameterIndex,
		_In_  UINT SrcData,
		_In_  UINT DestOffsetIn32BitValues
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRoot32BitConstant(
		_In_  UINT RootParameterIndex,
		_In_  UINT SrcData,
		_In_  UINT DestOffsetIn32BitValues
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRoot32BitConstants(
		_In_  UINT RootParameterIndex,
		_In_  UINT Num32BitValuesToSet,
		_In_reads_(Num32BitValuesToSet*sizeof(UINT))  const void *pSrcData,
		_In_  UINT DestOffsetIn32BitValues
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRoot32BitConstants(
		_In_  UINT RootParameterIndex,
		_In_  UINT Num32BitValuesToSet,
		_In_reads_(Num32BitValuesToSet*sizeof(UINT))  const void *pSrcData,
		_In_  UINT DestOffsetIn32BitValues
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRootConstantBufferView(
		_In_  UINT RootParameterIndex,
		_In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation
		)
	{
		// No ignored parameters, no assumptions to DEBUGCHECK.

		D3DQueueItem* item = reinterpret_cast<D3DQueueItem*>(_queueArrayBack);

		item->Type = D3DQueueItemType::SetGraphicsRootConstantBufferView;
		item->SetGraphicsRootConstantBufferView.RootParameterIndex = RootParameterIndex;
		item->SetGraphicsRootConstantBufferView.BufferLocation = BufferLocation;

		_queueArrayBack += sizeof(SetGraphicsRootConstantBufferViewArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRootConstantBufferView(
		_In_  UINT RootParameterIndex,
		_In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRootShaderResourceView(
		_In_  UINT RootParameterIndex,
		_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRootShaderResourceView(
		_In_  UINT RootParameterIndex,
		_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRootUnorderedAccessView(
		_In_  UINT RootParameterIndex,
		_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRootUnorderedAccessView(
		_In_  UINT RootParameterIndex,
		_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::IASetIndexBuffer(
		_In_opt_  const D3D12_INDEX_BUFFER_VIEW *pDesc
		)
	{
		// No ignored parameters, no assumptions to DEBUGCHECK.

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::SetIndexBuffer;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->SetIndexBuffer.desc = *pDesc;

		_queueArrayBack += sizeof(SetIndexBufferArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::IASetVertexBuffers(
		_In_  UINT StartSlot,
		_In_  UINT NumBuffers,
		_In_  const D3D12_VERTEX_BUFFER_VIEW *pDesc
		)
	{
		DEBUGCHECK(StartSlot == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(NumBuffers == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::SetVertexBuffers;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->SetVertexBuffers.desc = *pDesc;

		_queueArrayBack += sizeof(SetVertexBuffersArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SOSetTargets(
		_In_  UINT StartSlot,
		_In_  UINT NumViews,
		_In_  const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pViews
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::OMSetRenderTargets(
		_In_  UINT NumRenderTargetDescriptors,
		_In_  const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors,
		_In_  BOOL RTsSingleHandleToDescriptorRange,
		_In_opt_  const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor
		)
	{
		DEBUGCHECK(RTsSingleHandleToDescriptorRange == FALSE, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(NumRenderTargetDescriptors == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::SetRenderTargets;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->SetRenderTargets.RenderTargetDescriptor = *pRenderTargetDescriptors;

		if (pDepthStencilDescriptor)
			reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->SetRenderTargets.DepthStencilDescriptor = *pDepthStencilDescriptor;
		else
			reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->SetRenderTargets.DepthStencilDescriptor = {};

		_queueArrayBack += sizeof(SetRenderTargetsArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::ClearDepthStencilView(
		_In_  D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView,
		_In_  D3D12_CLEAR_FLAGS ClearFlags,
		_In_  FLOAT Depth,
		_In_  UINT8 Stencil,
		_In_  UINT NumRects,
		_In_reads_opt_(NumRects)  const D3D12_RECT *pRect
		)
	{
		DEBUGCHECK(ClearFlags == D3D11_CLEAR_DEPTH, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(Depth == 0.0f, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(Stencil == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(pRect == nullptr, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(NumRects == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::ClearDepthStencilView;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->ClearDepthStencilView.DepthStencilView = DepthStencilView;

		_queueArrayBack += sizeof(ClearDepthStencilViewArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::ClearRenderTargetView(
		_In_  D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView,
		_In_  const FLOAT ColorRGBA[4],
		_In_  UINT NumRects,
		_In_reads_opt_(NumRects)  const D3D12_RECT *pRects
		)
	{
		DEBUGCHECK(ColorRGBA[0] == 0.0f, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(ColorRGBA[1] == 0.0f, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(ColorRGBA[2] == 0.0f, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(ColorRGBA[3] == 1.0f, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(pRects == nullptr, "Error: Invalid assumption in ID3D12QueuedCommandList.");
		DEBUGCHECK(NumRects == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");

		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->Type = D3DQueueItemType::ClearRenderTargetView;
		reinterpret_cast<D3DQueueItem*>(_queueArrayBack)->ClearRenderTargetView.RenderTargetView = RenderTargetView;

		_queueArrayBack += sizeof(ClearRenderTargetViewArguments) + sizeof(D3DQueueItemType) * 2;
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::ClearUnorderedAccessViewUint(
		_In_  D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
		_In_  D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
		_In_  ID3D12Resource *pResource,
		_In_  const UINT Values[4],
		_In_  UINT NumRects,
		_In_reads_opt_(NumRects)  const D3D12_RECT *pRects
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::ClearUnorderedAccessViewFloat(
		_In_  D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
		_In_  D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
		_In_  ID3D12Resource *pResource,
		_In_  const FLOAT Values[4],
		_In_  UINT NumRects,
		_In_reads_opt_(NumRects)  const D3D12_RECT *pRects
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::DiscardResource(
		_In_  ID3D12Resource *pResource,
		_In_opt_  const D3D12_DISCARD_REGION *pDesc
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetMarker(
		UINT Metadata,
		_In_reads_bytes_opt_(Size)  const void *pData,
		UINT Size
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::BeginEvent(
		UINT Metadata,
		_In_reads_bytes_opt_(Size)  const void *pData,
		UINT Size
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::EndEvent()
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

	void STDMETHODCALLTYPE ID3D12QueuedCommandList::ExecuteIndirect(
		_In_  ID3D12CommandSignature *pCommandSignature,
		_In_  UINT MaxCommandCount,
		_In_  ID3D12Resource *pArgumentBuffer,
		_In_  UINT64 ArgumentBufferOffset,
		_In_opt_  ID3D12Resource *pCountBuffer,
		_In_  UINT64 CountBufferOffset
		)
	{
		// Function not implemented yet.
		DEBUGCHECK(0, "Function not implemented yet.");
	}

}  // namespace DX12