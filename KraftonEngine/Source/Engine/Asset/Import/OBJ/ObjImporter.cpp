/**
 * OBJ/MTL 원본 파일을 엔진의 StaticMesh 데이터로 변환하는 파서를 구현한다.
 *
 * 텍스트 기반 OBJ 라인을 읽어 위치, UV, 노말, face, material library 정보를 수집하고, OBJ의
 * 인덱스 조합을 엔진 정점 버퍼 형식으로 재구성한다. ImportOptions에 따라 forward 축, winding,
 * 스케일, 기본 머티리얼 생성 여부를 반영하며, 변환 결과는 ObjManager가 바이너리 캐시나
 * UStaticMesh로 이어서 사용할 수 있는 중간 데이터가 된다.
 */

#include "Asset/Import/OBJ/ObjImporter.h"
#include "Asset/Mesh/StaticMesh/StaticMeshAsset.h"
#include "Asset/Material/Material.h"
#include "Core/Log.h"
#include "Engine/Platform/Paths.h"
#include "Asset/Import/OBJ/ObjManager.h"
#include "SimpleJSON/json.hpp"
#include "Asset/Material/MaterialManager.h"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <charconv>
#include <chrono>

const FVector  FallbackColor3 = FVector(1.0f, 0.0f, 1.0f);
const FVector4 FallbackColor4 = FVector4(1.0f, 0.0f, 1.0f, 1.0f);

struct FVertexKey
{
    uint32 p, t, n;
    bool   operator==(const FVertexKey &Other) const
    {
        return p == Other.p && t == Other.t && n == Other.n;
    }
};

namespace std
{
    template <> struct hash<FVertexKey>
    {
        size_t operator()(const FVertexKey &Key) const noexcept
        {
            return ((size_t)Key.p) ^ (((size_t)Key.t) << 8) ^ (((size_t)Key.n) << 16);
        }
    };
} // namespace std

struct FStringParser
{

    static std::string_view GetNextToken(std::string_view &InOutView, char Delimiter = ' ')
    {
        size_t           DelimiterPosition = InOutView.find(Delimiter);
        std::string_view Token = InOutView.substr(0, DelimiterPosition);
        if (DelimiterPosition != std::string_view::npos)
        {
            InOutView.remove_prefix(DelimiterPosition + 1);
        }
        else
        {
            InOutView = std::string_view();
        }
        return Token;
    }

    static std::string_view GetNextWhitespaceToken(std::string_view &InOutView)
    {
        size_t Start = InOutView.find_first_not_of(" \t");
        if (Start == std::string_view::npos)
        {
            InOutView = std::string_view();
            return std::string_view();
        }
        InOutView.remove_prefix(Start);

        size_t           End = InOutView.find_first_of(" \t");
        std::string_view Token = InOutView.substr(0, End);

        if (End != std::string_view::npos)
        {
            InOutView.remove_prefix(End);
        }
        else
        {
            InOutView = std::string_view();
        }
        return Token;
    }

    static void TrimLeft(std::string_view &InOutView)
    {
        size_t Start = InOutView.find_first_not_of(" \t");
        if (Start != std::string_view::npos)
        {
            InOutView.remove_prefix(Start);
        }
        else
        {
            InOutView = std::string_view();
        }
    }

    static bool ParseInt(std::string_view Str, int &OutValue)
    {
        if (Str.empty())
            return false;
        std::from_chars_result result =
            std::from_chars(Str.data(), Str.data() + Str.size(), OutValue);
        return result.ec == std::errc();
    }

    static bool ParseFloat(std::string_view Str, float &OutValue)
    {
        if (Str.empty())
            return false;
        std::from_chars_result result =
            std::from_chars(Str.data(), Str.data() + Str.size(), OutValue);
        return result.ec == std::errc();
    }
};

struct FRawFaceVertex
{
    int32 PosIndex = -1;
    int32 UVIndex = -1;
    int32 NormalIndex = -1;
};

