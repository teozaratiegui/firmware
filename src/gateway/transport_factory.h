#pragma once

#include <memory>

class TransportMode;

std::unique_ptr<TransportMode> createDefaultTransport();
