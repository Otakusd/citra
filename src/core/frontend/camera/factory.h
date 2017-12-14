// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include "core/frontend/camera/interface.h"

namespace Camera {

class CameraFactory {
public:
    virtual ~CameraFactory();

    /**
     * Creates a camera object based on the camera id.
     * @param camera_id Camera ID.
     * @returns a unique_ptr to the created camera object.
     */
    virtual std::unique_ptr<CameraInterface> Create(int camera_id) const = 0;
};

/**
 * Registers an external camera factory.
 * @param name Identifier of the camera factory.
 * @param factory Camera factory to register.
 */
void RegisterFactory(const std::string& name, std::unique_ptr<CameraFactory> factory);

/**
 * Creates a camera from the factory.
 * @param camera_id Camera ID.
 */
std::unique_ptr<CameraInterface> CreateCamera(int camera_id);

} // namespace Camera