FRawFaceVertex ParseSingleFaceVertex(std::string_view FaceToken)
{
    FRawFaceVertex Result;

    std::string_view PosStr = FStringParser::GetNextToken(FaceToken, '/');
    FStringParser::ParseInt(PosStr, Result.PosIndex);

    if (!FaceToken.empty())
    {
        std::string_view UVStr = FStringParser::GetNextToken(FaceToken, '/');
        if (!UVStr.empty())
        {
            FStringParser::ParseInt(UVStr, Result.UVIndex);
        }
    }

    if (!FaceToken.empty())
    {
        std::string_view NormalStr = FStringParser::GetNextToken(FaceToken, '/');
        FStringParser::ParseInt(NormalStr, Result.NormalIndex);
    }

    return Result;
}

bool FObjImporter::ParseObj(const FString &ObjFilePath, FObjInfo &OutObjInfo)
{
    OutObjInfo = FObjInfo();

    std::wstring DiskPath;
    FString      Error;
    if (!FPaths::TryResolvePackagePath(ObjFilePath, DiskPath, &Error))
    {
        UE_LOG("Invalid OBJ file path: %s", Error.c_str());
        return false;
    }

    std::ifstream File(std::filesystem::path(DiskPath), std::ios::binary | std::ios::ate);
    if (!File.is_open())
    {
        UE_LOG("Failed to open OBJ file: %s", ObjFilePath.c_str());
        return false;
    }

    size_t FileSize = static_cast<size_t>(File.tellg());
    File.seekg(0, std::ios::beg);
    TArray<char> Buffer(FileSize);
    if (!File.read(Buffer.data(), FileSize))
    {
        UE_LOG("Failed to read OBJ file: %s", ObjFilePath.c_str());
        return false;
    }

    std::string_view FileView(Buffer.data(), Buffer.size());

    if (FileView.size() >= 3 && FileView[0] == '\xEF' && FileView[1] == '\xBB' &&
        FileView[2] == '\xBF')
    {
        FileView.remove_prefix(3);
    }

    TArray<FRawFaceVertex> FaceVertices;
    FaceVertices.reserve(6);

    while (!FileView.empty())
    {
        std::string_view Line = FStringParser::GetNextToken(FileView, '\n');

        if (!Line.empty() && Line.back() == '\r')
        {
            Line.remove_suffix(1);
        }

        if (Line.empty() || Line[0] == '#')
        {
            continue;
        }

        std::string_view Prefix = FStringParser::GetNextToken(Line);

        if (Prefix == "v")
        {
            FVector Position;
            FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Position.X);
            FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Position.Y);
            FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Position.Z);
            OutObjInfo.Positions.emplace_back(Position);
        }
        else if (Prefix == "vt")
        {
            FVector2 UV;
            FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), UV.U);
            FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), UV.V);
            OutObjInfo.UVs.emplace_back(UV);
        }
        else if (Prefix == "vn")
        {
            FVector Normal;
            FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Normal.X);
            FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Normal.Y);
            FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Normal.Z);
            OutObjInfo.Normals.emplace_back(Normal);
        }
        else if (Prefix == "f")
        {

            if (OutObjInfo.Sections.empty())
            {
                FStaticMeshSection DefaultSection;
                DefaultSection.MaterialSlotName = "None";
                DefaultSection.FirstIndex = 0;
                DefaultSection.NumTriangles = 0;
                OutObjInfo.Sections.emplace_back(DefaultSection);
            }

            while (!Line.empty())
            {
                std::string_view FaceToken = FStringParser::GetNextToken(Line, ' ');
                if (!FaceToken.empty())
                {
                    FaceVertices.push_back(ParseSingleFaceVertex(FaceToken));
                }
            }

            if (FaceVertices.size() < 3)
            {
                UE_LOG("Face with less than 3 vertices");
                continue;
            }

            for (size_t i = 1; i + 1 < FaceVertices.size(); ++i)
            {
                const std::array<FRawFaceVertex, 3> TriangleVerts = {
                    FaceVertices[0], FaceVertices[i], FaceVertices[i + 1]};
                for (int j = 0; j < 3; ++j)
                {
                    constexpr int32 InvalidIndex = -1;
                    OutObjInfo.PosIndices.emplace_back(TriangleVerts[j].PosIndex - 1);
                    OutObjInfo.UVIndices.emplace_back(
                        TriangleVerts[j].UVIndex > 0 ? TriangleVerts[j].UVIndex - 1 : InvalidIndex);
                    OutObjInfo.NormalIndices.emplace_back(TriangleVerts[j].NormalIndex > 0
                                                              ? TriangleVerts[j].NormalIndex - 1
                                                              : InvalidIndex);
                }
            }
            FaceVertices.clear();
        }
        else
        {
            if (Prefix == "mtllib")
            {
                size_t CommentPos = Line.find('#');
                if (CommentPos != std::string_view::npos)
                {
                    Line = Line.substr(0, CommentPos);
                }
                FStringParser::TrimLeft(Line);
                OutObjInfo.MaterialLibraryFilePath =
                    FPaths::ResolveAssetPath(ObjFilePath, std::string(Line));
                UE_LOG("Found material library: %s", OutObjInfo.MaterialLibraryFilePath.c_str());
            }
            else if (Prefix == "usemtl")
            {
                size_t CommentPos = Line.find('#');
                if (CommentPos != std::string_view::npos)
                {
                    Line = Line.substr(0, CommentPos);
                }
                FStringParser::TrimLeft(Line);

                if (!OutObjInfo.Sections.empty())
                {
                    OutObjInfo.Sections.back().NumTriangles =
                        (static_cast<uint32>(OutObjInfo.PosIndices.size()) -
                         OutObjInfo.Sections.back().FirstIndex) /
                        3;
                }
                FStaticMeshSection Section;
                Section.MaterialSlotName = std::string(Line);
                if (Section.MaterialSlotName.empty())
                {
                    Section.MaterialSlotName = "None";
                }
                Section.FirstIndex = static_cast<uint32>(OutObjInfo.PosIndices.size());
                OutObjInfo.Sections.emplace_back(Section);
            }
            else if (Prefix == "o")
            {
                size_t CommentPos = Line.find('#');
                if (CommentPos != std::string_view::npos)
                {
                    Line = Line.substr(0, CommentPos);
                }
                FStringParser::TrimLeft(Line);

                OutObjInfo.ObjectName = std::string(Line);
            }
        }
    }

    if (!OutObjInfo.Sections.empty())
    {
        OutObjInfo.Sections.back().NumTriangles =
            (static_cast<uint32>(OutObjInfo.PosIndices.size()) -
             OutObjInfo.Sections.back().FirstIndex) /
            3;
    }

    if (OutObjInfo.UVs.empty())
    {
        OutObjInfo.UVs.emplace_back(FVector2{0.0f, 0.0f});
    }

    return true;
}

