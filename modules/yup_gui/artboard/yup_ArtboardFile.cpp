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

#include "yup_gui.h"

#include "artboard/yup_ArtboardFile.h"

#include "yup_core/containers/yup_MemoryBlock.h"

#include "rive/assets/file_asset.hpp"
#include "rive/file.hpp"
#include "rive/simple_array.hpp"

#include <utility>
#include <vector>

namespace yup
{

namespace
{

[[nodiscard]] bool decodeAssetFromBytes (rive::FileAsset& asset,
                                         Span<const uint8> bytes,
                                         rive::Factory& factory)
{
    if (bytes.empty())
        return false;

    rive::SimpleArray<uint8_t> data (bytes.data(), bytes.size());
    return asset.decode (data, &factory);
}

[[nodiscard]] bool decodeAssetFromFile (const File& file,
                                        rive::FileAsset& asset,
                                        rive::Factory& factory)
{
    if (! file.existsAsFile())
        return false;

    MemoryBlock block;
    if (! file.loadFileAsData (block))
        return false;

    rive::SimpleArray<uint8_t> data (static_cast<const uint8*> (block.getData()), block.getSize());
    return asset.decode (data, &factory);
}

class LambdaAssetLoader : public rive::FileAssetLoader
{
public:
    LambdaAssetLoader (ArtboardFile::AssetLoadCallback assetCallbackIn)
        : assetCallback (std::move (assetCallbackIn))
    {
    }

    bool loadContents (rive::FileAsset& asset,
                       rive::Span<const uint8_t> inBandBytes,
                       rive::Factory* factory) override
    {
        jassert (factory != nullptr);

        ArtboardFile::AssetInfo assetInfo;
        assetInfo.uniqueName = String (asset.uniqueName());
        assetInfo.uniquePath = File (String (asset.uniqueFilename()));
        assetInfo.extension = String (asset.fileExtension());

        return assetCallback (
            assetInfo,
            asset,
            Span<const uint8> { inBandBytes.data(), inBandBytes.size() },
            *factory);
    }

private:
    ArtboardFile::AssetLoadCallback assetCallback;
};

[[nodiscard]] ArtboardFile::AssetLoadCallback makeDefaultAssetLoader (const File& baseDirectory)
{
    return [baseDirectory] (const ArtboardFile::AssetInfo& info,
                            rive::FileAsset& asset,
                            Span<const uint8> inBandBytes,
                            rive::Factory& factory)
    {
        if (decodeAssetFromBytes (asset, inBandBytes, factory))
            return true;

        if (! baseDirectory.isDirectory())
            return false;

        std::vector<File> candidates;
        const auto uniqueFilename = String (asset.uniqueFilename());
        if (uniqueFilename.isNotEmpty())
            candidates.push_back (baseDirectory.getChildFile (uniqueFilename));

        const auto infoPath = info.uniquePath;
        if (infoPath.getFullPathName().isNotEmpty())
        {
            if (infoPath.isAbsolutePath())
                candidates.push_back (infoPath);
            else
                candidates.push_back (baseDirectory.getChildFile (infoPath.getFullPathName()));
        }

        if (info.uniqueName.isNotEmpty() && info.extension.isNotEmpty())
            candidates.push_back (baseDirectory.getChildFile (info.uniqueName + "." + info.extension));

        for (const auto& candidate : candidates)
        {
            if (decodeAssetFromFile (candidate, asset, factory))
                return true;
        }

        return false;
    };
}

[[nodiscard]] ArtboardFile::AssetLoadCallback combineAssetCallbacks (ArtboardFile::AssetLoadCallback primary,
                                                                     ArtboardFile::AssetLoadCallback fallback)
{
    if (! primary)
        return fallback;

    if (! fallback)
        return primary;

    return [primary = std::move (primary), fallback = std::move (fallback)] (const ArtboardFile::AssetInfo& info,
                                                                             rive::FileAsset& asset,
                                                                             Span<const uint8> bytes,
                                                                             rive::Factory& factory)
    {
        if (primary (info, asset, bytes, factory))
            return true;

        return fallback (info, asset, bytes, factory);
    };
}

} // namespace

//==============================================================================

ArtboardFile::ArtboardFile (std::unique_ptr<rive::File> rivFile)
    : rivFile (std::move (rivFile))
{
}

//==============================================================================

const rive::File* ArtboardFile::getRiveFile() const
{
    return rivFile.get();
}

rive::File* ArtboardFile::getRiveFile()
{
    return rivFile.get();
}

//==============================================================================

ArtboardFile::LoadResult ArtboardFile::load (const File& file, rive::Factory& factory)
{
    return load (file, factory, AssetLoadCallback());
}

ArtboardFile::LoadResult ArtboardFile::load (const File& file, rive::Factory& factory, const AssetLoadCallback& assetCallback)
{
    if (! file.existsAsFile())
        return LoadResult::fail ("Failed to find artboard file to load");

    auto is = file.createInputStream();
    if (is == nullptr || ! is->openedOk())
        return LoadResult::fail ("Failed to open artboard file for reading");

    auto effectiveCallback = combineAssetCallbacks (assetCallback, makeDefaultAssetLoader (file.getParentDirectory()));
    return load (*is, factory, effectiveCallback);
}

//==============================================================================

ArtboardFile::LoadResult ArtboardFile::load (InputStream& is, rive::Factory& factory)
{
    return load (is, factory, AssetLoadCallback());
}

ArtboardFile::LoadResult ArtboardFile::load (InputStream& is, rive::Factory& factory, const AssetLoadCallback& assetCallback)
{
    yup::MemoryBlock mb;
    is.readIntoMemoryBlock (mb);

    rive::ImportResult result;
    std::unique_ptr<rive::File> rivFile;

    auto effectiveCallback = combineAssetCallbacks (assetCallback, makeDefaultAssetLoader (File {}));

    rivFile = rive::File::import (
        { static_cast<const uint8_t*> (mb.getData()), mb.getSize() },
        std::addressof (factory),
        std::addressof (result),
        rive::make_rcp<LambdaAssetLoader> (effectiveCallback));

    if (result == rive::ImportResult::malformed)
        return LoadResult::fail ("Malformed artboard file");

    if (result == rive::ImportResult::unsupportedVersion)
        return LoadResult::fail ("Unsupported artboard file for current runtime");

    if (rivFile == nullptr)
        return LoadResult::fail ("Failed to import artboard file");

    return LoadResult::ok (std::shared_ptr<ArtboardFile> (new ArtboardFile { std::move (rivFile) }));
}

} // namespace yup
