// Author: Kouek Kou

#include "Data.h"

#include <array>
#include <map>
#include <algorithm>

TVariant<UVolumeTexture *, FString>
VolumeData::LoadFromFile(const LoadFromFileDesc &Desc,
                         TOptional<std::reference_wrapper<TArray<uint8>>> VolumeOut) {
    using RetType = TVariant<UVolumeTexture *, FString>;

    if (Desc.Dimension.X <= 0 || Desc.Dimension.Y <= 0 || Desc.Dimension.Z <= 0)
        return RetType(TInPlaceType<FString>(), FString::Format(TEXT("Invalid Desc.Dimension {0}."),
                                                                {Desc.Dimension.ToString()}));

    TArray<uint8> buf;
    if (!FFileHelper::LoadFileToArray(buf, *Desc.FilePath.FilePath))
        return RetType(TInPlaceType<FString>(), FString::Format(TEXT("Invalid Desc.FilePath {0}."),
                                                                {Desc.FilePath.FilePath}));

    auto pixFmt = GetVoxelPixelFormat(Desc.VoxTy);
    auto transform = [&]() {
        using TrRetType = TVariant<FIntVector, FString>;

        if ([&]() {
                FIntVector cnts(0, 0, 0);
                for (int i = 0; i < 3; ++i)
                    if (auto j = std::abs(Desc.Axis[i]) - 1; 0 <= j && j <= 2)
                        ++cnts[j];
                    else
                        return true;
                for (int i = 0; i < 3; ++i)
                    if (cnts[i] != 1)
                        return true;
                return false;
            }())
            return TrRetType(TInPlaceType<FString>(),
                             FString::Format(TEXT("Invalid Desc.Axis {0},{1},{2}."),
                                             {Desc.Axis[0], Desc.Axis[1], Desc.Axis[2]}));

        if (Desc.Axis == decltype(Desc.Axis)(1, 2, 3))
            return TrRetType(TInPlaceType<FIntVector>(), Desc.Dimension);

        FIntVector trAxisMap(std::abs(Desc.Axis[0]) - 1, std::abs(Desc.Axis[1]) - 1,
                              std::abs(Desc.Axis[2]) - 1);
        FIntVector trDim(Desc.Dimension[trAxisMap[0]], Desc.Dimension[trAxisMap[1]],
                          Desc.Dimension[trAxisMap[2]]);

        decltype(buf) oldBuf = buf;
        auto trVoxYxX = static_cast<size_t>(trDim.Y) * trDim.X;
        size_t offs = 0;
        FIntVector coord;
        for (coord.Z = 0; coord.Z < Desc.Dimension.Z; ++coord.Z)
            for (coord.Y = 0; coord.Y < Desc.Dimension.Y; ++coord.Y)
                for (coord.X = 0; coord.X < Desc.Dimension.X; ++coord.X) {
                    FIntVector trCoord(
                        Desc.Axis.X > 0 ? coord[trAxisMap[0]] : trDim.X - 1 - coord[trAxisMap[0]],
                        Desc.Axis.Y > 0 ? coord[trAxisMap[1]] : trDim.Y - 1 - coord[trAxisMap[1]],
                        Desc.Axis.Z > 0 ? coord[trAxisMap[2]] : trDim.Z - 1 - coord[trAxisMap[2]]);
                    buf[trCoord.Z * trVoxYxX + trCoord.Y * trDim.X + trCoord.X] = oldBuf[offs];
                    ++offs;
                }

        return TrRetType(TInPlaceType<FIntVector>(), trDim);
    };

    auto load = [&]<SupportedVoxelType T>(T *dat) -> RetType {
        auto volSz = sizeof(T) * Desc.Dimension.X * Desc.Dimension.Y * Desc.Dimension.Z;

        if (buf.Num() != volSz)
            return RetType(TInPlaceType<FString>(),
                           FString::Format(TEXT("Invalid contents in Desc.FilePath {0}."),
                                           {Desc.FilePath.FilePath}));
        FIntVector trDim;
        if (auto ret = transform(); ret.IsType<FString>())
            return RetType(TInPlaceType<FString>(), ret.Get<FString>());
        else
            trDim = ret.Get<FIntVector>();

        UVolumeTexture *VolumeTexturet = NewObject<UVolumeTexture>(UVolumeTexture::StaticClass());
        VolumeTexturet->PlatformData = new FTexturePlatformData();
        VolumeTexturet->PlatformData->SizeX = trDim.X;
        VolumeTexturet->PlatformData->SizeY = trDim.Y;
        VolumeTexturet->PlatformData->SetNumSlices(trDim.Z);
        VolumeTexturet->PlatformData->PixelFormat = pixFmt;

        auto tex = VolumeTexturet;
        tex->Filter = TextureFilter::TF_Trilinear;
        #if WITH_EDITOR
            tex->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
        #endif
        //tex->AddressMode = TextureAddress::TA_Clamp;

        auto platformData = *tex->GetRunningPlatformData();
        auto *texDat = platformData->Mips[0].BulkData.Lock(EBulkDataLockFlags::LOCK_READ_WRITE);
        FMemory::Memcpy(texDat, dat, volSz);
        platformData->Mips[0].BulkData.Unlock();

        tex->UpdateResource();

        if (VolumeOut.IsSet())
            VolumeOut->get() = std::move(buf);

        return RetType(TInPlaceType<UVolumeTexture *>(), tex);
    };

    switch (Desc.VoxTy) {
    case ESupportedVoxelType::UInt8:
        return load(reinterpret_cast<uint8 *>(buf.GetData()));
    case ESupportedVoxelType::UInt16:
        return load(reinterpret_cast<uint16 *>(buf.GetData()));
    case ESupportedVoxelType::Float32:
        return load(reinterpret_cast<float *>(buf.GetData()));
    default:
        return RetType(TInPlaceType<FString>(), TEXT("Invalid Desc.VoxTy."));
    }
}

