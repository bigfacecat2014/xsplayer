#pragma once

#include <vector>
#include <cstdint>

/**
 * Basic VPS parser - extracts video width, height and framerate if applicable
 */
class H265VPSParser
{
public:
    H265VPSParser(uint8_t* vps, uint32_t vpsSize);

    uint32_t GetWidth() const { return _width; }
    uint32_t GetHeight() const { return _height; }
    double GetFramerate() const { return _framerate; }

private:
    std::vector<uint8_t> _vps;

    // Parsed value
    uint32_t _width = 0;
    uint32_t _height = 0;
    double _framerate = 0;
};

/**
 * Basic SPS parser - extracts video width, height and framerate if applicable
 */
class H265SPSParser
{
public:
    H265SPSParser(uint8_t* sps, uint32_t spsSize);

    uint32_t GetWidth() const { return _width; }
    uint32_t GetHeight() const { return _height; }
    double GetFramerate() const { return _framerate; }

private:
    std::vector<uint8_t> _sps;

    // Parsed value
    uint32_t _width;
    uint32_t _height;
    double _framerate;
};