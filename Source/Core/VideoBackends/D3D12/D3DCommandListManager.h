// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "D3DQueuedCommandList.h"

namespace DX12
{

	// This class provides an abstraction for D3D12 descriptor heaps.
	class D3DCommandListManager
	{
	public:

		D3DCommandListManager(D3D12_COMMAND_LIST_TYPE commandListType, ID3D12Device* device, ID3D12CommandQueue *commandQueue, ID3D12DescriptorHeap** gpuVisibleDescriptorHeaps, unsigned int numGpuVisibleDescriptorHeaps, ID3D12RootSignature* defaultRootSignature);

		void SetInitialCommandListState();

		void GetCommandList(ID3D12GraphicsCommandList **ppCommandList);

		void ExecuteQueuedWork(bool waitForGpuCompletion = false);
		void ExecuteQueuedWorkAndPresent(IDXGISwapChain* pSwapChain, UINT syncInterval, UINT flags);

		void ProcessQueuedWork();

		void WaitForQueuedWorkToBeRecordedOnCPU();
		void WaitForQueuedWorkToBeExecutedOnGPU();

		void ClearQueueAndWaitForCompletionOfInflightWork();
		void DestroyResourceAfterCurrentCommandListExecuted(ID3D12Resource* resource);
		void ImmediatelyDestroyAllResourcesScheduledForDestruction();

		void AddRef();
		unsigned int Release();

		bool dirtyVertexBuffer;
		bool dirtyPso;
		bool dirtyPSCBV;
		bool dirtyVSCBV;
		bool dirtyGSCBV;
		bool dirtySamplers;
		UINT currentTopology;

#ifdef USE_D3D12_FREQUENT_EXECUTION
		UINT _drawsSinceLastExecution;
		bool _cpuAccessLastFrame;
		bool _cpuAccessThisFrame;

		void CPUAccessNotify() {
			_cpuAccessLastFrame = true;
			_cpuAccessThisFrame = true;
			_drawsSinceLastExecution = 0;
		};
#endif

	private:
		~D3DCommandListManager();

		void PerformGpuRolloverChecks();
		void ResetCommandListWithIdleCommandAllocator();
		void WaitOnCPUForFence(ID3D12Fence* pFence, UINT64 fenceValue);


		ID3D12Device* _pDevice;
		ID3D12CommandQueue* _pCommandQueue;
		UINT64 _queueFenceValue;
		ID3D12Fence* _pQueueFence;
		UINT64 _queueRolloverFenceValue;
		ID3D12Fence* _pQueueRolloverFence;

		UINT _currentCommandAllocator;
		UINT _currentCommandAllocatorList;
		std::vector<ID3D12CommandAllocator*> _commandAllocatorLists[2];
		
		ID3D12GraphicsCommandList* _pBackingCommandList;
		ID3D12QueuedCommandList* _pQueuedCommandList;

		ID3D12DescriptorHeap* _pGpuVisibleDescriptorHeaps[2]; // We'll never need more than two.
		UINT _gpuVisibleDescriptorHeapsCount;
		ID3D12RootSignature* _pDefaultRootSignature;

		UINT _currentDeferredDestructionList;
		std::vector<ID3D12Resource*> _deferredDestructionLists[2];

		unsigned int _ref;
	};

}  // namespace