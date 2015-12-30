// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
#pragma once

#include "VideoCommon/NativeVertexFormat.h"
#include <d3d12.h>

namespace DX12
{
class D3DVertexFormat : public NativeVertexFormat
{
	D3D12_INPUT_ELEMENT_DESC m_elems[15];
	UINT m_num_elems;

	D3D12_INPUT_LAYOUT_DESC m_layout12;

public:
	D3DVertexFormat(const PortableVertexDeclaration& vtx_decl);
	~D3DVertexFormat() {}

	void SetupVertexPointers();

	D3D12_INPUT_LAYOUT_DESC GetActiveInputLayout12() const { return m_layout12; };

private:
	D3D12_INPUT_ELEMENT_DESC GetInputElementDescFromAttributeFormat(const AttributeFormat* format, const char* semantic_name, unsigned int semantic_index) const;
};
}