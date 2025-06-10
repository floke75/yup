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

   This file is part of the JUCE library.
   Copyright (c) 2022 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   To use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

namespace yup
{

class GZIPCompressorOutputStream::GZIPCompressorHelper
{
public:
    GZIPCompressorHelper (int compressionLevel, int windowBits)
        : compLevel ((compressionLevel < 0 || compressionLevel > 9) ? -1 : compressionLevel)
    {
        zerostruct (stream);

        streamIsValid = (deflateInit2 (&stream, compLevel, Z_DEFLATED, windowBits != 0 ? windowBits : MAX_WBITS, 8, strategy) == Z_OK);
    }

    ~GZIPCompressorHelper()
    {
        if (streamIsValid)
            deflateEnd (&stream);
    }

    bool write (const uint8* data, size_t dataSize, OutputStream& out)
    {
        // When you call flush() on a gzip stream, the stream is closed, and you can
        // no longer continue to write data to it!
        jassert (! finished);

        while (dataSize > 0)
            if (! doNextBlock (data, dataSize, out, Z_NO_FLUSH))
                return false;

        return true;
    }

    void finish (OutputStream& out)
    {
        const uint8* data = nullptr;
        size_t dataSize = 0;

        while (! finished)
            doNextBlock (data, dataSize, out, Z_FINISH);
    }

private:
    enum
    {
        strategy = 0
    };

    z_stream stream;
    const int compLevel;
    bool isFirstDeflate = true, streamIsValid = false, finished = false;
    Bytef buffer[32768];

    bool doNextBlock (const uint8*& data, size_t& dataSize, OutputStream& out, const int flushMode)
    {
        if (streamIsValid)
        {
            stream.next_in = const_cast<uint8*> (data);
            stream.next_out = buffer;
            stream.avail_in = (z_uInt) dataSize;
            stream.avail_out = (z_uInt) sizeof (buffer);

            auto result = isFirstDeflate ? deflateParams (&stream, compLevel, strategy)
                                         : deflate (&stream, flushMode);
            isFirstDeflate = false;

            switch (result)
            {
                case Z_STREAM_END:
                    finished = true;
                    YUP_FALLTHROUGH
                case Z_OK:
                {
                    data += dataSize - stream.avail_in;
                    dataSize = stream.avail_in;
                    auto bytesDone = (ssize_t) sizeof (buffer) - (ssize_t) stream.avail_out;
                    return bytesDone <= 0 || out.write (buffer, (size_t) bytesDone);
                }

                default:
                    break;
            }
        }

        return false;
    }

    YUP_DECLARE_NON_COPYABLE (GZIPCompressorHelper)
};

//==============================================================================
GZIPCompressorOutputStream::GZIPCompressorOutputStream (OutputStream& s, int compressionLevel, int windowBits)
    : GZIPCompressorOutputStream (&s, compressionLevel, false, windowBits)
{
}

GZIPCompressorOutputStream::GZIPCompressorOutputStream (OutputStream* out, int compressionLevel, bool deleteDestStream, int windowBits)
    : destStream (out, deleteDestStream)
    , helper (new GZIPCompressorHelper (compressionLevel, windowBits))
{
    jassert (out != nullptr);
}

GZIPCompressorOutputStream::~GZIPCompressorOutputStream()
{
    flush();
}

void GZIPCompressorOutputStream::flush()
{
    helper->finish (*destStream);
    destStream->flush();
}

bool GZIPCompressorOutputStream::write (const void* destBuffer, size_t howMany)
{
    jassert (destBuffer != nullptr && (ssize_t) howMany >= 0);

    return helper->write (static_cast<const uint8*> (destBuffer), howMany, *destStream);
}

int64 GZIPCompressorOutputStream::getPosition()
{
    return destStream->getPosition();
}

bool GZIPCompressorOutputStream::setPosition (int64 /*newPosition*/)
{
    jassertfalse; // can't do it!
    return false;
}

} // namespace yup
