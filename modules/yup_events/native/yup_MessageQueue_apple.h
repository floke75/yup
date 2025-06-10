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

//==============================================================================
class InternalMessageQueue
{
public:
    InternalMessageQueue()
    {
#if YUP_IOS
        runLoop = CFRunLoopGetCurrent();
#else
        runLoop = CFRunLoopGetMain();
#endif

        CFRunLoopSourceContext sourceContext;
        zerostruct (sourceContext); // (can't use "= { 0 }" on this object because it's typedef'ed as a C struct)
        sourceContext.info = this;
        sourceContext.perform = runLoopSourceCallback;
        runLoopSource.reset (CFRunLoopSourceCreate (kCFAllocatorDefault, 1, &sourceContext));
        CFRunLoopAddSource (runLoop, runLoopSource.get(), kCFRunLoopCommonModes);
    }

    ~InternalMessageQueue() noexcept
    {
        CFRunLoopRemoveSource (runLoop, runLoopSource.get(), kCFRunLoopCommonModes);
        CFRunLoopSourceInvalidate (runLoopSource.get());
    }

    void post (MessageManager::MessageBase* const message)
    {
        messages.add (message);
        wakeUp();
    }

private:
    ReferenceCountedArray<MessageManager::MessageBase, CriticalSection> messages;
    CFRunLoopRef runLoop;
    CFUniquePtr<CFRunLoopSourceRef> runLoopSource;

    void wakeUp() noexcept
    {
        YUP_PROFILE_INTERNAL_TRACE();

        CFRunLoopSourceSignal (runLoopSource.get());
        CFRunLoopWakeUp (runLoop);
    }

    bool deliverNextMessage()
    {
        YUP_PROFILE_INTERNAL_TRACE();

        const MessageManager::MessageBase::Ptr nextMessage (messages.removeAndReturn (0));

        if (nextMessage == nullptr)
            return false;

        YUP_AUTORELEASEPOOL
        {
            YUP_TRY
            {
                nextMessage->messageCallback();
            }
            YUP_CATCH_EXCEPTION
        }

        return true;
    }

    void runLoopCallback() noexcept
    {
        YUP_PROFILE_INTERNAL_TRACE();

        if (! deliverNextMessage())
            return;

        for (int i = 3; --i >= 0;)
        {
            if (! deliverNextMessage())
                break;
        }

        wakeUp();
    }

    static void runLoopSourceCallback (void* info) noexcept
    {
        static_cast<InternalMessageQueue*> (info)->runLoopCallback();
    }
};

} // namespace yup
