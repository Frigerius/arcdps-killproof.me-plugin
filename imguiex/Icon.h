#pragma once

#include <Windows.h>
#include <d3d9.h>

class Icon {
public:
	IDirect3DTexture9* texture;
	Icon(UINT name, HMODULE dll, IDirect3DDevice9* d3d9Device);
	~Icon();
	
private:
	UINT width;
	UINT height;
};
