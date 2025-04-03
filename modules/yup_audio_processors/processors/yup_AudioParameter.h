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

/**
    A flexible, thread-safe parameter class with support for custom mapping,
    string conversion, smoothing, and different parameter types (linear, log, dB, enum, etc).

    Use AudioParameterBuilder to construct instances of this class.

    @see AudioParameterBuilder
*/
class AudioParameter : public ReferenceCountedObject
{
public:
    //==============================================================================

    /** A pointer to an AudioParameter. */
    using Ptr = ReferenceCountedObjectPtr<AudioParameter>;

    //==============================================================================

    /** A function that converts a real value to a string. */
    using ValueToString = std::function<String (float)>;

    /** A function that converts a string to a real value. */
    using StringToValue = std::function<float (const String&)>;

    //==============================================================================

    /**
        Constructs an AudioParameter instance.

        @param id               The parameter ID (used in the state tree).
        @param name             The display name.
        @param minValue         The minimum real value.
        @param maxValue         The maximum real value.
        @param defaultValue     The default real value.
        @param valueToString    Converts real value to display string (optional).
        @param stringToValue    Parses display string to real value (optional).
    */
    AudioParameter (const String& id,
                    const String& name,
                    float minValue,
                    float maxValue,
                    float defaultValue,
                    ValueToString valueToString = nullptr,
                    StringToValue stringToValue = nullptr,
                    bool smoothingEnabled = false,
                    float smoothingTimeMs = 0.0f);

    /**
        Constructs an AudioParameter instance.

        @param id               The parameter ID (used in the state tree).
        @param name             The display name.
        @param valueRange         The value range.
        @param defaultValue     The default real value.
        @param valueToString    Converts real value to display string (optional).
        @param stringToValue    Parses display string to real value (optional).
    */
    AudioParameter (const String& id,
                    const String& name,
                    NormalisableRange<float> valueRange,
                    float defaultValue,
                    ValueToString valueToString = nullptr,
                    StringToValue stringToValue = nullptr,
                    bool smoothingEnabled = false,
                    float smoothingTimeMs = 0.0f);

    /** Destructor. */
    ~AudioParameter();

    //==============================================================================

    /** Returns the parameter ID. */
    const String& getID() const { return paramID; }

    /** Returns the parameter name. */
    const String& getName() const { return paramName; }

    //==============================================================================

    int getIndexInContainer() const { return paramIndex; }

    void setIndexInContainer (int newIndex) { paramIndex = newIndex; }

    //==============================================================================

    /** Returns the minimum value. */
    float getMinimumValue() const { return valueRange.start; }

    /** Returns the maximum value. */
    float getMaximumValue() const { return valueRange.end; }

    /** Returns the default value. */
    float getDefaultValue() const { return defaultValue; }

    //==============================================================================

    void beginChangeGesture();

    void endChangeGesture();

    bool isPerformingChangeGesture() const { return isInsideGesture != 0; }

    //==============================================================================

    /**
        Sets the real (un-normalized) parameter value and notifies the host.

        @param value The new real value.
    */
    void setValueNotifyingHost (float value);

    /**
        Sets the real (un-normalized) parameter value.

        @param newValue The new real value.
    */
    void setValue (float newValue)
    {
        currentValue.store (valueRange.snapToLegalValue (newValue));
    }

    /** Gets the real (un-normalized) parameter value. */
    float getValue() const { return currentValue.load(); }

    /**
        Sets the normalized [0..1] value.

        @param normalizedValue The new normalized value.
    */
    void setNormalizedValue (float normalizedValue)
    {
        setValue (convertToDenormalizedValue (normalizedValue));
    }

    /** Gets the normalized [0..1] value. */
    float getNormalizedValue() const
    {
        return convertToNormalizedValue (getValue());
    }

    //==============================================================================

    /** */
    float convertToNormalizedValue (float denormalizedValue) const
    {
        return valueRange.convertTo0to1 (denormalizedValue);
    }

    /** */
    float convertToDenormalizedValue (float normalizedValue) const
    {
        return valueRange.convertFrom0to1 (normalizedValue);
    }

    //==============================================================================

    /** Converts a real value to its display string. */
    String toString() const { return valueToString (getValue()); }

    /** Parses a string into a real parameter value. */
    void fromString (const String& string) { setValue (stringToValue (string)); }

    //==============================================================================

    /** */
    String convertToString (float value) const { return valueToString (value); }

    /** */
    float convertFromString (const String& string) const { return stringToValue (string); }

    //==============================================================================

    /** Returns true if smoothing is enabled. */
    bool isSmoothingEnabled() const { return smoothingEnabled; }

    /** Returns the smoothing time in milliseconds. */
    float getSmoothingTimeMs() const { return smoothingTimeMs; }

    //==============================================================================

    /** A listener for parameter changes. */
    class Listener
    {
    public:
        virtual ~Listener() = default;

        /** Called when the parameter value changes. */
        virtual void parameterValueChanged (const AudioParameter::Ptr& parameter, int indexInContainer) = 0;

        /** Called when a gesture begins. */
        virtual void parameterGestureBegin (const AudioParameter::Ptr& parameter, int indexInContainer) = 0;

        /** Called when a gesture ends. */
        virtual void parameterGestureEnd (const AudioParameter::Ptr& parameter, int indexInContainer) = 0;
    };

    /** Adds a listener to the parameter. */
    void addListener (Listener* listener);

    /** Removes a listener from the parameter. */
    void removeListener (Listener* listener);

private:
    using ListenersType = ListenerList<Listener, Array<Listener*, CriticalSection>>;

    String paramID;
    String paramName;
    int paramVersion = 0;
    int paramIndex = -1;
    std::atomic<float> currentValue = 0.0f;
    NormalisableRange<float> valueRange = { 0.0f, 1.0f };
    float defaultValue = 0.0f;
    ValueToString valueToString = nullptr;
    StringToValue stringToValue = nullptr;
    ListenersType listeners;
    float smoothingTimeMs = 0.0f;
    bool smoothingEnabled = false;
    int isInsideGesture = 0;
};

} // namespace yup
