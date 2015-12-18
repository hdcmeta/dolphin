// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DBlob.h"

namespace DX12
{

namespace D3D
{
	// The returned bytecode buffers should be Release()d.
	bool CompileVertexShader(const std::string& code, D3DBlob** blob);
	bool CompileGeometryShader(const std::string& code, D3DBlob** blob, const D3D_SHADER_MACRO* pDefines = nullptr);
	bool CompilePixelShader(const std::string& code, D3DBlob** blob, const D3D_SHADER_MACRO* pDefines = nullptr);
}

}  // namespace DX12
