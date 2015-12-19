// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DState.h"

#include "VideoBackends/D3D12/D3DQueuedCommandList.h"
#include "VideoBackends/D3D12/D3DCommandListManager.h"

#include "VideoBackends/D3D12/Render.h"
#include "VideoBackends/D3D12/VertexManager.h"

#include <vector>
#include <queue>

static const UINT s_initialCommandAllocatorCount = 8;

namespace DX12
{
	extern UINT currentVscbuf12;
	extern UINT currentPscbuf12;

	extern UINT s_replaceRGBATexture2DUploadHeapOffset;

	extern StateCache gx_state_cache;

	D3DCommandListManager::D3DCommandListManager(
		D3D12_COMMAND_LIST_TYPE commandListType,
		ID3D12Device* pDevice,
		ID3D12CommandQueue* pCommandQueue,
		ID3D12DescriptorHeap** ppGpuVisibleDescriptorHeaps,
		unsigned int gpuVisibleDescriptorHeapsCount,
		ID3D12RootSignature* pDefaultRootSignature
		) :
		_ref(1),
		_pDevice(pDevice),
		_pCommandQueue(pCommandQueue),

		_gpuVisibleDescriptorHeapsCount(gpuVisibleDescriptorHeapsCount),
		_pDefaultRootSignature(pDefaultRootSignature)

#ifdef USE_D3D12_FREQUENT_EXECUTION
		,_cpuAccessLastFrame(false),
		_cpuAccessThisFrame(false),
		_drawsSinceLastExecution(0)
#endif
	{
		// Create command allocators. Start with two lists of '8', and grow on demand if necessary (see D3DCommandListManager::DoubleGpuResourceCount).
		_currentCommandAllocator = 0;
		_currentCommandAllocatorList = 0;
		for (UINT i = 0; i < s_initialCommandAllocatorCount; i++)
		{
			for (UINT j = 0; j < ARRAYSIZE(_commandAllocatorLists); j++)
			{
				ID3D12CommandAllocator *pCommandAllocator = nullptr;

				CheckHR(_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCommandAllocator)));
				_commandAllocatorLists[j].push_back(pCommandAllocator);
			}
		}

		// Create backing command list.
		CheckHR(_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocatorLists[_currentCommandAllocatorList][0], nullptr, IID_PPV_ARGS(&_pBackingCommandList)));

#ifdef USE_D3D12_QUEUED_COMMAND_LISTS
		_pQueuedCommandList = new ID3D12QueuedCommandList(_pBackingCommandList, _pCommandQueue);
