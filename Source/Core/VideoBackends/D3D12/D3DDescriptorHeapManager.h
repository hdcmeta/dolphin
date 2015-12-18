// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <d3d12.h>
#include <unordered_map>

#include "VideoBackends/D3D12/D3DState.h"


namespace DX12
{
	// This class provides an abstraction for D3D12 descriptor heaps.
	class D3DDescriptorHeapManager
	{
	public:

		D3DDescriptorHeapManager(D3D12_DESCRIPTOR_HEAP_DESC *desc, ID3D12Device* device, unsigned int temporarySlots = 0);

		bool Allocate(D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle = nullptr, D3D12_CPU_DESCRIPTOR_HANDLE* gpuHandleCpuShadow = nullptr, bool temporary = false);
		bool AllocateGroup(D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandles, unsigned int numHandles, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandles = nullptr, D3D12_CPU_DESCRIPTOR_HANDLE* gpuHandleCpuShadows = nullptr, bool temporary = false);

		D3D12_GPU_DESCRIPTOR_HANDLE GetHandleForSamplerGroup(SamplerState* samplerState, unsigned int numSamplerSamples);

		ID3D12DescriptorHeap* GetDescriptorHeap();

		void AddRef();
		unsigned int Release();

		struct SamplerStateSet
		{
			SamplerState desc0;
			SamplerState desc1;
			SamplerState desc2;
			SamplerState desc3;
			SamplerState desc4;
			SamplerState desc5;
			SamplerState desc6;
			SamplerState desc7;
		};

	private:
		~D3DDescriptorHeapManager();

		ID3D12Device* device;
		ID3D12DescriptorHeap* descriptorHeap;
		ID3D12DescriptorHeap* descriptorHeapCpuShadow;

		D3D12_CPU_DESCRIPTOR_HANDLE heapBaseCPU;
		D3D12_GPU_DESCRIPTOR_HANDLE heapBaseGPU;
		D3D12_CPU_DESCRIPTOR_HANDLE heapBaseGPUcpushadow;

		struct hash_sampler_desc
		{
			size_t operator()(const SamplerStateSet samplerStateSet) const
			{
				return samplerStateSet.desc0.packed;
			}
		};

		std::unordered_map<SamplerStateSet, D3D12_GPU_DESCRIPTOR_HANDLE, hash_sampler_desc> _samplerMap;

		unsigned int currentTemporaryOffsetInHeap;
		unsigned int currentPermanentOffsetInHeap;

		unsigned int descriptorIncrementSize;
		unsigned int descriptorHeapSize;
		bool gpuVisible;

		unsigned int firstTemporarySlotInHeap;

		unsigned int ref;
	};

}  // namespace