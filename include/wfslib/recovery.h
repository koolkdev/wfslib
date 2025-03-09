/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <vector>

#include "errors.h"

class Device;
class WfsDevice;
class Directory;

class Recovery {
 public:
  static bool CheckWfsKey(std::shared_ptr<Device> device, std::optional<std::vector<std::byte>> key);

  static std::optional<WfsError> DetectDeviceParams(std::shared_ptr<Device> device,
                                                    std::optional<std::vector<std::byte>> key);

  // Open a WFS device without knowning the exact device parameters
  static std::expected<std::shared_ptr<WfsDevice>, WfsError> OpenWfsDeviceWithoutDeviceParams(
      std::shared_ptr<Device> device,
      std::optional<std::vector<std::byte>> key);

  // Open a WFS device with /usr as root directory while skipping the wfs header
  static std::expected<std::shared_ptr<WfsDevice>, WfsError> OpenUsrDirectoryWithoutWfsDeviceHeader(
      std::shared_ptr<Device> device,
      std::optional<std::vector<std::byte>> key);
};
