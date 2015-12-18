// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DState.h"
#include "VideoBackends/D3D12/D3DDescriptorHeapManager.h"

namespace DX12
{
	bool operator==(const D3DDescriptorHeapManager::SamplerStateSet& lhs, const D3DDescriptorHeapManager::SamplerStateSet& rhs)
	{
		// D3D12TODO: Do something more efficient than this.
		return (!memcmp(&lhs, &rhs, sizeof(D3DDescriptorHeapManager::SamplerStateSet)));
	}

	D3DDescriptorHeapManager::D3DDescriptorHeapManager(D3D12_DESCRIPTOR_HEAP_DESC *desc, ID3D12Device* device, unsigned int temporarySlots) :
		ref(1), device(device), currentPermanentOffsetInHeap(0), descriptorHeap(nullptr), descriptorHeapCpuShadow(nullptr)
	{
		CheckHR(device->CreateDescriptorHeap(desc, IID_PPV_ARGS(&descriptorHeap)));
	
		descriptorHeapSize = desc->NumDescriptors;
		descriptorIncrementSize = device->GetDescriptorHandleIncrementSize(desc->Type);
		gpuVisible = (desc->Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

		if (gpuVisible)
		{
			D3D12_DESCRIPTOR_HEAP_DESC cpuShadowHeapDesc = *desc;
			cpuShadowHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			CheckHR(device->CreateDescriptorHeap(&cpuShadowHeapDesc, IID_PPV_ARGS(&descriptorHeapCpuShadow)));

			heapBaseGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
			heapBaseGPUcpushadow = descriptorHeapCpuShadow->GetCPUDescriptorHandleForHeapStart();
		}

		heapBaseCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();

		firstTemporarySlotInHeap = descriptorHeapSize - temporarySlots;
		currentTemporaryOffsetInHeap = firstTemporarySlotInHeap;
	}

	bool D3DDescriptorHeapManager::Allocate(D3D12_CPU_DESCRIPTOR_HANDLE *cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE *gpuHandle, D3D12_CPU_DESCRIPTOR_HANDLE *gpuHandleCpuShadow, bool temporary)
	{
		bool allocatedFromCurrentHeap = true;

		if (currentPermanentOffsetInHeap + 1 >= firstTemporarySlotInHeap)
		{
			// If out of room in the heap, start back at beginning.
			allocatedFromCurrentHeap = false;
			currentPermanentOffsetInHeap = 0;
		}

		CHECK(!gpuHandle || (gpuHandle && gpuVisible), "D3D12_GPU_DESCRIPTOR_HANDLE used on non-GPU-visible heap.");

		if (temporary && currentTemporaryOffsetInHeap + 1 >= descriptorHeapSize)
		{
			currentTemporaryOffsetInHeap = firstTemporarySlotInHeap;
		}

		unsigned int heapOffsetToUse = temporary ? currentTemporaryOffsetInHeap : currentPermanentOffsetInHeap;

		if (gpuVisible)
		{
			gpuHandle->ptr = heapBaseGPU.ptr + heapOffsetToUse * descriptorIncrementSize;

			if (gpuHandleCpuShadow)
				gpuHandleCpuShadow->ptr = heapBaseGPUcpushadow.ptr + heapOffsetToUse * descriptorIncrementSize;
		}

		cpuHandle->ptr = heapBaseCPU.ptr + heapOffsetToUse * descriptorIncrementSize;

		if (!temporary)
		{
			currentPermanentOffsetInHeap++;
		}

		return allocatedFromCurrentHeap;
	}

	bool D3DDescriptorHeapManager::AllocateGroup(D3D12_CPU_DESCRIPTOR_HANDLE *baseCpuHandle, unsigned int numHandles, D3D12_GPU_DESCRIPTOR_HANDLE *baseGpuHandle, D3D12_CPU_DESCRIPTOR_HANDLE *baseGpuHandleCpuShadow, bool temporary)
	{
		bool allocatedFromCurrentHeap = true;

		if (currentPermanentOffsetInHeap + numHandles >= firstTemporarySlotInHeap)
		{
			// If out of room in the heap, start back at beginning.
			allocatedFromCurrentHeap = false;
			currentPermanentOffsetInHeap = 0;
		}

		CHECK(!baseGpuHandle || (baseGpuHandle && gpuVisible), "D3D12_GPU_DESCRIPTOR_HANDLE used on non-GPU-visible heap.");

		if (temporary && currentTemporaryOffsetInHeap + numHandles >= descriptorHeapSize)
		{
			currentTemporaryOffsetInHeap = firstTemporarySlotInHeap;
		}

		unsigned int heapOffsetToUse = temporary ? currentTemporaryOffsetInHeap : currentPermanentOffsetInHeap;

		if (gpuVisible)
		{
			baseGpuHandle->ptr = heapBaseGPU.ptr + heapOffsetToUse * descriptorIncrementSize;

			if (baseGpuHandleCpuShadow)
				baseGpuHandleCpuShadow->ptr = heapBaseGPUcpushadow.ptr + heapOffsetToUse * descriptorIncrementSize;
		}

		baseCpuHandle->ptr = heapBaseCPU.ptr + heapOffsetToUse * descriptorIncrementSize;

		if (temporary)
		{
			currentTemporaryOffsetInHeap += numHandles;
		}
		else
		{
			currentPermanentOffsetInHeap += numHandles;
		}
		
		return allocatedFromCurrentHeap;
	}
	
	D3D12_GPU_DESCRIPTOR_HANDLE D3DDescriptorHeapManager::GetHandleForSamplerGroup(DX12::SamplerState *samplerState, unsigned int numSamplerSamples)
	{
		auto it = _samplerMap.find(*reinterpret_cast<SamplerStateSet*>(samplerState));

		if (it == _samplerMap.end())
		{
			D3D12_CPU_DESCRIPTOR_HANDLE baseSamplerCpuHandle;
			D3D12_GPU_DESCRIPTOR_HANDLE baseSamplerGpuHandle;

			bool allocatedFromExistingHeap = AllocateGroup(&baseSamplerCpuHandle, numSamplerSamples, &baseSamplerGpuHandle);

			if (!allocatedFromExistingHeap)
			{
				_samplerMap.clear();
			}

			for (unsigned int i = 0; i < numSamplerSamples; i++)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE destinationDescriptor;
				destinationDescriptor.ptr = baseSamplerCpuHandle.ptr + i * D3D::samplerDescriptorSize;

				D3D::device12->CreateSampler(&StateCache::GetDesc12(samplerState[i]), destinationDescriptor);
			}

			_samplerMap[*reinterpret_cast<SamplerStateSet*>(samplerState)] = baseSamplerGpuHandle;

			return baseSamplerGpuHandle;
		}
		else
		{
			return it->second;
		}
	}

	ID3D12DescriptorHeap* D3DDescriptorHeapManager::GetDescriptorHeap()
	{
		return descriptorHeap;
	}

	D3DDescriptorHeapManager::~D3DDescriptorHeapManager()
	{
		SAFE_RELEASE(descriptorHeap);
		SAFE_RELEASE(descriptorHeapCpuShadow);
	}

	void D3DDescriptorHeapManager::AddRef()
	{
		++ref;
	}

	unsigned int D3DDescriptorHeapManager::Release()
	{
		if (--ref == 0)
		{
			delete this;
			return 0;
		}
		return ref;
	}

}  // namespace DX12