bool FObjImporter::ParseMtl(const FString &MtlFilePath, TArray<FObjMaterialInfo> &OutMtlInfos)
{
    OutMtlInfos.clear();
    std::wstring DiskPath;
    FString      Error;
    if (!FPaths::TryResolvePackagePath(MtlFilePath, DiskPath, &Error))
    {
        UE_LOG("Invalid MTL file path: %s", Error.c_str());
        return false;
    }

    std::ifstream File(std::filesystem::path(DiskPath), std::ios::binary | std::ios::ate);

    if (!File.is_open())
    {
        UE_LOG("Failed to open MTL file: %s", MtlFilePath.c_str());
        return false;
    }

    size_t FileSize = static_cast<size_t>(File.tellg());
    File.seekg(0, std::ios::beg);
    TArray<char> Buffer(FileSize);
    if (!File.read(Buffer.data(), FileSize))
    {
        UE_LOG("Failed to read MTL file: %s", MtlFilePath.c_str());
        return false;
    }

    std::string_view FileView(Buffer.data(), Buffer.size());

    if (FileView.size() >= 3 && FileView[0] == '\xEF' && FileView[1] == '\xBB' &&
        FileView[2] == '\xBF')
    {
        FileView.remove_prefix(3);
    }

    while (!FileView.empty())
    {
        std::string_view Line = FStringParser::GetNextToken(FileView, '\n');

        if (!Line.empty() && Line.back() == '\r')
        {
            Line.remove_suffix(1);
        }

        if (Line.empty() || Line[0] == '#')
        {
            continue;
        }

        std::string_view Prefix = FStringParser::GetNextWhitespaceToken(Line);

        if (Prefix == "newmtl")
        {
            FObjMaterialInfo MaterialInfo;
            FStringParser::TrimLeft(Line);
            MaterialInfo.MaterialSlotName = std::string(Line);
            MaterialInfo.Kd = FallbackColor3;
            OutMtlInfos.emplace_back(MaterialInfo);
        }
        else if (Prefix == "Kd")
        {
            if (OutMtlInfos.empty())
            {
                continue;
            }
            FObjMaterialInfo &CurrentMaterial = OutMtlInfos.back();
            FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line),
                                      CurrentMaterial.Kd.X);
            FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line),
                                      CurrentMaterial.Kd.Y);
            FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line),
                                      CurrentMaterial.Kd.Z);
        }
        else if (Prefix == "map_Kd")
        {
            if (OutMtlInfos.empty())
            {
                continue;
            }

            std::string TextureFileName;

            while (!Line.empty())
            {

                std::string_view LineBeforeToken = Line;
                std::string_view Token = FStringParser::GetNextWhitespaceToken(Line);

                if (Token.empty())
                    break;

                if (Token[0] == '-')
                {
                    int32 ArgsToSkip = 0;

                    if (Token == "-s" || Token == "-o" || Token == "-t")
                    {
                        ArgsToSkip = 3;
                    }

                    else if (Token == "-mm")
                    {
                        ArgsToSkip = 2;
                    }

                    else if (Token == "-bm" || Token == "-boost" || Token == "-texres" ||
                             Token == "-blendu" || Token == "-blendv" || Token == "-clamp" ||
                             Token == "-cc" || Token == "-imfchan")
                    {
                        ArgsToSkip = 1;
                    }

                    for (int32 i = 0; i < ArgsToSkip; ++i)
                    {
                        FStringParser::GetNextWhitespaceToken(Line);
                    }
                }
                else
                {

                    FStringParser::TrimLeft(LineBeforeToken);
                    TextureFileName = FString(LineBeforeToken);
                    break;
                }
            }

            size_t LastNonSpace = TextureFileName.find_last_not_of(" \t");
            if (LastNonSpace != FString::npos)
            {
                TextureFileName.erase(LastNonSpace + 1);
            }

            if (!TextureFileName.empty())
            {
                OutMtlInfos.back().map_Kd = FPaths::ResolveAssetPath(MtlFilePath, TextureFileName);
            }
        }
    }

    return true;
}