TVariant<UVolumeTexture *, FString>
VolumeData::SmoothFromFlatArray(const SmoothFromFlatArrayDesc &Desc,
                                TOptional<std::reference_wrapper<TArray<uint8>>> SmoothedVolOut) {
    using RetType = TVariant<UVolumeTexture *, FString>;

    auto voxPerVolYxX = static_cast<uint64>(Desc.Dimension.Y) * Desc.Dimension.X;

    TArray<uint8> buf;
    buf.SetNum(Desc.VolDat.Num());

    auto smoothKernelMax = [&]<SupportedVoxelType T>(const T *dat, const FIntVector &pos) -> T {
        auto scalar = std::numeric_limits<T>::min();
        FIntVector dPos(pos.X == 0 ? 0 : -1, pos.Y == 0 ? 0 : -1,
                         Desc.SmoothDim == EVolumeSmoothDimension::XY || pos.Z == 0 ? 0 : -1);
        for (; dPos.Z <
               (Desc.SmoothDim == EVolumeSmoothDimension::XY || pos.Z == Desc.Dimension.Z - 1 ? 1
                                                                                              : 2);
             ++dPos.Z)
            for (; dPos.Y < (pos.Y == Desc.Dimension.Y - 1 ? 1 : 2); ++dPos.Y)
                for (; dPos.X < (pos.X == Desc.Dimension.X - 1 ? 1 : 2); ++dPos.X) {
                    auto newPos = pos + dPos;
                    scalar = std::max(
                        scalar,
                        dat[newPos.Z * voxPerVolYxX + newPos.Y * Desc.Dimension.X + newPos.X]);
                }
        return scalar;
    };
    auto smoothKernelAvg = [&]<SupportedVoxelType T>(const T *dat, const FIntVector &pos) -> T {
        float scalar = 0.f;
        int32 num = 0.f;
        FIntVector dPos(pos.X == 0 ? 0 : -1, pos.Y == 0 ? 0 : -1,
                         Desc.SmoothDim != EVolumeSmoothDimension::XY || pos.Z == 0 ? 0 : -1);
        for (; dPos.Z <
               (Desc.SmoothDim != EVolumeSmoothDimension::XY || pos.Z == Desc.Dimension.Z - 1 ? 1
                                                                                              : 2);
             ++dPos.Z)
            for (; dPos.Y < (pos.Y == Desc.Dimension.Y - 1 ? 1 : 2); ++dPos.Y)
                for (; dPos.X < (pos.X == Desc.Dimension.X - 1 ? 1 : 2); ++dPos.X) {
                    auto newPos = pos + dPos;
                    scalar += dat[newPos.Z * voxPerVolYxX + newPos.Y * Desc.Dimension.X + newPos.X];
                    ++num;
                }
        if constexpr (std::is_floating_point_v<T>)
            return scalar / num;
        else
            return static_cast<T>(std::roundf(scalar / num));
    };
    auto smooth = [&]<SupportedVoxelType T>(T *newDat, const T *oldDat) -> RetType {
        int32 idx = 0;
        FIntVector pos;
        for (pos.Z = 0; pos.Z < Desc.Dimension.Z; ++pos.Z)
            for (pos.Y = 0; pos.Y < Desc.Dimension.Y; ++pos.Y)
                for (pos.X = 0; pos.X < Desc.Dimension.X; ++pos.X) {
                    newDat[idx] = Desc.SmoothTy == EVolumeSmoothType::Avg
                                      ? smoothKernelAvg(oldDat, pos)
                                      : smoothKernelMax(oldDat, pos);
                    ++idx;
                }

        auto volSz = sizeof(T) * Desc.Dimension.X * Desc.Dimension.Y * Desc.Dimension.Z;
        if (Desc.VolDat.Num() != volSz)
            return RetType(
                TInPlaceType<FString>(),
                FString::Format(
                    TEXT("Size of Desc.VolDat {0} is not the same as Desc.Dimension {1}."),
                    {Desc.VolDat.Num(), Desc.Dimension.ToString()}));
        UVolumeTexture *VolumeTexturet = NewObject<UVolumeTexture>(UVolumeTexture::StaticClass());
        VolumeTexturet->PlatformData = new FTexturePlatformData();
        VolumeTexturet->PlatformData->SizeX = Desc.Dimension.X;
        VolumeTexturet->PlatformData->SizeY = Desc.Dimension.Y;
        VolumeTexturet->PlatformData->SetNumSlices(Desc.Dimension.Z);
        VolumeTexturet->PlatformData->PixelFormat = GetVoxelPixelFormat(Desc.VoxTy);

        auto tex = VolumeTexturet;
        tex->Filter = TextureFilter::TF_Trilinear;
        #if WITH_EDITOR
                tex->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
        #endif
        //tex->AddressMode = TextureAddress::TA_Clamp;
        auto platformData = *tex->GetRunningPlatformData();
        auto *texDat =
            platformData->Mips[0].BulkData.Lock(EBulkDataLockFlags::LOCK_READ_WRITE);
        FMemory::Memcpy(texDat, newDat, volSz);
        platformData->Mips[0].BulkData.Unlock();

        tex->UpdateResource();

        if (SmoothedVolOut.IsSet())
            SmoothedVolOut->get() = std::move(buf);

        return RetType(TInPlaceType<UVolumeTexture *>(), tex);
    };

    switch (Desc.VoxTy) {
    case ESupportedVoxelType::UInt8:
        return smooth(reinterpret_cast<uint8 *>(buf.GetData()),
                      reinterpret_cast<const uint8 *>(Desc.VolDat.GetData()));
    default:
        return RetType(TInPlaceType<FString>(), TEXT("Invalid Desc.VoxTy."));
    }
}

