// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "VideoCommon/VertexManagerBase.h"

namespace DX12
{

class VertexManager : public VertexManagerBase
{
public:
	VertexManager();
	~VertexManager();

	NativeVertexFormat* CreateNativeVertexFormat(const PortableVertexDeclaration& vtx_decl) override;
	void CreateDeviceObjects() override;
	void DestroyDeviceObjects() override;

	void SetIndexBuffer();

protected:
	void ResetBuffer(u32 stride) override;

private:

	void PrepareDrawBuffers(u32 stride);
	void Draw(u32 stride);
	// temp
	void vFlush(bool useDstAlpha) override;

	u32 m_vertexDrawOffset;
	u32 m_indexDrawOffset;

	ID3D12Resource* m_vertexBuffer;
	void* m_vertexBufferData;
	ID3D12Resource* m_indexBuffer;
	void* m_indexBufferData;
};

}  // namespace
