// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <map>

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DBlob.h"

#include "VideoCommon/VertexShaderGen.h"

namespace DX12 {

class VertexShaderCache
{
public:
	static void Init();
	static void Shutdown();

	static void GetConstantBuffer12();
};

}  // namespace DX12
