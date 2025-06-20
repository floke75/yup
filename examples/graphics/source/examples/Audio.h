/*
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2025 - kunitoki@gmail.com

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

#pragma once

#include <memory>
#include <cmath> // For sine wave generation

//==============================================================================

class SineWaveGenerator
{
public:
    SineWaveGenerator()
        : sampleRate (44100.0)
        , currentAngle (0.0)
        , frequency (0.0)
        , amplitude (0.0)
    {
    }

    void setSampleRate (double newSampleRate)
    {
        sampleRate = newSampleRate;

        frequency.reset (newSampleRate, 0.1);
        amplitude.reset (newSampleRate, 0.1);
    }

    void setFrequency (double newFrequency, bool immediate = false)
    {
        if (immediate)
            frequency.setCurrentAndTargetValue ((yup::MathConstants<double>::twoPi * newFrequency) / sampleRate);
        else
            frequency.setTargetValue ((yup::MathConstants<double>::twoPi * newFrequency) / sampleRate);
    }

    void setAmplitude (float newAmplitude)
    {
        amplitude.setTargetValue (newAmplitude);
    }

    float getAmplitude() const
    {
        return amplitude.getCurrentValue();
    }

    float getNextSample()
    {
        auto sample = std::sin (currentAngle) * amplitude.getNextValue();

        currentAngle += frequency.getNextValue();
        if (currentAngle >= yup::MathConstants<double>::twoPi)
            currentAngle -= yup::MathConstants<double>::twoPi;

        return static_cast<float> (sample);
    }

private:
    double sampleRate;
    double currentAngle;
    yup::SmoothedValue<float> frequency;
    yup::SmoothedValue<float> amplitude;
};

//==============================================================================

class Oscilloscope : public yup::Component
{
public:
    Oscilloscope()
        : Component ("Oscilloscope")
    {
    }

    void setRenderData (const std::vector<float>& data, int newReadPos)
    {
        renderData.resize (data.size());

        for (std::size_t i = 0; i < data.size(); ++i)
            renderData[i] = data[i];
    }

    void paint (yup::Graphics& g) override
    {
        auto bounds = getLocalBounds();

        auto backgroundColor = yup::Color (0xff101010);
        g.setFillColor (backgroundColor);
        g.fillAll();

        auto lineColor = yup::Color (0xff4b4bff);
        if (renderData.empty())
            return;

        float xSize = getWidth() / float (renderData.size());
        float centerY = getHeight() * 0.5f;

        // Build the main waveform path
        path.clear();
        path.reserveSpace ((int) renderData.size());
        path.moveTo (0.0f, (renderData[0] + 1.0f) * 0.5f * getHeight());

        for (std::size_t i = 1; i < renderData.size(); ++i)
            path.lineTo (i * xSize, (renderData[i] + 1.0f) * 0.5f * getHeight());

        filledPath = path.createStrokePolygon (4.0f);

        g.setFillColor (lineColor);
        g.setFeather (8.0f);
        g.fillPath (filledPath);

        g.setFillColor (lineColor.brighter (0.2f));
        g.setFeather (4.0f);
        g.fillPath (filledPath);

        g.setStrokeColor (lineColor.withAlpha (0.8f));
        g.setStrokeWidth (2.0f);
        g.strokePath (path);

        g.setStrokeColor (lineColor.brighter (0.3f));
        g.setStrokeWidth (1.0f);
        g.strokePath (path);

        g.setStrokeColor (yup::Colors::white.withAlpha (0.9f));
        g.setStrokeWidth (0.5f);
        g.strokePath (path);
    }

private:
    std::vector<float> renderData;
    yup::Path path;
    yup::Path filledPath;
};

//==============================================================================

class AudioExample
    : public yup::Component
    , public yup::AudioIODeviceCallback
{
public:
    AudioExample (const yup::Font& font)
        : Component ("AudioExample")
    {
        // Initialize the audio device
        deviceManager.initialiseWithDefaultDevices (0, 2);

        // Initialize sine wave generators
        double sampleRate = deviceManager.getAudioDeviceSetup().sampleRate;
        sineWaveGenerators.resize (totalRows * totalColumns);
        for (size_t i = 0; i < sineWaveGenerators.size(); ++i)
        {
            sineWaveGenerators[i] = std::make_unique<SineWaveGenerator>();
            sineWaveGenerators[i]->setSampleRate (sampleRate);
            sineWaveGenerators[i]->setFrequency (440.0 * std::pow (1.1, i), true);
        }

        deviceManager.addAudioCallback (this);

        // Add sliders
        for (int i = 0; i < totalRows * totalColumns; ++i)
        {
            auto slider = sliders.add (std::make_unique<yup::Slider> (yup::String (i)));

            slider->onValueChanged = [this, i, sampleRate] (float value)
            {
                sineWaveGenerators[i]->setFrequency (440.0 * std::pow (1.1, i + value));
                sineWaveGenerators[i]->setAmplitude (value * 0.5);
            };

            addAndMakeVisible (slider);
        }

        // Add buttons
        button = std::make_unique<yup::TextButton> ("Randomize");
        button->onClick = [this]
        {
            for (int i = 0; i < sliders.size(); ++i)
                sliders[i]->setValue (yup::Random::getSystemRandom().nextFloat());
        };
        addAndMakeVisible (*button);

        // Add the oscilloscope
        addAndMakeVisible (oscilloscope);
    }

    ~AudioExample() override
    {
        deviceManager.removeAudioCallback (this);
        deviceManager.closeAudioDevice();
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (proportionOfWidth (0.1f), proportionOfHeight (0.2f));
        auto width = bounds.getWidth() / totalColumns;
        auto height = bounds.getHeight() / totalRows;

        for (int i = 0; i < totalRows && sliders.size(); ++i)
        {
            auto row = bounds.removeFromTop (height);
            for (int j = 0; j < totalColumns; ++j)
            {
                auto col = row.removeFromLeft (width);
                sliders.getUnchecked (i * totalRows + j)->setBounds (col.largestFittingSquare());
            }
        }

        if (button != nullptr)
            button->setBounds (getLocalBounds()
                                   .removeFromTop (proportionOfHeight (0.2f))
                                   .reduced (proportionOfWidth (0.2f), 0.0f));

        auto bottomBounds = getLocalBounds()
                                .removeFromBottom (proportionOfHeight (0.2f))
                                .reduced (proportionOfWidth (0.01f), proportionOfHeight (0.01f));

        oscilloscope.setBounds (bottomBounds);
    }

    void mouseDown (const yup::MouseEvent& event) override
    {
        takeKeyboardFocus();
    }

    void refreshDisplay (double lastFrameTimeSeconds) override
    {
        {
            const yup::CriticalSection::ScopedLockType sl (renderMutex);
            oscilloscope.setRenderData (renderData, readPos);
        }

        oscilloscope.repaint();
    }

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const yup::AudioIODeviceCallbackContext& context) override
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float mixedSample = 0.0f;
            float totalScale = 0.0f;

            for (int i = 0; i < sineWaveGenerators.size(); ++i)
            {
                mixedSample += sineWaveGenerators[i]->getNextSample();
                totalScale += sineWaveGenerators[i]->getAmplitude();
            }

            if (totalScale > 1.0f)
                mixedSample /= static_cast<float> (totalScale);

            for (int channel = 0; channel < numOutputChannels; ++channel)
                outputChannelData[channel][sample] = mixedSample;

            auto pos = readPos.fetch_add (1);
            inputData[pos] = mixedSample;
            readPos = readPos % inputData.size();
        }

        const yup::CriticalSection::ScopedLockType sl (renderMutex);
        std::swap (inputData, renderData);
    }

    void audioDeviceAboutToStart (yup::AudioIODevice* device) override
    {
        const yup::CriticalSection::ScopedLockType sl (renderMutex);

        inputData.resize (device->getDefaultBufferSize());
        renderData.resize (device->getDefaultBufferSize());
        readPos = 0;
    }

    void audioDeviceStopped() override
    {
    }

private:
    yup::AudioDeviceManager deviceManager;
    std::vector<std::unique_ptr<SineWaveGenerator>> sineWaveGenerators;

    std::vector<float> renderData;
    std::vector<float> inputData;
    yup::CriticalSection renderMutex;
    std::atomic_int readPos = 0;

    yup::OwnedArray<yup::Slider> sliders;
    int totalRows = 4;
    int totalColumns = 4;

    std::unique_ptr<yup::TextButton> button;
    Oscilloscope oscilloscope;
};
