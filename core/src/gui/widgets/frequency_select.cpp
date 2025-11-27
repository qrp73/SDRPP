#include "frequency_select.h"
#include <algorithm>
#include <gui/style.h>
#include <utils/flog.h>


//#define USE_LED7


#ifdef USE_LED7
    constexpr const char* groupSymbol = "'";
    constexpr const char* minusSymbol = "-";
    constexpr const char* emptySymbol = "8";
#else
    constexpr const char* groupSymbol = "˙";
    constexpr const char* minusSymbol = "-";
    constexpr const char* emptySymbol = "0";
#endif


FrequencySelect::FrequencySelect() {
    const int digitCount = sizeof(_digits)/sizeof(_digits[0]);
    if (digitCount > 18) {
        flog::warn("frequency_select digitCount > 18 can lead out of range issues\n");
    }
    _isNegative = false;
    for (int i = 0; i < digitCount; i++) {
        _digits[i] = 0;
    }
    // maxValue = 10^digitCount - 1
    int64_t maxValue = 1;
    for (int i=0; i < digitCount; i++) {
        maxValue *= 10;
    }
    maxValue -= 1;
    setLimits(-maxValue, maxValue);
}

void FrequencySelect::moveCursorToDigit(int i) {
    (void)i;
    //double xpos, ypos;
    //backend::getMouseScreenPos(xpos, ypos);
    //double nxpos = (_digitTop[i].Max.x + _digitTop[i].Min.x) / 2.0;
    //backend::setMouseScreenPos(nxpos, ypos);
}

void FrequencySelect::onPosChange() {
    _digitSz = ImGui::CalcTextSize(emptySymbol);
    _groupSz = ImGui::CalcTextSize(groupSymbol);
    _minusSz = ImGui::CalcTextSize(minusSymbol);
    ImVec2 drawOffset  = _widgetPos + ImVec2(_minusSz.x, 0);
    const int digitCount = sizeof(_digits)/sizeof(_digits[0]);
    for (int i = 0; i < digitCount; i++) {
        _digitTop[i]    = ImRect(drawOffset, drawOffset + ImVec2(_digitSz.x, _digitSz.y / 2));
        _digitBottom[i] = ImRect(drawOffset + ImVec2(0, _digitSz.y / 2), drawOffset + _digitSz);
        drawOffset.x += _digitSz.x;
        if ((digitCount-1-i) % 3 == 0 && i < digitCount-1) {
#ifdef USE_LED7
            drawOffset.x += _groupSz.x/2;
#else
            drawOffset.x += _groupSz.x;
#endif
        }
    }
}

