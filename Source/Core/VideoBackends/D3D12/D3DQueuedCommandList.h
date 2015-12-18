// Copyright hdcmeta
// Dual-Licensed under MIT and GPLv2+
// Refer to the license.txt/license_mit.txt files included.

#pragma once

#include <d3d12.h>

static const UINT s_queueArraySize = 24 * 1024 * 1024;

namespace DX12
{

	typedef enum D3DQueueItemType
	{
		AbortProcessing = 0,
		SetPipelineState,
		SetRenderTargets,
		SetVertexBuffers,
		SetIndexBuffer,
		RSSetViewports,
		RSSetScissorRects,
		SetGraphicsRootDescriptorTable,
		SetGraphicsRootConstantBufferView,
		SetGraphicsRootSignature,
		ClearRenderTargetView,
		ClearDepthStencilView,
		DrawInstanced,
		DrawIndexedInstanced,
		IASetPrimitiveTopology,
		CopyBufferRegion,
		CopyTextureRegion,
		SetDescriptorHeaps,
		ResourceBarrier,
		ResolveSubresource,
		ExecuteCommandList,
		CloseCommandList,
		Present,
		ResetCommandList,
		ResetCommandAllocator,
		FenceGpuSignal,
		FenceCpuSignal,
		Stop,
		Noop
	} D3DQueueItemType;

	typedef struct SetPipelineStateArguments
	{
		ID3D12PipelineState *pPipelineStateObject;
	} SetPipelineStateArguments;

	typedef struct SetRenderTargetsArguments
	{
		D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetDescriptor;
		D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilDescriptor;
	} SetRenderTargetsArguments;

	typedef struct SetVertexBuffersArguments
	{
		// UINT startSlot; - Dolphin only uses the 0th slot.
		D3D12_VERTEX_BUFFER_VIEW desc;
		// UINT numBuffers; - Only supporting single vertex buffer set since that's all Dolphin uses.
	} SetVertexBuffersArguments;

	typedef struct SetIndexBufferArguments
	{
		D3D12_INDEX_BUFFER_VIEW desc;
	} SetIndexBufferArguments;

	typedef struct RSSetViewportsArguments
	{
		FLOAT TopLeftX;
		FLOAT TopLeftY;
		FLOAT Width;
		FLOAT Height;
	} RSSetViewportsArguments;

	typedef struct RSSetScissorRectsArguments
	{
		LONG left;
		LONG top;
		LONG right;
		LONG bottom;
	} RSSetScissorRectsArguments;

	typedef struct SetGraphicsRootDescriptorTableArguments
	{
		UINT RootParameterIndex;
		D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor;
	} SetGraphicsRootDescriptorTableArguments;

	typedef struct SetGraphicsRootConstantBufferViewArguments
	{
		UINT RootParameterIndex;
		D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
	} SetGraphicsRootConstantBufferViewArguments;

	typedef struct SetGraphicsRootSignatureArguments
	{
		ID3D12RootSignature *pRootSignature;
	} SetGraphicsRootSignatureArguments;

	typedef struct ClearRenderTargetViewArguments
	{
		D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView;
	} ClearRenderTargetViewArguments;

	typedef struct ClearDepthStencilViewArguments
	{
		D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView;
	} ClearDepthStencilViewArguments;

	typedef struct DrawInstancedArguments
	{
		UINT VertexCount;
		UINT StartVertexLocation;
	} DrawInstancedArguments;

	typedef struct DrawIndexedInstancedArguments
	{
		UINT IndexCount;
		UINT StartIndexLocation;
		INT BaseVertexLocation;
	} DrawIndexedInstancedArguments;

	typedef struct IASetPrimitiveTopologyArguments
	{
		D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology;
	} IASetPrimitiveTopologyArguments;

	typedef struct CopyBufferRegionArguments
	{
		ID3D12Resource *pDstBuffer;
		UINT DstOffset;
		ID3D12Resource *pSrcBuffer;
		UINT SrcOffset;
		UINT NumBytes;
	} CopyBufferRegionArguments;

