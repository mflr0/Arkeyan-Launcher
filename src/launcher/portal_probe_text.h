#pragma once

#include <string>

struct PortalProbeEntry;

// One portal's lines for Copy, built from owned strings without opening a device.
std::string PortalProbeEntryText(const PortalProbeEntry& entry);