#endif

		// Copy list of default descriptor heaps.
		for (UINT i = 0; i < gpuVisibleDescriptorHeapsCount; i++)
		{
			_pGpuVisibleDescriptorHeaps[i] = ppGpuVisibleDescriptorHeaps[i];
		}

		// Create fence that will be used to measure GPU progress of app rendering requests (e.g. CPU readback of GPU data).
		_queueFenceValue = 0;
		CheckHR(_pDevice->CreateFence(_queueFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_pQueueFence)));
		
		// Create fence that will be used internally by D3DCommandListManager to make sure we aren't using in-use resources.
		_queueRolloverFenceValue = 0;
		CheckHR(_pDevice->CreateFence(_queueRolloverFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_pQueueRolloverFence)));

		// Pre-size the deferred destruction lists.
		for (UINT i = 0; i < ARRAYSIZE(_deferredDestructionLists); i++)
		{
			_deferredDestructionLists[0].reserve(200);
		}

		_currentDeferredDestructionList = 0;
	}

	void D3DCommandListManager::SetInitialCommandListState()
	{
		ID3D12GraphicsCommandList* pCommandList = nullptr;
		GetCommandList(&pCommandList);

		pCommandList->SetDescriptorHeaps(_gpuVisibleDescriptorHeapsCount, _pGpuVisibleDescriptorHeaps);
		pCommandList->SetGraphicsRootSignature(_pDefaultRootSignature);

		if (g_renderer)
		{
			// It is possible that we change command lists in the middle of the frame. In that case, restore
			// the viewport/scissor to the current console GPU state.
			g_renderer->SetScissorRect(((Renderer*)g_renderer)->GetScissorRect());
			g_renderer->SetViewport();
		}

		pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

		if (g_vertex_manager)
			reinterpret_cast<VertexManager*>(g_vertex_manager)->SetIndexBuffer();

		dirtyPso = true;
		dirtyVertexBuffer = true;
		dirtyPSCBV = true;
		dirtyVSCBV = true;
		dirtyGSCBV = true;
		dirtySamplers = true;
		currentTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
	}

	void D3DCommandListManager::GetCommandList(ID3D12GraphicsCommandList **ppCommandList)
	{
#ifdef USE_D3D12_QUEUED_COMMAND_LISTS
		*ppCommandList = this->_pQueuedCommandList;
#else
		*ppCommandList = this->_pBackingCommandList;
#endif
	}

	void D3DCommandListManager::ProcessQueuedWork()
	{
		_pQueuedCommandList->ProcessQueuedItems();
	}

	void D3DCommandListManager::ExecuteQueuedWork(bool waitForGpuCompletion)
	{
		if (waitForGpuCompletion)
		{
			_queueFenceValue++;
		}

#ifdef USE_D3D12_QUEUED_COMMAND_LISTS
		CheckHR(_pQueuedCommandList->Close());
		_pQueuedCommandList->QueueExecute();
		
		if (waitForGpuCompletion)
		{
			_pQueuedCommandList->QueueFenceGpuSignal(_pQueueFence, _queueFenceValue);
		}

		if (_currentCommandAllocator == 0)
		{
			PerformGpuRolloverChecks();
		}

		ResetCommandListWithIdleCommandAllocator();

		_pQueuedCommandList->ProcessQueuedItems();
#else
		CheckHR(_pBackingCommandList->Close());

		ID3D12CommandList* const commandListsToExecute[1] = { _pBackingCommandList };
		_pCommandQueue->ExecuteCommandLists(1, commandListsToExecute);
	
		if (waitForGpuCompletion)
		{
			CheckHR(_pCommandQueue->Signal(_pQueueFence, _queueFenceValue));
		}

		if (_currentCommandAllocator == 0)
		{
			PerformGpuRolloverChecks();
		}

		ResetCommandListWithIdleCommandAllocator();
#endif

		SetInitialCommandListState();

		if (waitForGpuCompletion)
		{
			WaitOnCPUForFence(_pQueueFence, _queueFenceValue);
		}
	}

	void D3DCommandListManager::ExecuteQueuedWorkAndPresent(IDXGISwapChain* pSwapChain, UINT syncInterval, UINT flags)
	{
		if (currentVscbuf12 > 5000 * 3840)
		{
			currentVscbuf12 = 0;
		}

		if (currentPscbuf12 > 5000 * 768)
		{
			currentPscbuf12 = 0;
		}

#ifdef USE_D3D12_QUEUED_COMMAND_LISTS
		CheckHR(_pQueuedCommandList->Close());
		_pQueuedCommandList->QueueExecute();
		_pQueuedCommandList->QueuePresent(pSwapChain, syncInterval, flags);
		_pQueuedCommandList->ProcessQueuedItems();

		if (_currentCommandAllocator == 0)
		{
			PerformGpuRolloverChecks();
		}

		_currentCommandAllocator = (_currentCommandAllocator + 1) % _commandAllocatorLists[_currentCommandAllocatorList].size();

		ResetCommandListWithIdleCommandAllocator();

		SetInitialCommandListState();
#else
		ExecuteQueuedWork();
		CheckHR(pSwapChain->Present(syncInterval, flags));
#endif
	}

	void D3DCommandListManager::WaitForQueuedWorkToBeExecutedOnGPU()
	{
		// Wait for GPU to finish all outstanding work.
		_queueFenceValue++;
#ifdef USE_D3D12_QUEUED_COMMAND_LISTS
		_pQueuedCommandList->QueueExecute();
		_pQueuedCommandList->QueueFenceGpuSignal(_pQueueFence, _queueFenceValue);

		_pQueuedCommandList->ProcessQueuedItems();
#else
		CheckHR(_pCommandQueue->Signal(_pQueueFence, _queueFenceValue));
#endif
		WaitOnCPUForFence(_pQueueFence, _queueFenceValue);
	}

	void D3DCommandListManager::PerformGpuRolloverChecks()
	{
		// Insert fence to measure GPU progress, ensure we aren't using in-use command allocators.
		if (_pQueueRolloverFence->GetCompletedValue() < _queueRolloverFenceValue)
		{
			WaitOnCPUForFence(_pQueueRolloverFence, _queueRolloverFenceValue);

			// Means the CPU is too far ahead of GPU. Need to increase number of command allocators.
			UINT currentCommandAllocatorCount = static_cast<UINT>(_commandAllocatorLists[0].size());

			// Hard upper-bound, in case we are pathologically rendering at 2000 FPS, and wanting to create hundreds of Command Allocators.
			if (currentCommandAllocatorCount < 4) 
			{
				for (UINT i = currentCommandAllocatorCount; i < currentCommandAllocatorCount * 2; i++)
				{
					for (UINT j = 0; j < ARRAYSIZE(_commandAllocatorLists); j++)
					{
						ID3D12CommandAllocator *pCommandAllocator = nullptr;
						CheckHR(_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCommandAllocator)));

						_commandAllocatorLists[j].push_back(pCommandAllocator);
					}
				}
			}

		}
		else
		{
			// We know that the previous 'set' of command lists has completed on GPU, and it is safe to
			// release resources / start back at beginning of command allocator list.

			// Begin Deferred Resource Destruction
			UINT _safeToDeleteDeferredDestructionList = (_currentDeferredDestructionList - 1) % ARRAYSIZE(_deferredDestructionLists);

			for (UINT i = 0; i < _deferredDestructionLists[_safeToDeleteDeferredDestructionList].size(); i++)
			{
				CHECK(_deferredDestructionLists[_safeToDeleteDeferredDestructionList][i]->Release() == 0, "Resource leak.");
			}

			_deferredDestructionLists[_safeToDeleteDeferredDestructionList].clear();

			_currentDeferredDestructionList = (_currentDeferredDestructionList + 1) % ARRAYSIZE(_deferredDestructionLists);
			// End Deferred Resource Destruction


			// Begin Command Allocator Resets
			UINT _safeToResetCommandAllocatorList = (_currentCommandAllocatorList - 1) % ARRAYSIZE(_commandAllocatorLists);

			for (UINT i = 0; i < _commandAllocatorLists[_safeToResetCommandAllocatorList].size(); i++)
			{
				CheckHR(_commandAllocatorLists[_safeToResetCommandAllocatorList][i]->Reset());
			}

			_currentCommandAllocatorList = (_currentCommandAllocatorList + 1) % ARRAYSIZE(_commandAllocatorLists);
			// End Command Allocator Resets

			_queueRolloverFenceValue++;
#ifdef USE_D3D12_QUEUED_COMMAND_LISTS
			_pQueuedCommandList->QueueFenceGpuSignal(_pQueueRolloverFence, _queueRolloverFenceValue);
#else
			CheckHR(_pCommandQueue->Signal(_pQueueRolloverFence, _queueRolloverFenceValue));
#endif

			if (_safeToResetCommandAllocatorList == 0)
			{
				s_replaceRGBATexture2DUploadHeapOffset = 0;
			}
		}
	}

	void D3DCommandListManager::ResetCommandListWithIdleCommandAllocator()
	{
#ifdef USE_D3D12_QUEUED_COMMAND_LISTS
		ID3D12QueuedCommandList* pCommandList = _pQueuedCommandList;
#else
		ID3D12GraphicsCommandList* pCommandList = _pBackingCommandList;
#endif

		CheckHR(pCommandList->Reset(_commandAllocatorLists[_currentCommandAllocatorList][_currentCommandAllocator], nullptr));
	}

	void D3DCommandListManager::DestroyResourceAfterCurrentCommandListExecuted(ID3D12Resource *resource)
	{
		CHECK(resource, "Null resource being inserted!");

		_deferredDestructionLists[_currentDeferredDestructionList].push_back(resource);
	}

	void D3DCommandListManager::ImmediatelyDestroyAllResourcesScheduledForDestruction()
	{
		for (UINT i = 0; i < ARRAYSIZE(_deferredDestructionLists); i++)
		{
			for (UINT j = 0; j < _deferredDestructionLists[i].size(); j++)
			{
				_deferredDestructionLists[i][j]->Release();
			}

			_deferredDestructionLists[i].clear();
		}
	}

	void D3DCommandListManager::ClearQueueAndWaitForCompletionOfInflightWork()
	{
		// Wait for GPU to finish all outstanding work.
		_queueFenceValue++;
#ifdef USE_D3D12_QUEUED_COMMAND_LISTS
		_pQueuedCommandList->ClearQueue(); // Waits for currently-processing work to finish, then clears queue.
		_pQueuedCommandList->QueueFenceGpuSignal(_pQueueFence, _queueFenceValue);
		_pQueuedCommandList->ProcessQueuedItems();
#else
		CheckHR(_pCommandQueue->Signal(_pQueueFence, _queueFenceValue));
#endif
		WaitOnCPUForFence(_pQueueFence, _queueFenceValue);
	}

	D3DCommandListManager::~D3DCommandListManager()
	{
		ImmediatelyDestroyAllResourcesScheduledForDestruction();

#ifdef USE_D3D12_QUEUED_COMMAND_LISTS
		CHECK(_pQueuedCommandList->Release() == 0, "Ref leak");
#endif
		CHECK(_pBackingCommandList->Release() == 0, "Ref leak");

		for (UINT i = 0; i < ARRAYSIZE(_commandAllocatorLists); i++)
		{
			for (UINT j = 0; j < _commandAllocatorLists[i].size(); j++)
			{
				CHECK(_commandAllocatorLists[i][j]->Release() == 0, "Ref leak");
			}
		}

		_pQueueFence->Release();
		_pQueueRolloverFence->Release();
	}

	void D3DCommandListManager::WaitOnCPUForFence(ID3D12Fence* pFence, UINT64 fenceValue)
	{
		HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

		CheckHR(pFence->SetEventOnCompletion(fenceValue, hEvent));
		WaitForSingleObject(hEvent, INFINITE);

		CloseHandle(hEvent);
	}

	void D3DCommandListManager::AddRef()
	{
		++_ref;
	}

	unsigned int D3DCommandListManager::Release()
	{
		if (--_ref == 0)
		{
			delete this;
			return 0;
		}
		return _ref;
	}

}  // namespace DX12