	typedef struct CopyTextureRegionArguments
	{
		D3D12_TEXTURE_COPY_LOCATION dst;
		// UINT DstX, DstY, DstZ; Assuming always '0'. Checked at runtime with DEBUGCHECK.
		D3D12_TEXTURE_COPY_LOCATION src;
		D3D12_BOX srcBox;

	} CopyTextureRegionArguments;

	typedef struct SetDescriptorHeapsArguments
	{
		ID3D12DescriptorHeap** ppDescriptorHeap;
		UINT NumDescriptorHeaps;
	} SetDescriptorHeapsArguments;

	typedef struct ResourceBarrierArguments
	{
		D3D12_RESOURCE_BARRIER barrier;
	} ResourceBarrierArguments;

	typedef struct ResolveSubresourceArguments
	{
		ID3D12Resource *pDstResource;
		UINT DstSubresource;
		ID3D12Resource *pSrcResource;
		UINT SrcSubresource;
		DXGI_FORMAT Format;
	} ResolveSubresourceArguments;

	typedef struct CloseCommandListArguments
	{
	} CloseCommandListArguments;

	typedef struct ExecuteCommandListArguments
	{
	} ExecuteCommandListArguments;

	typedef struct PresentArguments
	{
		IDXGISwapChain *pSwapChain;
		UINT SyncInterval;
		UINT Flags;
	} PresentArguments;

	typedef struct ResetCommandListArguments
	{
		ID3D12CommandAllocator *pAllocator;
	} ResetCommandListArguments;

	typedef struct ResetCommandAllocatorArguments
	{
		ID3D12CommandAllocator *pAllocator;
	} ResetCommandAllocatorArguments;

	typedef struct FenceGpuSignalArguments
	{
		ID3D12Fence *pFence;
		UINT64 fenceValue;
	} FenceGpuSignalArguments;

	typedef struct FenceCpuSignalArguments
	{
		ID3D12Fence *pFence;
		UINT64 fenceValue;
	} FenceCpuSignalArguments;

	typedef struct D3DQueueItem
	{
		D3DQueueItemType Type;

		union
		{
			SetPipelineStateArguments SetPipelineState;
			SetRenderTargetsArguments SetRenderTargets;
			SetVertexBuffersArguments SetVertexBuffers;
			SetIndexBufferArguments SetIndexBuffer;
			RSSetViewportsArguments RSSetViewports;
			RSSetScissorRectsArguments RSSetScissorRects;
			SetGraphicsRootDescriptorTableArguments SetGraphicsRootDescriptorTable;
			SetGraphicsRootConstantBufferViewArguments SetGraphicsRootConstantBufferView;
			SetGraphicsRootSignatureArguments SetGraphicsRootSignature;
			ClearRenderTargetViewArguments ClearRenderTargetView;
			ClearDepthStencilViewArguments ClearDepthStencilView;
			DrawInstancedArguments DrawInstanced;
			DrawIndexedInstancedArguments DrawIndexedInstanced;
			IASetPrimitiveTopologyArguments IASetPrimitiveTopology;
			CopyBufferRegionArguments CopyBufferRegion;
			CopyTextureRegionArguments CopyTextureRegion;
			SetDescriptorHeapsArguments SetDescriptorHeaps;
			ResourceBarrierArguments ResourceBarrier;
			ResolveSubresourceArguments ResolveSubresource;
			CloseCommandListArguments CloseCommandList;
			ExecuteCommandListArguments ExecuteCommandList;
			PresentArguments Present;
			ResetCommandListArguments ResetCommandList;
			ResetCommandAllocatorArguments ResetCommandAllocator;
			FenceGpuSignalArguments FenceGpuSignal;
			FenceCpuSignalArguments FenceCpuSignal;
		};
	} D3DQueueItem;

	class ID3D12QueuedCommandList : public ID3D12GraphicsCommandList
	{
	public:

		ID3D12QueuedCommandList(ID3D12GraphicsCommandList* pBackingCommandList, ID3D12CommandQueue* pBackingCommandQueue);

		void ProcessQueuedItems();

