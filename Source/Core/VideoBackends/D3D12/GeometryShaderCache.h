// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <d3d11.h>
#include <map>

#include "VideoCommon/GeometryShaderGen.h"

namespace DX12
{

class GeometryShaderCache
{
public:
	static void Init();
	static void Shutdown();

	static void GetConstantBuffer12(); // This call on D3D12 actually sets the constant buffer, no need to return it.
};

}  // namespace DX12