TVariant<TTuple<UTexture2D *, UCurveLinearColor *>, FString>
TransferFunctionData::LoadFromFile(const Desc &Desc) {
    using ValueType = TTuple<UTexture2D *, UCurveLinearColor *>;
    using RetType = TVariant<ValueType, FString>;

    FJsonSerializableArray buf;
    if (!FFileHelper::LoadANSITextFileToStrings(*Desc.FilePath.FilePath, nullptr, buf))
        return RetType(TInPlaceType<FString>(), FString::Format(TEXT("Invalid Desc.FilePath {0}."),
                                                                {Desc.FilePath.FilePath}));

    TMap<float, FVector4f> pnts;
    for (int i = 0; i < buf.Num(); ++i) {
        if (buf[i].IsEmpty())
            continue;

        float lnVars[5] = {0.f};
        auto validCnt = sscanf_s(TCHAR_TO_ANSI(*buf[i]), "%f%f%f%f%f", &lnVars[0], &lnVars[1],
                               &lnVars[2], &lnVars[3], &lnVars[4]);
        if (validCnt != 5 || [&]() {
                for (int i = 0; i < 5; ++i)
                    if (lnVars[i] < 0.f || lnVars[i] >= 255.5f)
                        return true;
                return false;
            }())
            return RetType(
                TInPlaceType<FString>(),
                FString::Format(TEXT("Invalid contents at line {0} in Desc.FilePath {1}."),
                                {i + 1, Desc.FilePath.FilePath}));

        auto &v4 = pnts.Emplace(lnVars[0]);
        for (int j = 0; j < 4; ++j)
            v4[j] = std::min(std::max(lnVars[j + 1] / 255.f, 0.f), 1.f);
    }

    auto tex = FromFlatArrayToTexture(LerpFromPointsToFlatArray(pnts), Desc.Name);
    auto curve = FromPointsToCurve(pnts);

    return RetType(TInPlaceType<ValueType>(), tex, curve);
}