		void QueueExecute();
		void QueueFenceGpuSignal(ID3D12Fence *pFenceToSignal, UINT64 fenceValue);
		void QueueFenceCpuSignal(ID3D12Fence *pFenceToSignal, UINT64 fenceValue);
		void QueuePresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
		void ClearQueue();

		// IUnknown methods

		ULONG STDMETHODCALLTYPE AddRef();
		ULONG STDMETHODCALLTYPE Release();
		HRESULT STDMETHODCALLTYPE QueryInterface(
			_In_ REFIID riid,
			_COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject
			);

		// ID3D12Object methods

		HRESULT STDMETHODCALLTYPE GetPrivateData(
			_In_  REFGUID guid,
			_Inout_  UINT *pDataSize,
			_Out_writes_bytes_opt_(*pDataSize)  void *pData
			);

		HRESULT STDMETHODCALLTYPE SetPrivateData(
			_In_  REFGUID guid,
			_In_  UINT DataSize,
			_In_reads_bytes_opt_(DataSize)  const void *pData
			);

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
			_In_  REFGUID guid,
			_In_opt_  const IUnknown *pData
			);

		HRESULT STDMETHODCALLTYPE SetName(
			_In_z_  LPCWSTR pName
			);

		// ID3D12DeviceChild methods

		D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType(
			);

		// ID3D12CommandList methods

		HRESULT STDMETHODCALLTYPE GetDevice(
			REFIID riid,
			void **ppvDevice
			);

		HRESULT STDMETHODCALLTYPE Close(void);

		HRESULT STDMETHODCALLTYPE Reset(
			_In_  ID3D12CommandAllocator *pAllocator,
			_In_opt_  ID3D12PipelineState *pInitialState
			);

		void STDMETHODCALLTYPE ClearState(
			_In_  ID3D12PipelineState *pPipelineState
			);

		void STDMETHODCALLTYPE DrawInstanced(
			_In_  UINT VertexCountPerInstance,
			_In_  UINT InstanceCount,
			_In_  UINT StartVertexLocation,
			_In_  UINT StartInstanceLocation
			);

		void STDMETHODCALLTYPE DrawIndexedInstanced(
			_In_  UINT IndexCountPerInstance,
			_In_  UINT InstanceCount,
			_In_  UINT StartIndexLocation,
			_In_  INT BaseVertexLocation,
			_In_  UINT StartInstanceLocation
			);

		void STDMETHODCALLTYPE Dispatch(
			_In_  UINT ThreadGroupCountX,
			_In_  UINT ThreadGroupCountY,
			_In_  UINT ThreadGroupCountZ
			);

		void STDMETHODCALLTYPE DispatchIndirect(
			_In_  ID3D12Resource *pBufferForArgs,
			_In_  UINT AlignedByteOffsetForArgs
			);

		void STDMETHODCALLTYPE CopyBufferRegion(
			_In_  ID3D12Resource *pDstBuffer,
			UINT64 DstOffset,
			_In_  ID3D12Resource *pSrcBuffer,
			UINT64 SrcOffset,
			UINT64 NumBytes
			);

		void STDMETHODCALLTYPE CopyTextureRegion(
			_In_  const D3D12_TEXTURE_COPY_LOCATION *pDst,
			UINT DstX,
			UINT DstY,
			UINT DstZ,
			_In_  const D3D12_TEXTURE_COPY_LOCATION *pSrc,
			_In_opt_  const D3D12_BOX *pSrcBox
			);

		void STDMETHODCALLTYPE CopyResource(
			_In_  ID3D12Resource *pDstResource,
			_In_  ID3D12Resource *pSrcResource
			);

		void STDMETHODCALLTYPE CopyTiles(
			_In_  ID3D12Resource *pTiledResource,
			_In_  const D3D12_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate,
			_In_  const D3D12_TILE_REGION_SIZE *pTileRegionSize,
			_In_  ID3D12Resource *pBuffer,
			UINT64 BufferStartOffsetInBytes,
			D3D12_TILE_COPY_FLAGS Flags
			);

		void STDMETHODCALLTYPE ResolveSubresource(
			_In_  ID3D12Resource *pDstResource,
			_In_  UINT DstSubresource,
			_In_  ID3D12Resource *pSrcResource,
			_In_  UINT SrcSubresource,
			_In_  DXGI_FORMAT Format
			);

