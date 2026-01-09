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
#include "Types.h"

// Core interfaces
#include "IRenderer.h"
#include "IWindow.h"
#include "IMesh.h"
#include "IFont.h"
#include "IText.h"
#include "IVisObject.h"

// Value types
#include "Camera.h"
#include "MeshNode.h"
#include "InputState.h"