TOptional<FString> TransferFunctionData::SaveToFile(const UCurveLinearColor *Curve,
                                                    const FFilePath &FilePath) {
    if (!Curve)
        return FString("Invalid Curve.");

    std::map<float, FVector4f> pnts;
    {
        std::array<const FRichCurve *, 4> curves = {&Curve->FloatCurves[0], &Curve->FloatCurves[1],
                                                    &Curve->FloatCurves[2], &Curve->FloatCurves[3]};
        for (int32 i = 0; i < 4; ++i)
            for (auto itr = curves[i]->GetKeyIterator(); itr; ++itr) {
                auto pnt = pnts.find(itr->Time);
                if (pnt == pnts.end()) {
                    auto [pntItr, _] =
                        pnts.emplace(itr->Time, Curve->GetLinearColorValue(itr->Time));
                    pnt = pntItr;
                }
                pnt->second[i] = std::clamp(itr->Value * 255.f, 0.f, 255.f);
            }
    }

    FJsonSerializableArray buf;
    for (auto [scalar, rgba] : pnts)
        buf.Add(
            FString::Format(TEXT("{0} {1} {2} {3} {4}"), {scalar, rgba.X, rgba.Y, rgba.Z, rgba.W}));

    if (!FFileHelper::SaveStringArrayToFile(buf, *FilePath.FilePath,
                                            FFileHelper::EEncodingOptions::ForceAnsi))
        return FString::Format(TEXT("Invalid Desc.FilePath {0}."), {FilePath.FilePath});
    return {};
}

void TransferFunctionData::FromFlatArrayToTexture(UTexture2D *Tex, const TArray<FFloat16> &Dat) {
    auto *texDat =
        Tex->GetPlatformData()->Mips[0].BulkData.Lock(EBulkDataLockFlags::LOCK_READ_WRITE);
    FMemory::Memmove(texDat, Dat.GetData(), ElemSz * Resolution);
    Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
    Tex->UpdateResource();
}

UTexture2D *TransferFunctionData::FromFlatArrayToTexture(const TArray<FFloat16> &Dat,
                                                         const FName &Name) {
    auto tex = UTexture2D::CreateTransient(Resolution, 1, PF_FloatRGBA, Name);

    tex->Filter = TextureFilter::TF_Bilinear;
    #if WITH_EDITOR
        tex->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
    #endif
    tex->AddressX = tex->AddressY = TextureAddress::TA_Clamp;
    FromFlatArrayToTexture(tex, Dat);

    return tex;
}

void TransferFunctionData::FromPointsToCurve(UCurveLinearColor *Curve,
                                             const TMap<float, FVector4f> &Pnts) {
    Curve->ResetCurve();
    std::array<FRichCurve *, 4> curves = {&Curve->FloatCurves[0], &Curve->FloatCurves[1],
                                          &Curve->FloatCurves[2], &Curve->FloatCurves[3]};
    for (auto &[scalar, rgba] : Pnts)
        for (int32 i = 0; i < 4; ++i)
            curves[i]->AddKey(scalar, rgba[i]);
}

UCurveLinearColor *TransferFunctionData::FromPointsToCurve(const TMap<float, FVector4f> &Pnts) {
    auto curve = NewObject<UCurveLinearColor>();
    FromPointsToCurve(curve, Pnts);

    return curve;
}

TArray<FFloat16>
TransferFunctionData::LerpFromPointsToFlatArray(const TMap<float, FVector4f> &Pnts) {
    TArray<FFloat16> dat;

    dat.Reserve(Resolution * 4);

    auto pnt = Pnts.begin();
    auto prevPnt = pnt;
    ++pnt;
    for (int scalar = 0; scalar < Resolution; ++scalar) {
        if (scalar > pnt->Key) {
            ++prevPnt;
            ++pnt;
        }
        auto &[s, c] = *pnt;
        auto &[prevS, prevC] = *prevPnt;

        auto k = s == prevS ? 1.f : (scalar - prevS) / (s - prevS);
        auto rgba = (1.f - k) * prevC + k * c;

        dat.Emplace(rgba.X);
        dat.Emplace(rgba.Y);
        dat.Emplace(rgba.Z);
        dat.Emplace(rgba.W);
    }

    return dat;
}
