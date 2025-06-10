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

#pragma once

/*
   This file provides flags for compiler features that aren't supported on all platforms.
*/

//==============================================================================
// GCC
#if YUP_GCC

#if (__GNUC__ * 100 + __GNUC_MINOR__) < 700
#error "YUP requires GCC 7.0 or later"
#endif

#ifndef YUP_EXCEPTIONS_DISABLED
#if ! __EXCEPTIONS
#define YUP_EXCEPTIONS_DISABLED 1
#endif
#endif

#define YUP_CXX17_IS_AVAILABLE (__cplusplus >= 201703L)
#define YUP_CXX20_IS_AVAILABLE (__cplusplus >= 202002L)

#endif

//==============================================================================
// Clang
#if YUP_CLANG

#if (__clang_major__ < 6)
#error "YUP requires Clang 6 or later"
#endif

#ifndef YUP_COMPILER_SUPPORTS_ARC
#define YUP_COMPILER_SUPPORTS_ARC 1
#endif

#ifndef YUP_EXCEPTIONS_DISABLED
#if ! __has_feature(cxx_exceptions)
#define YUP_EXCEPTIONS_DISABLED 1
#endif
#endif

#if ! defined(YUP_SILENCE_XCODE_15_LINKER_WARNING) \
    && defined(__apple_build_version__)            \
    && __apple_build_version__ >= 15000000         \
    && __apple_build_version__ < 15000100

    // Due to known issues, the linker in Xcode 15.0 may produce broken binaries.
#error Please upgrade to Xcode 15.1 or higher
#endif

#define YUP_CXX17_IS_AVAILABLE (__cplusplus >= 201703L)
#define YUP_CXX20_IS_AVAILABLE (__cplusplus >= 202002L)

#endif

//==============================================================================
// MSVC
#if YUP_MSVC

#if _MSC_FULL_VER < 191025017 // VS2017
#error "YUP requires Visual Studio 2017 or later"
#endif

#ifndef YUP_EXCEPTIONS_DISABLED
#if ! _CPPUNWIND
#define YUP_EXCEPTIONS_DISABLED 1
#endif
#endif

#define YUP_CXX17_IS_AVAILABLE (_MSVC_LANG >= 201703L)
#define YUP_CXX20_IS_AVAILABLE (_MSVC_LANG >= 202002L)
#endif

//==============================================================================
#if ! YUP_CXX17_IS_AVAILABLE
#error "YUP requires C++17 or later"
#endif

//==============================================================================
#ifndef DOXYGEN
// These are old flags that are now supported on all compatible build targets
#define YUP_CXX14_IS_AVAILABLE 1
#define YUP_COMPILER_SUPPORTS_OVERRIDE_AND_FINAL 1
#define YUP_COMPILER_SUPPORTS_VARIADIC_TEMPLATES 1
#define YUP_COMPILER_SUPPORTS_INITIALIZER_LISTS 1
#define YUP_COMPILER_SUPPORTS_NOEXCEPT 1
#define YUP_DELETED_FUNCTION = delete
#define YUP_CONSTEXPR constexpr
#define YUP_NODISCARD [[nodiscard]]
#endif
