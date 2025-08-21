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

namespace yup
{

//==============================================================================
/**
    First-order IIR filter implementation.

    The filter implements the difference equation:
    y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1]

    @see FilterBase, FirstOrderCoefficients, FirstOrderState
*/
template <typename SampleType, typename CoeffType = double>
class FirstOrder : public FilterBase<SampleType, CoeffType>
{
public:
    //==============================================================================
    /** Default constructor */
    FirstOrder() = default;

    //==============================================================================
    /**
        Sets the filter coefficients.

        @param newCoefficients  The new first-order coefficients
    */
    void setCoefficients (const FirstOrderCoefficients<CoeffType>& newCoefficients) noexcept
    {
        coefficients = newCoefficients;
    }

    /**
        Gets the current filter coefficients.

        @returns  The current first-order coefficients
    */
    const FirstOrderCoefficients<CoeffType>& getCoefficients() const noexcept
    {
        return coefficients;
    }

    //==============================================================================
    /** @internal */
    void reset() noexcept override
    {
        state.reset();
    }

    /** @internal */
    void prepare (double sampleRate, int maximumBlockSize) override
    {
        this->sampleRate = sampleRate;
        this->maximumBlockSize = maximumBlockSize;
        reset();
    }

    /** @internal */
    SampleType processSample (SampleType inputSample) noexcept override
    {
        const auto inputCoeff = static_cast<CoeffType> (inputSample);
        const auto outputCoeff = coefficients.b0 * inputCoeff + coefficients.b1 * state.x1 - coefficients.a1 * state.y1;

        state.x1 = inputCoeff;
        state.y1 = outputCoeff;

        return static_cast<SampleType> (outputCoeff);
    }

    /** @internal */
    void processBlock (const SampleType* inputBuffer, SampleType* outputBuffer, int numSamples) noexcept override
    {
        auto x1 = state.x1;
        auto y1 = state.y1;
        const auto b0 = coefficients.b0;
        const auto b1 = coefficients.b1;
        const auto a1 = coefficients.a1;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto input = inputBuffer[i];
            const auto output = b0 * input + b1 * x1 - a1 * y1;

            x1 = input;
            y1 = output;
            outputBuffer[i] = output;
        }

        state.x1 = x1;
        state.y1 = y1;
    }

    /** @internal */
    Complex<CoeffType> getComplexResponse (CoeffType frequency) const override
    {
        return coefficients.getComplexResponse (frequency, this->sampleRate);
    }

    /** @internal */
    void getPolesZeros (
        ComplexVector<CoeffType>& poles,
        ComplexVector<CoeffType>& zeros) const override
    {
        poles.reserve (1);
        zeros.reserve (1);

        extractPolesZerosFromFirstOrder (coefficients.b0, coefficients.b1, coefficients.a1, poles, zeros);
    }

private:
    //==============================================================================
    struct FirstOrderState
    {
        CoeffType x1 = 0; // Input delay
        CoeffType y1 = 0; // Output delay

        /** Resets all state variables to zero */
        void reset() noexcept
        {
            x1 = y1 = static_cast<CoeffType> (0.0);
        }
    };

    //==============================================================================
    FirstOrderCoefficients<CoeffType> coefficients;
    FirstOrderState state;

    //==============================================================================
    YUP_LEAK_DETECTOR (FirstOrder)
};

//==============================================================================
/** Type aliases for convenience */
using FirstOrderFloat = FirstOrder<float>;   // float samples, double coefficients (default)
using FirstOrderDouble = FirstOrder<double>; // double samples, double coefficients (default)

} // namespace yup