FString FObjImporter::ConvertMtlInfoToJson(const FObjMaterialInfo *MtlInfo)
{
    return ConvertMtlInfoToMat(MtlInfo);
}

FString FObjImporter::ConvertMtlInfoToMat(const FObjMaterialInfo *MtlInfo)
{
    FString      MatPath = "Asset/Materials/Auto/" + MtlInfo->MaterialSlotName + ".mat";
    std::wstring MatDiskPath;
    FString      Error;
    if (!FPaths::TryResolvePackagePath(MatPath, MatDiskPath, &Error))
    {
        return "";
    }

    if (std::filesystem::exists(std::filesystem::path(MatDiskPath)))
        return MatPath;

    std::filesystem::create_directories(std::filesystem::path(MatDiskPath).parent_path());

    json::JSON JsonData;
    JsonData["PathFileName"] = MatPath;
    JsonData["Origin"] = "ObjImport";
    JsonData["ShaderPath"] = "Shaders/Geometry/UberLit.hlsl";
    JsonData["RenderPass"] = "Opaque";

    if (!MtlInfo->map_Kd.empty())
    {
        JsonData["Textures"]["DiffuseTexture"] = MtlInfo->map_Kd;

        JsonData["Parameters"]["SectionColor"][0] = 1.0f;
        JsonData["Parameters"]["SectionColor"][1] = 1.0f;
        JsonData["Parameters"]["SectionColor"][2] = 1.0f;
        JsonData["Parameters"]["SectionColor"][3] = 1.0f;
    }
    else
    {

        JsonData["Parameters"]["SectionColor"][0] = MtlInfo->Kd.X;
        JsonData["Parameters"]["SectionColor"][1] = MtlInfo->Kd.Y;
        JsonData["Parameters"]["SectionColor"][2] = MtlInfo->Kd.Z;
        JsonData["Parameters"]["SectionColor"][3] = 1.0f;
    }

#if IS_GAME_CLIENT
    return MatPath;
#else
    std::ofstream File(std::filesystem::path(MatDiskPath), std::ios::binary);
    File << JsonData.dump();

    return MatPath;
#endif
}

