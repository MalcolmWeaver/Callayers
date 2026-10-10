#pragma once

#include <string>

namespace callayers {

// Registers CallayersVCam.dll from the folder that contains this executable.
// Machine-wide registration needs an elevated process.
HRESULT RegisterInstalledDll();

// Registers the media source and starts the system-lifetime virtual camera.
// The returned text is the status line for the window.
std::wstring EnsureVirtualCamera();

}