		void STDMETHODCALLTYPE IASetPrimitiveTopology(
			_In_  D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology
			);

		void STDMETHODCALLTYPE RSSetViewports(
			_In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT Count,
			_In_reads_(Count)  const D3D12_VIEWPORT *pViewports
			);

		void STDMETHODCALLTYPE RSSetScissorRects(
			_In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT Count,
			_In_reads_(Count)  const D3D12_RECT *pRects
			);

		void STDMETHODCALLTYPE OMSetBlendFactor(
			_In_opt_  const FLOAT BlendFactor[4]
			);

		void STDMETHODCALLTYPE OMSetStencilRef(
			_In_  UINT StencilRef
			);

		void STDMETHODCALLTYPE SetPipelineState(
			_In_  ID3D12PipelineState *pPipelineState
			);

		void STDMETHODCALLTYPE ResourceBarrier(
			_In_  UINT NumBarriers,
			_In_reads_(NumBarriers)  const D3D12_RESOURCE_BARRIER *pBarriers
			);

		void STDMETHODCALLTYPE ExecuteBundle(
			/* [annotation] */
			_In_  ID3D12GraphicsCommandList *pCommandList
			);

		void STDMETHODCALLTYPE BeginQuery(
			_In_  ID3D12QueryHeap *pQueryHeap,
			_In_  D3D12_QUERY_TYPE Type,
			_In_  UINT Index
			);

		void STDMETHODCALLTYPE EndQuery(
			_In_  ID3D12QueryHeap *pQueryHeap,
			_In_  D3D12_QUERY_TYPE Type,
			_In_  UINT Index
			);

		void STDMETHODCALLTYPE ResolveQueryData(
			_In_  ID3D12QueryHeap *pQueryHeap,
			_In_  D3D12_QUERY_TYPE Type,
			_In_  UINT StartElement,
			_In_  UINT ElementCount,
			_In_  ID3D12Resource *pDestinationBuffer,
			_In_  UINT64 AlignedDestinationBufferOffset
			);

		void STDMETHODCALLTYPE SetPredication(
			_In_opt_  ID3D12Resource *pBuffer,
			_In_  UINT64 AlignedBufferOffset,
			_In_  D3D12_PREDICATION_OP Operation
			);

		void STDMETHODCALLTYPE SetDescriptorHeaps(
			_In_  UINT NumDescriptorHeaps,
			_In_reads_(NumDescriptorHeaps)  ID3D12DescriptorHeap **pDescriptorHeaps
			);

		void STDMETHODCALLTYPE SetComputeRootSignature(
			_In_  ID3D12RootSignature *pRootSignature
			);

		void STDMETHODCALLTYPE SetGraphicsRootSignature(
			_In_  ID3D12RootSignature *pRootSignature
			);

		void STDMETHODCALLTYPE SetComputeRootDescriptorTable(
			_In_  UINT RootParameterIndex,
			_In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor
			);

		void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(
			_In_  UINT RootParameterIndex,
			_In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor
			);

		void STDMETHODCALLTYPE SetComputeRoot32BitConstant(
			_In_  UINT RootParameterIndex,
			_In_  UINT SrcData,
			_In_  UINT DestOffsetIn32BitValues
			);

		void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(
			_In_  UINT RootParameterIndex,
			_In_  UINT SrcData,
			_In_  UINT DestOffsetIn32BitValues
			);

		void STDMETHODCALLTYPE SetComputeRoot32BitConstants(
			_In_  UINT RootParameterIndex,
			_In_  UINT Num32BitValuesToSet,
			_In_reads_(Num32BitValuesToSet*sizeof(UINT))  const void *pSrcData,
			_In_  UINT DestOffsetIn32BitValues
			);

		void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(
			_In_  UINT RootParameterIndex,
			_In_  UINT Num32BitValuesToSet,
			_In_reads_(Num32BitValuesToSet*sizeof(UINT))  const void *pSrcData,
			_In_  UINT DestOffsetIn32BitValues
			);

