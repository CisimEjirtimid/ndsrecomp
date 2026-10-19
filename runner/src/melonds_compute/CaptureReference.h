// Copyright 2026 Matthew Stanley. GPL-3.0-or-later.
// Derived from the melonDS GPU/render interfaces.
#pragma once
#include "NDS.h"
#include <memory>

namespace melonDS {
// Independent of the host renderer's lifetime and internal resolution. A job
// is latched at VCount215 and consumed only if DISPCAPCNT needs 3D pixels.
class CaptureReference {
public:
    void Reset() { Frame.reset(); Ready = false; }
    bool HasFrame() const { return Frame != nullptr; }
    void Latch(GPU& source);
    const u32* GetLine(int line, u16 renderXPos, bool abort);
private:
    std::unique_ptr<NDS> Frame;
    bool Ready = false;
    u32 ZeroLine[448] {};
};
}
