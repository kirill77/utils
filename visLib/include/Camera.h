#pragma once

#include "Types.h"

namespace visLib {

// Camera class for 3D scene viewing
// This is a value type - no virtual methods, can be copied freely
class Camera {
public:
    Camera();

    // Position and orientation
    void setPosition(const float3& pos);
    void setDirection(const float3& dir);
    void setUp(const float3& up);

    float3 getPosition() const { return m_position; }
    float3 getDirection() const { return m_direction; }
    float3 getUp() const { return m_up; }
    float3 getRight() const;

    // Projection parameters
    void setFOV(float fovDegrees);
    void setAspectRatio(float aspectRatio);
    void setNearPlane(float nearPlane);
    void setFarPlane(float farPlane);

    float getFOV() const { return m_fov; }
    float getAspectRatio() const { return m_aspectRatio; }
    float getNearPlane() const { return m_nearPlane; }
    float getFarPlane() const { return m_farPlane; }

    // Fit the provided bounding box into view
    // Adjusts camera position and direction to frame the box
    // Returns false if the box is empty or invalid
    bool fitBoxToView(const box3& boxToFit);

    // Camera transform as affine (camera-to-world transform)
    affine3 getCameraTransform() const;
    void setCameraTransform(const affine3& transform);

    // View and projection matrices (row-major, suitable for DirectX)
    // These are computed on-demand from camera parameters
    float4x4 getViewMatrix() const;
    float4x4 getProjectionMatrix() const;

private:
    float3 m_position;
    float3 m_direction;
    float3 m_up;
    float m_fov;            // Field of view in degrees
    float m_aspectRatio;    // Width / height
    float m_nearPlane;
    float m_farPlane;
};

} // namespace visLib
