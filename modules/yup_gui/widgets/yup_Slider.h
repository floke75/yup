/*
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2024 - kunitoki@gmail.com

   YUP is an open source library subject to open-source licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   to use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   YUP IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

namespace yup
{

//==============================================================================
class JUCE_API Slider : public Component
{
public:
    //==============================================================================
    Slider (StringRef componentID, const Font& font);

    //==============================================================================
    void setValue (float newValue, NotificationType notification = sendNotification);
    float getValue() const;

    virtual void valueChanged();

    std::function<void (float)> onValueChanged;
    std::function<void()> onDragStart;
    std::function<void()> onDragEnd;

    //==============================================================================
    void resized() override;
    void paint (Graphics& g) override;
    void mouseEnter (const MouseEvent& event) override;
    void mouseExit (const MouseEvent& event) override;
    void mouseDown (const MouseEvent& event) override;
    void mouseUp (const MouseEvent& event) override;
    void mouseDrag (const MouseEvent& event) override;
    void mouseWheel (const MouseEvent& event, const MouseWheelData& data) override;

private:
    void updateRenderItems (bool forceAll);
    void sendValueChanged (NotificationType notification);

    Path backgroundPath;
    Path backgroundArc;
    Path foregroundArc;
    Path foregroundLine;
    StyledText text;

    Point<float> origin;
    const Font& font;
    float value = 0.0f;
    int index = 0;
    bool isInside = false;
};

} // namespace yup