		void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(
			_In_  UINT RootParameterIndex,
			_In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation
			);

		void STDMETHODCALLTYPE SetComputeRootConstantBufferView(
			_In_  UINT RootParameterIndex,
			_In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation
			);

		void STDMETHODCALLTYPE SetComputeRootShaderResourceView(
			_In_  UINT RootParameterIndex,
			_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
			);

		void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(
			_In_  UINT RootParameterIndex,
			_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
			);

		void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(
			_In_  UINT RootParameterIndex,
			_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
			);

		void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(
			_In_  UINT RootParameterIndex,
			_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
			);

		void STDMETHODCALLTYPE IASetIndexBuffer(
			_In_opt_  const D3D12_INDEX_BUFFER_VIEW *pDesc
			);

		void STDMETHODCALLTYPE IASetVertexBuffers(
			_In_  UINT StartSlot,
			_In_  UINT NumBuffers,
			_In_  const D3D12_VERTEX_BUFFER_VIEW *pDesc
			);

		void STDMETHODCALLTYPE SOSetTargets(
			_In_  UINT StartSlot,
			_In_  UINT NumViews,
			_In_  const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pViews
			);

		void STDMETHODCALLTYPE OMSetRenderTargets(
			_In_  UINT NumRenderTargetDescriptors,
			_In_  const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors,
			_In_  BOOL RTsSingleHandleToDescriptorRange,
			_In_opt_  const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor
			);

		void STDMETHODCALLTYPE ClearDepthStencilView(
			_In_  D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView,
			_In_  D3D12_CLEAR_FLAGS ClearFlags,
			_In_  FLOAT Depth,
			_In_  UINT8 Stencil,
			_In_  UINT NumRects,
			_In_reads_opt_(NumRects)  const D3D12_RECT *pRect
			);

		void STDMETHODCALLTYPE ClearRenderTargetView(
			_In_  D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView,
			_In_  const FLOAT ColorRGBA[4],
			_In_  UINT NumRects,
			_In_reads_opt_(NumRects)  const D3D12_RECT *pRects
			);

		void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(
			_In_  D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
			_In_  D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
			_In_  ID3D12Resource *pResource,
			_In_  const UINT Values[4],
			_In_  UINT NumRects,
			_In_reads_opt_(NumRects)  const D3D12_RECT *pRects
			);

		void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(
			_In_  D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
			_In_  D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
			_In_  ID3D12Resource *pResource,
			_In_  const FLOAT Values[4],
			_In_  UINT NumRects,
			_In_reads_opt_(NumRects)  const D3D12_RECT *pRects
			);

		void STDMETHODCALLTYPE DiscardResource(
			_In_  ID3D12Resource *pResource,
			_In_opt_  const D3D12_DISCARD_REGION *pRegion
			);

		void STDMETHODCALLTYPE SetMarker(
			UINT Metadata,
			_In_reads_bytes_opt_(Size)  const void *pData,
			UINT Size);

		void STDMETHODCALLTYPE BeginEvent(
			UINT Metadata,
			_In_reads_bytes_opt_(Size)  const void *pData,
			UINT Size);

		void STDMETHODCALLTYPE EndEvent(void);

		void STDMETHODCALLTYPE ExecuteIndirect(
			_In_  ID3D12CommandSignature *pCommandSignature,
			_In_  UINT MaxCommandCount,
			_In_  ID3D12Resource *pArgumentBuffer,
			_In_  UINT64 ArgumentBufferOffset,
			_In_opt_  ID3D12Resource *pCountBuffer,
			_In_  UINT64 CountBufferOffset
			);

	private:

		~ID3D12QueuedCommandList();

		static DWORD WINAPI BackgroundThreadFunction(LPVOID lpParam);

		byte _queueArray[s_queueArraySize];
		volatile UINT _queueArrayFront;
		byte* _queueArrayBack;

		DWORD _backgroundThreadId;
		HANDLE _hBackgroundThread;

		HANDLE _hBeginExecutionEvent;

		ID3D12GraphicsCommandList* _pCommandList;
		ID3D12CommandQueue* _pCommandQueue;

		unsigned int _ref;
	};

}  // namespace