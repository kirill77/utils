#include "utils/visLib/include/Camera.h"
#include <cmath>
#include <numbers>
#include <algorithm>

namespace visLib {

Camera::Camera()
    : m_position(0.0f, 0.0f, -5.0f)
    , m_direction(0.0f, 0.0f, 1.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_fov(45.0f)
    , m_aspectRatio(16.0f / 9.0f)
    , m_nearPlane(0.1f)
    , m_farPlane(1000.0f)
{
}

void Camera::setPosition(const float3& pos)
{
    m_position = pos;
}

void Camera::setDirection(const float3& dir)
{
    float len = length(dir);
    if (len > 0.0001f) {
        m_direction = dir / len;
    }
}

void Camera::setUp(const float3& up)
{
    float len = length(up);
    if (len > 0.0001f) {
        m_up = up / len;
    }
}

float3 Camera::getRight() const
{
    return normalize(cross(m_direction, m_up));
}

void Camera::setFOV(float fovDegrees)
{
    m_fov = std::clamp(fovDegrees, 1.0f, 179.0f);
}

void Camera::setAspectRatio(float aspectRatio)
{
    if (aspectRatio > 0.0f) {
        m_aspectRatio = aspectRatio;
    }
}

void Camera::setNearPlane(float nearPlane)
{
    if (nearPlane > 0.0f) {
        m_nearPlane = nearPlane;
    }
}

void Camera::setFarPlane(float farPlane)
{
    if (farPlane > m_nearPlane) {
        m_farPlane = farPlane;
    }
}

bool Camera::fitBoxToView(const box3& boxToFit)
{
    if (boxToFit.isempty()) {
        return false;
    }

    // Calculate box center and size
    float3 center = boxToFit.center();
    float3 diagonal = boxToFit.diagonal();
    float radius = length(diagonal) * 0.5f;

    if (radius < 0.0001f) {
        return false;
    }

    // Set FOV to 30 degrees for a nice framing
    m_fov = 30.0f;

    // Calculate distance to fit the box in view
    float fovRad = m_fov * static_cast<float>(std::numbers::pi) / 180.0f;
    float distance = radius / std::sin(fovRad * 0.5f);

    // Position camera to look at center from current direction
    m_position = center - m_direction * distance;

    return true;
}

affine3 Camera::getCameraTransform() const
{
    float3 right = getRight();
    float3 trueUp = cross(right, m_direction);

    // Camera-to-world transform: columns are right, up, forward, position
    return affine3::from_cols(right, trueUp, m_direction, m_position);
}

void Camera::setCameraTransform(const affine3& transform)
{
    // Extract axes from the transform
    // Column 0 = right, Column 1 = up, Column 2 = forward, Column 3 = position
    m_direction = float3(transform.m_linear.m02, transform.m_linear.m12, transform.m_linear.m22);
    m_up = float3(transform.m_linear.m01, transform.m_linear.m11, transform.m_linear.m21);
    m_position = transform.m_translation;

    // Normalize to avoid drift
    m_direction = normalize(m_direction);
    m_up = normalize(m_up);
}

float4x4 Camera::getViewMatrix() const
{
    // Build view matrix (world-to-camera transform)
    float3 right = getRight();
    float3 trueUp = cross(right, m_direction);

    // View matrix is the inverse of the camera transform
    // For an orthonormal basis, inverse = transpose of rotation, negated translation
    float4x4 view;

    // Row 0: right axis
    view.row0.x = right.x;
    view.row0.y = trueUp.x;
    view.row0.z = m_direction.x;
    view.row0.w = 0.0f;

    // Row 1: up axis
    view.row1.x = right.y;
    view.row1.y = trueUp.y;
    view.row1.z = m_direction.y;
    view.row1.w = 0.0f;

    // Row 2: forward axis
    view.row2.x = right.z;
    view.row2.y = trueUp.z;
    view.row2.z = m_direction.z;
    view.row2.w = 0.0f;

    // Row 3: translation (dot products with negated position)
    view.row3.x = -dot(right, m_position);
    view.row3.y = -dot(trueUp, m_position);
    view.row3.z = -dot(m_direction, m_position);
    view.row3.w = 1.0f;

    return view;
}

float4x4 Camera::getProjectionMatrix() const
{
    // Build perspective projection matrix (left-handed, depth 0 to 1)
    float fovRad = m_fov * static_cast<float>(std::numbers::pi) / 180.0f;
    float yScale = 1.0f / std::tan(fovRad * 0.5f);
    float xScale = yScale / m_aspectRatio;
    float zRange = m_farPlane - m_nearPlane;

    float4x4 proj = float4x4::zero();

    proj.row0.x = xScale;
    proj.row1.y = yScale;
    proj.row2.z = m_farPlane / zRange;
    proj.row2.w = 1.0f;
    proj.row3.z = -m_nearPlane * m_farPlane / zRange;

    return proj;
}

} // namespace visLib