void FrequencySelect::draw() {
    ImGui::PushFont(style::bigFont);

    auto window = ImGui::GetCurrentWindow();
    ImVec2 cursorPos = ImGui::GetCursorPos();
    
    ImVec2 paddingFix = ImGui::GetWindowContentRegionMin() * 0.3125;
    _widgetPos = window->Pos + cursorPos - paddingFix;

    if (_widgetPos.x != _lastWidgetPos.x || _widgetPos.y != _lastWidgetPos.y) {
        _lastWidgetPos = _widgetPos;
        onPosChange();
    }

    const ImU32 textColor0 = ImGui::GetColorU32(ImGuiCol_Text, 0.1f); // not meaning digits
    const ImU32 textColor1 = ImGui::GetColorU32(ImGuiCol_Text);       // meaning digits

    const int digitCount = sizeof(_digits)/sizeof(_digits[0]);

    ImVec2 drawOffset = _widgetPos;
    ImGui::ItemSize(ImRect(drawOffset, _digitBottom[digitCount-1].Max - paddingFix));

    window->DrawList->AddText(drawOffset, _isNegative ? textColor1 : textColor0, minusSymbol);
    drawOffset.x += _minusSz.x;
    
    bool zeros = true;
    for (int i = 0; i < digitCount; i++) {
        if (_digits[i] != 0) {
            zeros = false;
        }
        const char buf[] = { (char)('0' + _digits[i]), 0};
        bool isMeaning = !(zeros && i != digitCount-1);
#ifdef USE_LED7
        window->DrawList->AddText(drawOffset, textColor0, emptySymbol);
        if (isMeaning) {
            window->DrawList->AddText(drawOffset, textColor1, buf);
        }
#else
        window->DrawList->AddText(drawOffset, isMeaning ? textColor1 : textColor0, buf);
#endif
        drawOffset.x += _digitSz.x;
        if ((digitCount-1-i) % 3 == 0 && i < digitCount-1) {
#ifdef USE_LED7
            ImVec2 fixOffset = drawOffset;
            fixOffset.x -= _groupSz.x/4;
            window->DrawList->AddText(fixOffset, zeros ? textColor0 : textColor1, groupSymbol);
            drawOffset.x += _groupSz.x/2;
#else
            window->DrawList->AddText(drawOffset, zeros ? textColor0 : textColor1, groupSymbol);
            drawOffset.x += _groupSz.x;
#endif
        }
    }

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_None))//&&!gui::mainWindow.lockWaterfallControls) 
    {
        ImGuiID id = ImGui::GetID("frequency_select");
        ImGui::SetKeyOwner(ImGuiKey_UpArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_DownArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_LeftArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_RightArrow, id);

        ImVec2 mousePos = ImGui::GetMousePos();
        int mouseWheel = ImGui::GetIO().MouseWheel;
        auto inputChars = ImGui::GetIO().InputQueueCharacters;
        bool leftClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool rightClick = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        bool onDigit = false;
        bool hovered = false;

        for (int i = 0; i < digitCount; i++) {
            onDigit = false;
            if (_digitTop[i].Contains(mousePos)) {
                window->DrawList->AddRectFilled(_digitTop[i].Min, _digitTop[i].Max, IM_COL32(255, 0, 0, 96));
                if (leftClick) {
                    digitUp(i);
                }
                onDigit = true;
            }
            if (_digitBottom[i].Contains(mousePos)) {
                window->DrawList->AddRectFilled(_digitBottom[i].Min, _digitBottom[i].Max, IM_COL32(0, 0, 255, 96));
                if (leftClick) {
                    digitDown(i);
                }
                onDigit = true;
            }
            if (onDigit) {
                hovered = true;
                if (rightClick || (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
                    for (int j = i; j < digitCount; j++) {
                        _digits[j] = 0;
                    }
                    frequencyChanged = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                    digitUp(i);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                    digitDown(i);
                }
                if ((ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && i > 0) {
                    moveCursorToDigit(i - 1);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && i < digitCount-1) {
                    moveCursorToDigit(i + 1);
                }

                // For each keyboard characters, type it
                for (int j = 0; j < inputChars.Size; j++) {
                    if (inputChars[j] >= '0' && inputChars[j] <= '9') {
                        _digits[i + j] = inputChars[j] - '0';
                        if ((i + j) < digitCount) { moveCursorToDigit(i + j + 1); }
                        frequencyChanged = true;
                    }
                }

                if (mouseWheel != 0) {
                    auto count = abs(mouseWheel);
                    for (int j = 0; j < count; j++) {
                        if (mouseWheel > 0) { 
                            digitUp(i);
                        } else { 
                            digitDown(i);
                        }
                    }
                }
            }
        }
        digitHovered = hovered;
    }
    setFrequency(getDigitFrequency());
    ImGui::PopFont();
}

void FrequencySelect::incrementDigit(int i) {
    const int digitCount = sizeof(_digits)/sizeof(_digits[0]);
    if (i < 0 || i >= digitCount || (i==0 && _digits[i] >= 9)) {
        return;
    }
    _digits[i] = std::clamp<int>(_digits[i], 0, 9);
    if (_digits[i] < 9) {
        _digits[i]++;
        frequencyChanged = true;
    } else {
        bool canCarry = false;
        for (int j = i - 1; j >= 0; j--) {
            if (_digits[j] < 9) {
                canCarry = true;
                break;
            }
        }
        if (canCarry) {
            _digits[i] = 0;
            frequencyChanged = true;
            incrementDigit(i - 1);
        }
    }
}
void FrequencySelect::decrementDigit(int i) {
    const int digitCount = sizeof(_digits)/sizeof(_digits[0]);
    if (i < 0 || i >= digitCount) {
        return;
    }
    _digits[i] = std::clamp<int>(_digits[i], 0, 9);
    if (_digits[i] > 0) {
        _digits[i]--;

        bool isAllZero = true;
        for (int j=0; j < digitCount; j++) {
            if (_digits[j] > 0) {
                isAllZero = false;
                break;
            }
        }
        if (isAllZero) {
            _isNegative = false;
        }
    }
    else {
        bool canCarry = false;
        for (int j = i - 1; j >= 0; j--) {
            if (_digits[j] > 0) {
                canCarry = true;
                break;
            }
        }
        if (canCarry) { 
            _digits[i] = 9;
            decrementDigit(i - 1);
        } else {
            _digits[i] = 1;              // crossing zero
            _isNegative = !_isNegative;
        }
    }
    frequencyChanged = true;
}
void FrequencySelect::digitUp(int i) {
    if (!_isNegative) incrementDigit(i);
    else decrementDigit(i);
}
void FrequencySelect::digitDown(int i) {
    if (!_isNegative) decrementDigit(i);
    else incrementDigit(i);
}

int64_t FrequencySelect::getDigitFrequency() const {
    const int digitCount = sizeof(_digits)/sizeof(_digits[0]);
    int64_t freq = 0;
    int64_t mult = 1;
    for (int i=digitCount-1; i >= 0; i--) {
        freq += _digits[i] * mult;
        mult *= 10;
    }
    if (_isNegative) {
        freq = -freq;
    }
    return freq;
}

void FrequencySelect::setFrequency(int64_t freq) {
    if (freq != _frequency && (freq < _minFreq || freq > _maxFreq)) {
        //flog::debug("setFrequency: reject {} due to being outside limits [{}, {}]", freq, _minFreq, _maxFreq);
        freq = _frequency;
        frequencyChanged = true;
    }
    _isNegative = freq < 0;
    const int digitCount = sizeof(_digits)/sizeof(_digits[0]);
    uint64_t absFreq = freq < 0 ? -freq:freq;
    for (int i = digitCount-1; i >= 0; i--) {
        _digits[i] = absFreq % 10;
        absFreq /= 10;
    }
    int64_t newFreq = getDigitFrequency();
    if (_frequency != newFreq) {
        //flog::debug("setFrequency: {} => {}", _frequency, newFreq);
        //dump_stack();
        frequencyChanged = true;
        _frequency = newFreq;
    }
}

int64_t FrequencySelect::getFrequency() const {
    return _frequency;
}

void FrequencySelect::setLimits(int64_t min, int64_t max) {
    _minFreq = min;
    _maxFreq = max;
    //flog::debug("_minFreq: {}", _minFreq);
    //flog::debug("_maxFreq: {}", _maxFreq);
}
