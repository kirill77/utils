#pragma once

// visLib - Abstract Visualization Library
// Platform-independent interfaces for 3D rendering
//
// Usage:
//   #include <visLib.h>
//
// Or include individual headers:
//   #include <IRenderer.h>
//   #include <IWindow.h>
//   etc.

// Types and math
#include "utils/visLib/include/Types.h"

// Core interfaces
#include "utils/visLib/include/IRenderer.h"
#include "utils/visLib/include/IWindow.h"
#include "utils/visLib/include/IMesh.h"
#include "utils/visLib/include/IFont.h"
#include "utils/visLib/include/IText.h"
#include "utils/visLib/include/IVisObject.h"

// Value types
#include "utils/visLib/include/Camera.h"
#include "utils/visLib/include/MeshNode.h"
#include "utils/visLib/include/InputState.h"
