// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#pragma once
#include "Imaging.h"

namespace deka {
inline std::shared_ptr<Picture> ReferenceShellPicture(const std::wstring& path, SIIGBF flags) {
    ComPtr<IShellItemImageFactory> shell;
    Require(SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&shell))),
            "Test Shell image factory unavailable");
    HBITMAP bitmap = nullptr;
    Require(SUCCEEDED(shell->GetImage(SIZE{1024, 1024}, flags | SIIGBF_BIGGERSIZEOK, &bitmap)),
            "Test Shell image unavailable");
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    ComPtr<IWICBitmap> wic;
    if (SUCCEEDED(hr)) {
        hr =
            factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUsePremultipliedAlpha, &wic);
    }
    DeleteObject(bitmap);
    Require(SUCCEEDED(hr), "Test Shell bitmap conversion failed");
    return ConvertBitmap(wic.Get(), factory.Get());
}

inline void RequireSamePicture(const Picture& actual, const Picture& expected) {
    Require(actual.sourceWidth == expected.sourceWidth &&
                actual.sourceHeight == expected.sourceHeight &&
                actual.bitmap->GetWidth() == expected.bitmap->GetWidth() &&
                actual.bitmap->GetHeight() == expected.bitmap->GetHeight(),
            "Loaded image dimensions differ from the expected image");
    for (UINT y = 0; y < actual.bitmap->GetHeight(); y += 7) {
        for (UINT x = 0; x < actual.bitmap->GetWidth(); x += 7) {
            Gdiplus::Color a, b;
            Require(actual.bitmap->GetPixel(x, y, &a) == Gdiplus::Ok &&
                        expected.bitmap->GetPixel(x, y, &b) == Gdiplus::Ok &&
                        a.GetValue() == b.GetValue(),
                    "Loaded image pixels differ from the expected image");
        }
    }
}

inline void CheckImaging(const std::wstring& imagePath, const std::wstring& filePath) {
    Item image;
    image.key = imagePath;
    auto thumbnail = ReferenceShellPicture(imagePath, SIIGBF_THUMBNAILONLY);
    auto actual = LoadPicture(image);
    RequireSamePicture(*actual, *thumbnail);
    Require(actual->sourceWidth > 256 && actual->sourceWidth > actual->sourceHeight,
            "Thumbnail resolution or landscape aspect ratio was lost");
    Gdiplus::Color color;
    Require(actual->bitmap->GetPixel(actual->bitmap->GetWidth() / 2,
                                     actual->bitmap->GetHeight() * 7 / 10, &color) == Gdiplus::Ok &&
                color.GetA() > 240 && color.GetG() > 180 && color.GetR() < 80,
            "Thumbnail content is missing or transparent");

    Item file;
    file.key = filePath;
    bool hasThumbnail = false;
    try {
        ReferenceShellPicture(filePath, SIIGBF_THUMBNAILONLY);
        hasThumbnail = true;
    } catch (...) {
    }
    Require(!hasThumbnail, "Fallback fixture unexpectedly has a thumbnail provider");
    RequireSamePicture(*LoadPicture(file), *ReferenceShellPicture(filePath, SIIGBF_ICONONLY));

    file.overrideImage = imagePath;
    auto override = LoadPicture(file);
    Require(override->sourceWidth == 1024 && override->sourceHeight == 512,
            "Explicit image override was not loaded at its original size");
    Require(override->bitmap->GetPixel(0, 0, &color) == Gdiplus::Ok && color.GetA() == 0,
            "Explicit image override lost its transparent background");
}
} // namespace deka