FVector FObjImporter::RemapPosition(const FVector &ObjPos, EForwardAxis Axis)
{

    switch (Axis)
    {
    case EForwardAxis::X:
        return FVector(ObjPos.X, ObjPos.Z, ObjPos.Y);
    case EForwardAxis::NegX:
        return FVector(-ObjPos.X, -ObjPos.Z, ObjPos.Y);
    case EForwardAxis::Y:
        return FVector(ObjPos.Y, ObjPos.X, ObjPos.Z);
    case EForwardAxis::NegY:
        return FVector(-ObjPos.Y, -ObjPos.X, ObjPos.Z);
    case EForwardAxis::Z:
        return FVector(ObjPos.Z, ObjPos.X, ObjPos.Y);
    case EForwardAxis::NegZ:
        return FVector(-ObjPos.Z, ObjPos.X, ObjPos.Y);
    default:
        return FVector(ObjPos.X, ObjPos.Z, ObjPos.Y);
    }
}

bool FObjImporter::Convert(const FObjInfo &ObjInfo, const TArray<FObjMaterialInfo> &MtlInfos,
                           const FImportOptions &Options, FStaticMesh &OutMesh,
                           TArray<FStaticMaterial> &OutMaterials)
{
    OutMesh = FStaticMesh();
    OutMaterials.clear();

    TArray<FString> OrderedMaterialSlots;
    bool            bHasNoneSlot = false;

    for (const FStaticMeshSection &Section : ObjInfo.Sections)
    {
        const FString &CurrentSlotName = Section.MaterialSlotName;

        if (CurrentSlotName == "None")
        {
            bHasNoneSlot = true;
            continue;
        }

        if (std::find(OrderedMaterialSlots.begin(), OrderedMaterialSlots.end(), CurrentSlotName) ==
            OrderedMaterialSlots.end())
        {
            OrderedMaterialSlots.push_back(CurrentSlotName);
        }
    }

    for (const FString &TargetSlotName : OrderedMaterialSlots)
    {

        const FObjMaterialInfo *MatchedMaterial = nullptr;
        auto                    It = std::find_if(MtlInfos.begin(), MtlInfos.end(),
                                                  [&TargetSlotName](const FObjMaterialInfo &Mat)
                                                  { return Mat.MaterialSlotName == TargetSlotName; });

        if (It != MtlInfos.end())
        {
            MatchedMaterial = &(*It);

            UE_LOG("Importer TargetSlotName: %s;", TargetSlotName.c_str());

            FString MaterialPath = ConvertMtlInfoToMat(MatchedMaterial);

            UMaterial *MaterialObject = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);

            FStaticMaterial NewStaticMaterial;
            NewStaticMaterial.MaterialInterface = MaterialObject;
            NewStaticMaterial.MaterialSlotName = TargetSlotName;
            OutMaterials.push_back(NewStaticMaterial);
        }
        else
        {
            UMaterial *DefaultMaterial = FMaterialManager::Get().GetOrCreateMaterial("None");

            FStaticMaterial NewEmptyStaticMaterial;
            NewEmptyStaticMaterial.MaterialInterface = DefaultMaterial;
            NewEmptyStaticMaterial.MaterialSlotName = TargetSlotName;
            OutMaterials.push_back(NewEmptyStaticMaterial);
        }
    }

    if (bHasNoneSlot)
    {
        UMaterial *DefaultMaterial = FMaterialManager::Get().GetOrCreateMaterial("None");

        FStaticMaterial NewDefaultStaticMaterial;
        NewDefaultStaticMaterial.MaterialInterface = DefaultMaterial;
        NewDefaultStaticMaterial.MaterialSlotName = "None";

        OutMaterials.push_back(NewDefaultStaticMaterial);
    }

    TArray<TArray<uint32>> FacesPerMaterial;
    FacesPerMaterial.resize(OutMaterials.size());

    for (const FStaticMeshSection &RawSection : ObjInfo.Sections)
    {

        auto It = std::find_if(OutMaterials.begin(), OutMaterials.end(),
                               [&RawSection](const FStaticMaterial &Mat)
                               { return Mat.MaterialSlotName == RawSection.MaterialSlotName; });

        size_t MaterialIndex = 0;
        if (It != OutMaterials.end())
        {
            MaterialIndex = std::distance(OutMaterials.begin(), It);
        }
        else
        {

            MaterialIndex = OutMaterials.size() - 1;
            UE_LOG("Warning: Material slot '%s' not found. Assigning to Default slot.",
                   RawSection.MaterialSlotName.c_str());
        }

        for (uint32 i = 0; i < RawSection.NumTriangles; ++i)
        {
            uint32 FaceStartIndex = RawSection.FirstIndex + (i * 3);
            FacesPerMaterial[MaterialIndex].push_back(FaceStartIndex);
        }
    }

    TMap<FVertexKey, uint32> VertexMap;

    for (size_t MaterialIndex = 0; MaterialIndex < OutMaterials.size(); ++MaterialIndex)
    {
        const TArray<uint32> &FaceStarts = FacesPerMaterial[MaterialIndex];
        if (FaceStarts.empty())
            continue;

        FStaticMeshSection NewSection;
        NewSection.MaterialIndex = static_cast<int32>(MaterialIndex);
        NewSection.MaterialSlotName = OutMaterials[MaterialIndex].MaterialSlotName;
        NewSection.FirstIndex = static_cast<uint32>(OutMesh.Indices.size());
        NewSection.NumTriangles = static_cast<uint32>(FaceStarts.size());

        for (uint32 FaceStartIndex : FaceStarts)
        {
            uint32 TriangleIndices[3];

            FVector P0 = ObjInfo.Positions[ObjInfo.PosIndices[FaceStartIndex]];
            FVector P1 = ObjInfo.Positions[ObjInfo.PosIndices[FaceStartIndex + 1]];
            FVector P2 = ObjInfo.Positions[ObjInfo.PosIndices[FaceStartIndex + 2]];

            FVector Edge1 = P1 - P0;
            FVector Edge2 = P2 - P0;
            FVector FaceNormal = Edge1.Cross(Edge2).Normalized();

            for (int j = 0; j < 3; ++j)
            {
                size_t     CurrentIndex = FaceStartIndex + j;
                FVertexKey Key = {ObjInfo.PosIndices[CurrentIndex], ObjInfo.UVIndices[CurrentIndex],
                                  ObjInfo.NormalIndices[CurrentIndex]};

                if (auto It = VertexMap.find(Key); It != VertexMap.end())
                {

                    TriangleIndices[j] = It->second;
                }
                else
                {

                    FNormalVertex NewVertex;

                    NewVertex.pos = RemapPosition(ObjInfo.Positions[Key.p], Options.ForwardAxis) *
                                    Options.Scale;

                    if (Key.n == -1)
                    {
                        NewVertex.normal =
                            RemapPosition(FaceNormal, Options.ForwardAxis).Normalized();
                    }
                    else
                    {
                        NewVertex.normal =
                            RemapPosition(ObjInfo.Normals[Key.n], Options.ForwardAxis).Normalized();
                    }

                    if (Key.t == -1)
                    {
                        NewVertex.tex = {0.0f, 0.0f};
                    }
                    else
                    {
                        NewVertex.tex = ObjInfo.UVs[Key.t];

                        NewVertex.tex.V = 1.0f - NewVertex.tex.V;
                    }

                    NewVertex.color = {1.0f, 1.0f, 1.0f, 1.0f};

                    uint32 NewIndex = static_cast<uint32>(OutMesh.Vertices.size());
                    OutMesh.Vertices.push_back(NewVertex);

                    VertexMap[Key] = NewIndex;
                    TriangleIndices[j] = NewIndex;
                }
            }

            OutMesh.Indices.push_back(TriangleIndices[0]);
            if (Options.WindingOrder == EWindingOrder::CCW_to_CW)
            {
                OutMesh.Indices.push_back(TriangleIndices[2]);
                OutMesh.Indices.push_back(TriangleIndices[1]);
            }
            else
            {
                OutMesh.Indices.push_back(TriangleIndices[1]);
                OutMesh.Indices.push_back(TriangleIndices[2]);
            }
        }

        OutMesh.Sections.push_back(NewSection);
    }

    TArray<FVector> TangentSums(OutMesh.Vertices.size(), FVector(0.0f, 0.0f, 0.0f));
    TArray<FVector> BitangentSums(OutMesh.Vertices.size(), FVector(0.0f, 0.0f, 0.0f));

    for (size_t i = 0; i + 2 < OutMesh.Indices.size(); i += 3)
    {
        uint32 I0 = OutMesh.Indices[i + 0];
        uint32 I1 = OutMesh.Indices[i + 1];
        uint32 I2 = OutMesh.Indices[i + 2];

        const FNormalVertex &V0 = OutMesh.Vertices[I0];
        const FNormalVertex &V1 = OutMesh.Vertices[I1];
        const FNormalVertex &V2 = OutMesh.Vertices[I2];

        FVector Edge1 = V1.pos - V0.pos;
        FVector Edge2 = V2.pos - V0.pos;

        FVector2 DeltaUV1 = V1.tex - V0.tex;
        FVector2 DeltaUV2 = V2.tex - V0.tex;

        float Det = DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X;
        if (std::abs(Det) < 1e-8f)
        {
            continue;
        }

        float InvDet = 1.0f / Det;

        FVector Tangent = (Edge1 * DeltaUV2.Y - Edge2 * DeltaUV1.Y) * InvDet;
        FVector Bitangent = (Edge2 * DeltaUV1.X - Edge1 * DeltaUV2.X) * InvDet;

        TangentSums[I0] += Tangent;
        TangentSums[I1] += Tangent;
        TangentSums[I2] += Tangent;

        BitangentSums[I0] += Bitangent;
        BitangentSums[I1] += Bitangent;
        BitangentSums[I2] += Bitangent;
    }

    for (size_t i = 0; i < OutMesh.Vertices.size(); ++i)
    {
        FNormalVertex &V = OutMesh.Vertices[i];

        FVector N = V.normal.Normalized();
        FVector T = TangentSums[i];

        T = T - N * N.Dot(T);

        if (T.Length() < 1e-8f)
        {

            FVector Axis =
                std::abs(N.Z) < 0.999f ? FVector(0.0f, 0.0f, 1.0f) : FVector(0.0f, 1.0f, 0.0f);

            T = Axis.Cross(N).Normalized();
        }
        else
        {
            T.Normalize();
        }

        FVector B = BitangentSums[i];
        float   Handedness = N.Cross(T).Dot(B) < 0.0f ? -1.0f : 1.0f;

        V.tangent = FVector4(T, Handedness);
    }

    return true;
}

