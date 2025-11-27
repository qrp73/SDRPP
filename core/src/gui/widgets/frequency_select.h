#pragma once
#include <stdint.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

class FrequencySelect {
public:
    FrequencySelect();

    void draw();
    
    int64_t getFrequency() const;
    void setFrequency(int64_t freq);
    void setLimits(int64_t min, int64_t max);

    bool frequencyChanged = false;
    bool digitHovered = false;

private:
    void onPosChange();
    void incrementDigit(int i);
    void decrementDigit(int i);
    void digitUp(int i);
    void digitDown(int i);
    
    void moveCursorToDigit(int i);
    int64_t getDigitFrequency() const;

    ImVec2 _widgetPos = ImVec2(0,0);
    ImVec2 _lastWidgetPos = ImVec2(-1,-1);
    
    ImVec2 _digitSz = ImVec2(0,0);
    ImVec2 _groupSz = ImVec2(0,0);
    ImVec2 _minusSz = ImVec2(0,0);

    int     _digits[12];
    ImRect  _digitTop[12];
    ImRect  _digitBottom[12];

    bool    _isNegative = false;
    int64_t _frequency = 0;

    int64_t _minFreq;
    int64_t _maxFreq;
};
