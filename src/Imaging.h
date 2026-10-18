// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sh4
#pragma once
#include "ShellDesktop.h"
#include <gdiplus.h>
#include <wincodec.h>
#include <map>

namespace deka {
struct Picture {
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    UINT sourceWidth = 0, sourceHeight = 0;
};

inline std::shared_ptr<Picture> ConvertBitmap(IWICBitmapSource* source,
                                              IWICImagingFactory* factory) {
    auto result = std::make_shared<Picture>();
    Require(SUCCEEDED(source->GetSize(&result->sourceWidth, &result->sourceHeight)),
            "Image size unavailable");
    Require(result->sourceWidth > 0 && result->sourceHeight > 0 && result->sourceWidth <= 16384 &&
                result->sourceHeight <= 16384 &&
                UINT64(result->sourceWidth) * result->sourceHeight <= 64000000,
            "Image exceeds pixel limit");
    ComPtr<IWICBitmapSource> scaled = source;
    if (std::max(result->sourceWidth, result->sourceHeight) > 1024) {
        double scale = 1024.0 / std::max(result->sourceWidth, result->sourceHeight);
        ComPtr<IWICBitmapScaler> scaler;
        Require(SUCCEEDED(factory->CreateBitmapScaler(&scaler)), "Scaler unavailable");
        Require(
            SUCCEEDED(scaler->Initialize(source, std::max(1u, UINT(result->sourceWidth * scale)),
                                         std::max(1u, UINT(result->sourceHeight * scale)),
                                         WICBitmapInterpolationModeFant)),
            "Image scaling failed");
        scaler.As(&scaled);
    }
    ComPtr<IWICFormatConverter> converter;
    Require(SUCCEEDED(factory->CreateFormatConverter(&converter)), "Converter unavailable");
    Require(SUCCEEDED(converter->Initialize(scaled.Get(), GUID_WICPixelFormat32bppPBGRA,
                                            WICBitmapDitherTypeNone, nullptr, 0,
                                            WICBitmapPaletteTypeCustom)),
            "Image conversion failed");
    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    result->bitmap = std::make_unique<Gdiplus::Bitmap>(w, h, PixelFormat32bppPARGB);
    Gdiplus::Rect rect(0, 0, static_cast<INT>(w), static_cast<INT>(h));
    Gdiplus::BitmapData data{};
    Require(result->bitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB,
                                     &data) == Gdiplus::Ok,
            "Cannot lock image");
    HRESULT hr =
        converter->CopyPixels(nullptr, static_cast<UINT>(data.Stride),
                              static_cast<UINT>(data.Stride) * h, static_cast<BYTE*>(data.Scan0));
    result->bitmap->UnlockBits(&data);
    Require(SUCCEEDED(hr), "Cannot copy pixels");
    return result;
}

inline std::shared_ptr<Picture> LoadPicture(const Item& item) {
    ComPtr<IWICImagingFactory> factory;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&factory))),
            "WIC unavailable");
    if (!item.overrideImage.empty()) {
        ComPtr<IWICBitmapDecoder> decoder;
        Require(SUCCEEDED(factory->CreateDecoderFromFilename(
                    item.overrideImage.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                    &decoder)),
                "Cannot decode override image");
        ComPtr<IWICBitmapFrameDecode> frame;
        Require(SUCCEEDED(decoder->GetFrame(0, &frame)), "Image frame unavailable");
        return ConvertBitmap(frame.Get(), factory.Get());
    }
    ComPtr<IShellItemImageFactory> shell;
    Require(SUCCEEDED(SHCreateItemFromParsingName(item.key.c_str(), nullptr, IID_PPV_ARGS(&shell))),
            "Shell image factory unavailable");
    HBITMAP bitmap = nullptr;
    // Let the Shell prefer thumbnails and fall back to icons for other items.
    // Request our maximum drawing size; smaller results are scaled by the renderer.
    Require(SUCCEEDED(shell->GetImage(SIZE{1024, 1024}, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK,
                                      &bitmap)),
            "Shell image unavailable");
    ComPtr<IWICBitmap> wic;
    HRESULT hr =
        factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUsePremultipliedAlpha, &wic);
    DeleteObject(bitmap);
    Require(SUCCEEDED(hr), "Shell bitmap conversion failed");
    return ConvertBitmap(wic.Get(), factory.Get());
}

inline CLSID PngEncoder() {
    UINT count = 0, bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    std::vector<BYTE> buffer(bytes);
    auto codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    Require(Gdiplus::GetImageEncoders(count, bytes, codecs) == Gdiplus::Ok,
            "Image encoders unavailable");
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            return codecs[i].Clsid;
        }
    }
    throw std::runtime_error("PNG encoder unavailable");
}
} // namespace deka