bool FObjImporter::Import(const FString &ObjFilePath, FStaticMesh &OutMesh,
                          TArray<FStaticMaterial> &OutMaterials)
{
    return Import(ObjFilePath, FImportOptions::Default(), OutMesh, OutMaterials);
}

bool FObjImporter::Import(const FString &ObjFilePath, const FImportOptions &Options,
                          FStaticMesh &OutMesh, TArray<FStaticMaterial> &OutMaterials)
{
    auto StartTime = std::chrono::high_resolution_clock::now();

    OutMaterials.clear();

    FObjInfo ObjInfo;
    if (!FObjImporter::ParseObj(ObjFilePath, ObjInfo))
    {
        UE_LOG("ParseObj failed for: %s", ObjFilePath.c_str());
        return false;
    }

    TArray<FObjMaterialInfo> ParsedMtlInfos;
    if (!ObjInfo.MaterialLibraryFilePath.empty())
    {
        if (!FObjImporter::ParseMtl(ObjInfo.MaterialLibraryFilePath, ParsedMtlInfos))
        {
            UE_LOG("ParseMtl failed for: %s", ObjInfo.MaterialLibraryFilePath.c_str());
            ObjInfo.MaterialLibraryFilePath.clear();
            ParsedMtlInfos.clear();
        }
    }

    if (!FObjImporter::Convert(ObjInfo, ParsedMtlInfos, Options, OutMesh, OutMaterials))
    {
        UE_LOG("Convert failed for: %s", ObjFilePath.c_str());
        return false;
    }
    OutMesh.PathFileName = ObjFilePath;

    auto                          EndTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> Duration = EndTime - StartTime;
    UE_LOG("OBJ Imported successfully. File: %s. Time taken: %.4f seconds", ObjFilePath.c_str(),
           Duration.count());

    return true;
}
