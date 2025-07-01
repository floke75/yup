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

#include <yup_core/yup_core.h>
#include <yup_audio_devices/yup_audio_devices.h>
#include <yup_events/yup_events.h>
#include <yup_graphics/yup_graphics.h>
#include <yup_gui/yup_gui.h>

#include <memory>
#include <cmath> // For sine wave generation

#if YUP_ANDROID
#include <BinaryData.h>
#endif

#include "examples/Artboard.h"
#include "examples/Audio.h"
#include "examples/LayoutFonts.h"
#include "examples/FileChooser.h"
#include "examples/OpaqueDemo.h"
#include "examples/Paths.h"
#include "examples/PopupMenu.h"
#include "examples/TextEditor.h"
#include "examples/VariableFonts.h"
#include "examples/Widgets.h"

//==============================================================================

class CustomWindow
    : public yup::DocumentWindow
    , public yup::Timer
{
public:
    CustomWindow()
        : yup::DocumentWindow (yup::ComponentNative::Options()
                                   .withAllowedHighDensityDisplay (true),
                               yup::Color (0xff404040))
    {
        setTitle ("main");

#if YUP_WASM
        auto baseFilePath = yup::File ("/data");
#else
        auto baseFilePath = yup::File (__FILE__).getParentDirectory().getSiblingFile ("data");
#endif

        auto font = yup::ApplicationTheme::getGlobalTheme()->getDefaultFont();

        /*
        // Load an image
        {
            yup::MemoryBlock mb;
            auto imageFile = baseFilePath.getChildFile ("logo.png");
            if (imageFile.loadFileAsData (mb))
            {
                auto loadedImage = yup::Image::loadFromData (mb.asBytes());
                if (loadedImage.wasOk())
                    image = std::move (loadedImage.getReference());
            }
            else
            {
                yup::Logger::outputDebugString ("Unable to load requested image");
            }
        }
        */

        int counter = 0;

        {
            auto button = std::make_unique<yup::TextButton> ("Audio");
            button->onClick = [this, number = counter++]
            {
                selectComponent (number);
            };
            addAndMakeVisible (button.get());
            buttons.add (std::move (button));

            components.add (std::make_unique<AudioExample> (font));
            addChildComponent (components.getLast());
        }

#if JUCE_WASM
        yup::File dataPath = yup::File ("/data");
#else
        yup::File dataPath = yup::File (__FILE__).getParentDirectory().getSiblingFile ("data");
#endif
        {
            auto button = std::make_unique<yup::TextButton> ("Layout Fonts");
            button->onClick = [this, number = counter++]
            {
                selectComponent (number);
            };
            addAndMakeVisible (button.get());
            buttons.add (std::move (button));

            components.add (std::make_unique<LayoutFontsExample> (font));
            addChildComponent (components.getLast());
        }

        {
            auto button = std::make_unique<yup::TextButton> ("Variable Fonts");
            button->onClick = [this, number = counter++]
            {
                selectComponent (number);
            };
            addAndMakeVisible (button.get());
            buttons.add (std::move (button));

            components.add (std::make_unique<VariableFontsExample> (font));
            addChildComponent (components.getLast());
        }

        {
            auto button = std::make_unique<yup::TextButton> ("Paths");
            button->onClick = [this, number = counter++]
            {
                selectComponent (number);
            };
            addAndMakeVisible (button.get());
            buttons.add (std::move (button));

            components.add (std::make_unique<PathsExample>());
            addChildComponent (components.getLast());
        }

        {
            auto button = std::make_unique<yup::TextButton> ("Text Editor");
            button->onClick = [this, number = counter++]
            {
                selectComponent (number);
            };
            addAndMakeVisible (button.get());
            buttons.add (std::move (button));

            components.add (std::make_unique<TextEditorDemo>());
            addChildComponent (components.getLast());
        }

        {
            auto button = std::make_unique<yup::TextButton> ("Popup Menu");
            button->onClick = [this, number = counter++]
            {
                selectComponent (number);
            };
            addAndMakeVisible (button.get());
            buttons.add (std::move (button));

            components.add (std::make_unique<PopupMenuDemo>());
            addChildComponent (components.getLast());
        }

        {
            auto button = std::make_unique<yup::TextButton> ("File Chooser");
            button->onClick = [this, number = counter++]
            {
                selectComponent (number);
            };
            addAndMakeVisible (button.get());
            buttons.add (std::move (button));

            components.add (std::make_unique<FileChooserDemo>());
            addChildComponent (components.getLast());
        }

        {
            auto button = std::make_unique<yup::TextButton> ("Widgets");
            button->onClick = [this, number = counter++]
            {
                selectComponent (number);
            };
            addAndMakeVisible (button.get());
            buttons.add (std::move (button));

            components.add (std::make_unique<yup::WidgetsDemo>());
            addChildComponent (components.getLast());
        }

        {
            auto button = std::make_unique<yup::TextButton> ("Artboard");
            button->onClick = [this, number = counter++]
            {
                selectComponent (number);
            };
            addAndMakeVisible (button.get());
            buttons.add (std::move (button));

            auto artboard = std::make_unique<yup::ArtboardDemo>();
            addChildComponent (artboard.get());
            jassert (artboard->loadArtboard());
            components.add (std::move (artboard));
        }

        {
            auto button = std::make_unique<yup::TextButton> ("Opaque Demo");
            button->onClick = [this, number = counter++]
            {
                selectComponent (number);
            };
            addAndMakeVisible (button.get());
            buttons.add (std::move (button));

            components.add (std::make_unique<yup::OpaqueDemo>());
            addChildComponent (components.getLast());
        }

        selectComponent (0);

        startTimerHz (10);
    }

    ~CustomWindow() override
    {
    }

    void resized() override
    {
        constexpr auto margin = 5;
        constexpr auto buttonsPerRow = 6;

        auto bounds = getLocalBounds().reduced (margin);
        auto initialBounds = bounds;
        auto buttonBounds = initialBounds;

        const auto totalMargin = margin * (buttons.size() - 1);
        const auto buttonWidth = (bounds.getWidth() - totalMargin) / buttonsPerRow;

        int buttonsInRow = 0;
        for (auto& button : buttons)
        {
            if (buttonsInRow == 0)
                buttonBounds = initialBounds.removeFromTop (30);

            button->setBounds (buttonBounds.removeFromLeft (buttonWidth));
            buttonBounds.removeFromLeft (margin);

            if (++buttonsInRow == buttonsPerRow)
            {
                initialBounds.removeFromTop (margin);
                buttonsInRow = 0;
            }
        }

        initialBounds.removeFromTop (margin);
        for (auto& component : components)
            component->setBounds (initialBounds);
    }

    void paint (yup::Graphics& g) override
    {
        yup::DocumentWindow::paint (g);

        //g.drawImageAt (image, getLocalBounds().getCenter());
    }

    /*
    void paintOverChildren (yup::Graphics& g) override
    {
        if (! image.isValid())
            return;

        g.setBlendMode (yup::BlendMode::ColorDodge);
        g.setOpacity (1.0f);
        g.drawImageAt (image, getLocalBounds().getCenter());
    }
    */

    void keyDown (const yup::KeyPress& keys, const yup::Point<float>& position) override
    {
        switch (keys.getKey())
        {
            case yup::KeyPress::escapeKey:
                userTriedToCloseWindow();
                break;

            case yup::KeyPress::textAKey:
                getNativeComponent()->enableAtomicMode (! getNativeComponent()->isAtomicModeEnabled());
                break;

            case yup::KeyPress::textWKey:
                getNativeComponent()->enableWireframe (! getNativeComponent()->isWireframeEnabled());
                break;

            case yup::KeyPress::textZKey:
                setFullScreen (! isFullScreen());
                break;
        }
    }

    void timerCallback() override
    {
        updateWindowTitle();
    }

    void userTriedToCloseWindow() override
    {
        yup::YUPApplication::getInstance()->systemRequestedQuit();
    }

    void selectComponent (int index)
    {
        for (auto& component : components)
            component->setVisible (false);

        components[index]->setVisible (true);
    }

private:
    void updateWindowTitle()
    {
        yup::String title;

        auto currentFps = getNativeComponent()->getCurrentFrameRate();
        title << "[" << yup::String (currentFps, 1) << " FPS]";
        title << " | YUP On Rive Renderer";

        if (getNativeComponent()->isAtomicModeEnabled())
            title << " (atomic)";

        auto [width, height] = getNativeComponent()->getContentSize();
        title << " | " << width << " x " << height;

        setTitle (title);
    }

    yup::OwnedArray<yup::TextButton> buttons;
    yup::OwnedArray<yup::Component> components;

    yup::Font font;

    yup::Image image;
};

//==============================================================================

struct Application : yup::YUPApplication
{
    Application() = default;

    yup::String getApplicationName() override
    {
        return "yup! graphics";
    }

    yup::String getApplicationVersion() override
    {
        return "1.0";
    }

    void initialise (const yup::String& commandLineParameters) override
    {
        YUP_PROFILE_START();

        yup::Logger::outputDebugString ("Starting app " + commandLineParameters);

        window = std::make_unique<CustomWindow>();

#if YUP_IOS
        window->centreWithSize ({ 320, 480 });
#elif YUP_ANDROID
        window->centreWithSize ({ 1080, 2400 });
        // window->setFullScreen(true);
#else
        window->centreWithSize ({ 600, 800 });
#endif

        window->setVisible (true);
    }

    void shutdown() override
    {
        yup::Logger::outputDebugString ("Shutting down");

        window.reset();

        YUP_PROFILE_STOP();
    }

private:
    std::unique_ptr<CustomWindow> window;
};

START_YUP_APPLICATION (Application)
