# Dolphin [Unofficial] DX12 - A GameCube / Wii / Triforce Emulator

This repo contains an unofficial DX12 port of the Dolphin emulator. It in general provides anywhere from a 0% - 50% FPS improvement from DX11/OGL, depending on the particular system/workload/settings.

### Requirements
* Windows 10
* Latest graphics driver, and a AMD 7000-series, Intel HD 4400, or nVidia 600-series GPU or higher.
* [VS 2015 Redist](https://www.microsoft.com/en-us/download/details.aspx?id=48145)

Then just select the 'Direct3D 12' backend from Video Options. See this Dolphin forums thread for more details: https://forums.dolphin-emu.org/Thread-unofficial-dolphin-dx12-backend

### Contributing
Pull requests are welcome! Some small example things that need fixing:
* GPU-side bounding box is not implemented.
* Real XFB is not implemented.
* Performance queries are not yet implemented.
* Shader/PSO compilation should be moved to a background thread - or maybe skip objects until the shader is available a few frames later?
* Offline PSOs could be compressed for disk savings, these tend to be larger than the individual shaders.

### License
All files are licensed under GPLv2+, see license.txt.

Additionally, the D3D12QueuedCommandList implementation is available under the MIT license - choose which you prefer! See license_mit.txt

### Acknowledgements
Thanks for course to everyone who has worked on Dolphin. Special thanks to those on the D3D11 video backend, which served as the basis for this port.