/**
 * FBX SDK 타입을 엔진 수학 타입과 메시 속성 값으로 변환하는 공통 함수를 구현한다.
 *
 * 좌표, 행렬, 노말, UV, 탄젠트, 머티리얼 인덱스처럼 여러 파서가 반복해서 필요로 하는 읽기 규칙을
 * 한곳에 모은다. FBX의 mapping/reference mode 차이와 누락된 데이터에 대한 fallback 처리를 여기서
 * 통일하여 StaticMesh와 SkeletalMesh 임포트 결과가 같은 기준을 따르게 한다.
 */

#include "FBXUtil.h"
#include <cmath>
#include <cstring>
#include <fbxsdk.h>

inline const FVector  DefaultNormal(0.0f, 0.0f, 1.0f);
inline const FVector2 DefaultUV(0.0f, 0.0f);
inline const FVector4 DefaultTangent(1.0f, 0.0f, 0.0f, 1.0f);

namespace
{
    bool IsValidString(const char *Value) { return Value && Value[0] != '\0'; }

    bool AreSameString(const char *A, const char *B)
    {
        return IsValidString(A) && IsValidString(B) && std::strcmp(A, B) == 0;
    }

    bool TryGetPolygonVertexUV(FbxMesh *Mesh, int32 PolyIndex, int32 CornerIndex,
                               const char *UVSetName, FVector2 &OutUV, FFbxUVReadStats *Stats)
    {
        if (!Mesh || !IsValidString(UVSetName))
        {
            return false;
        }

        FbxVector2 FbxUV;
        bool       bUnmapped = false;
        if (!Mesh->GetPolygonVertexUV(PolyIndex, CornerIndex, UVSetName, FbxUV, bUnmapped))
        {
            if (Stats)
            {
                ++Stats->GetPolygonVertexUVFailedCount;
            }
            return false;
        }

        if (bUnmapped)
        {
            if (Stats)
            {
                ++Stats->UnmappedCount;
            }
            return false;
        }

        OutUV = FBXUtil::ConvertFbxVector2(FbxUV);
        return true;
    }

    bool TryReadElementUV(FbxGeometryElementUV *UVElement, int32 PolyIndex, int32 ControlPointIndex,
                          int32 PolygonVertexCounter, FVector2 &OutUV)
    {
        if (!UVElement)
        {
            return false;
        }

        int32 MappingIndex = -1;
        switch (UVElement->GetMappingMode())
        {
        case FbxGeometryElement::eByControlPoint:
            MappingIndex = ControlPointIndex;
            break;
        case FbxGeometryElement::eByPolygonVertex:
            MappingIndex = PolygonVertexCounter;
            break;
        case FbxGeometryElement::eByPolygon:
            MappingIndex = PolyIndex;
            break;
        case FbxGeometryElement::eAllSame:
            MappingIndex = 0;
            break;
        default:
            return false;
        }

        if (MappingIndex < 0)
        {
            return false;
        }

        int32 DirectIndex = MappingIndex;
        if (UVElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect ||
            UVElement->GetReferenceMode() == FbxGeometryElement::eIndex)
        {
            if (MappingIndex >= UVElement->GetIndexArray().GetCount())
            {
                return false;
            }
            DirectIndex = UVElement->GetIndexArray().GetAt(MappingIndex);
        }
        else if (UVElement->GetReferenceMode() != FbxGeometryElement::eDirect)
        {
            return false;
        }

        if (DirectIndex < 0 || DirectIndex >= UVElement->GetDirectArray().GetCount())
        {
            return false;
        }

        OutUV = FBXUtil::ConvertFbxVector2(UVElement->GetDirectArray().GetAt(DirectIndex));
        return true;
    }
} // namespace

int32 FBXUtil::GetNodeDepth(FbxNode *Node)
{
    int32 Depth = 0;
    while (Node && Node->GetParent())
    {
        ++Depth;
        Node = Node->GetParent();
    }
    return Depth;
}

FVector FBXUtil::ConvertFbxVector(const FbxVector4 &V)
{
    return FVector(static_cast<float>(V[0]), static_cast<float>(V[1]), static_cast<float>(V[2]));
}

FVector2 FBXUtil::ConvertFbxVector2(const FbxVector2 &V)
{
    return FVector2(static_cast<float>(V[0]), static_cast<float>(V[1]));
}

FMatrix FBXUtil::ConvertFbxMatrix(const FbxAMatrix &M)
{
    FMatrix Result = FMatrix::Identity;

    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            Result.M[Row][Col] = static_cast<float>(M.Get(Row, Col));
        }
    }
    return Result;
}

int32 FBXUtil::ReadMaterialIndex(FbxMesh *Mesh, int32 PolyIndex)
{
    FbxLayerElementMaterial *MaterialElement = Mesh ? Mesh->GetElementMaterial() : nullptr;
    if (!MaterialElement)
    {
        return 0;
    }

    const auto MappingMode = MaterialElement->GetMappingMode();
    const auto ReferenceMode = MaterialElement->GetReferenceMode();

    if (MappingMode == FbxGeometryElement::eAllSame)
    {
        return MaterialElement->GetIndexArray().GetCount() > 0
                   ? MaterialElement->GetIndexArray().GetAt(0)
                   : 0;
    }

    if (MappingMode == FbxGeometryElement::eByPolygon)
    {
        if (ReferenceMode == FbxGeometryElement::eIndexToDirect ||
            ReferenceMode == FbxGeometryElement::eIndex)
        {
            return (PolyIndex >= 0 && PolyIndex < MaterialElement->GetIndexArray().GetCount())
                       ? MaterialElement->GetIndexArray().GetAt(PolyIndex)
                       : 0;
        }
        return PolyIndex;
    }

    return 0;
}

FVector FBXUtil::ReadPosition(FbxMesh *Mesh, int32 ControlPointIndex)
{
    FbxVector4 *ControlPoints = Mesh ? Mesh->GetControlPoints() : nullptr;
    return ControlPoints ? ConvertFbxVector(ControlPoints[ControlPointIndex])
                         : FVector(0.0f, 0.0f, 0.0f);
}

FVector FBXUtil::ReadNormal(FbxMesh *Mesh, int32 PolyIndex, int32 CornerIndex)
{
    FbxVector4 FbxNormal;
    if (!Mesh || !Mesh->GetPolygonVertexNormal(PolyIndex, CornerIndex, FbxNormal))
    {
        return DefaultNormal;
    }

    FVector Normal = ConvertFbxVector(FbxNormal);
    if (Normal.IsNearlyZero())
    {
        return DefaultNormal;
    }
    return Normal.Normalized();
}

FVector2 FBXUtil::ReadUV(FbxMesh *Mesh, int32 PolyIndex, int32 CornerIndex, int32 ControlPointIndex,
                         int32 PolygonVertexCounter, const char *PreferredUVSetName,
                         FFbxUVReadStats *Stats)
{
    if (!Mesh)
    {
        if (Stats)
        {
            ++Stats->DefaultUVCount;
        }
        return DefaultUV;
    }

    FVector2 UV;
    if (IsValidString(PreferredUVSetName) &&
        TryGetPolygonVertexUV(Mesh, PolyIndex, CornerIndex, PreferredUVSetName, UV, Stats))
    {
        if (Stats)
        {
            ++Stats->PreferredSuccessCount;
        }
        return UV;
    }

    FbxStringList UVSetNames;
    Mesh->GetUVSetNames(UVSetNames);
    for (int32 UVSetIndex = 0; UVSetIndex < UVSetNames.GetCount(); ++UVSetIndex)
    {
        const char *UVSetName = UVSetNames[UVSetIndex].Buffer();
        if (!IsValidString(UVSetName) || AreSameString(UVSetName, PreferredUVSetName))
        {
            continue;
        }

        if (TryGetPolygonVertexUV(Mesh, PolyIndex, CornerIndex, UVSetName, UV, Stats))
        {
            if (Stats)
            {
                ++Stats->UVSetFallbackSuccessCount;
            }
            return UV;
        }
    }

    const int32 UVElementCount = Mesh->GetElementUVCount();
    for (int32 UVElementIndex = 0; UVElementIndex < UVElementCount; ++UVElementIndex)
    {
        if (TryReadElementUV(Mesh->GetElementUV(UVElementIndex), PolyIndex, ControlPointIndex,
                             PolygonVertexCounter, UV))
        {
            if (Stats)
            {
                ++Stats->ManualElementFallbackSuccessCount;
            }
            return UV;
        }
    }

    if (Stats)
    {
        ++Stats->DefaultUVCount;
    }
    return DefaultUV;
}

FVector4 FBXUtil::ReadTangent(FbxMesh *Mesh, int32 ControlPointIndex, int32 PolygonVertexIndex)
{
    FbxLayer *Layer = Mesh ? Mesh->GetLayer(0) : nullptr;
    if (!Layer)
    {
        return DefaultTangent;
    }

    FbxLayerElementTangent *TangentElement = Layer->GetTangents();
    if (!TangentElement)
    {
        return DefaultTangent;
    }

    int32 ElementIndex = 0;
    switch (TangentElement->GetMappingMode())
    {
    case FbxGeometryElement::eByControlPoint:
        ElementIndex = ControlPointIndex;
        break;
    case FbxGeometryElement::eByPolygonVertex:
        ElementIndex = PolygonVertexIndex;
        break;
    default:
        return DefaultTangent;
    }

    int32 DirectIndex = ElementIndex;
    if (TangentElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect ||
        TangentElement->GetReferenceMode() == FbxGeometryElement::eIndex)
    {
        if (ElementIndex < 0 || ElementIndex >= TangentElement->GetIndexArray().GetCount())
        {
            return DefaultTangent;
        }
        DirectIndex = TangentElement->GetIndexArray().GetAt(ElementIndex);
    }

    if (DirectIndex < 0 || DirectIndex >= TangentElement->GetDirectArray().GetCount())
    {
        return DefaultTangent;
    }

    const FbxVector4 FbxTangent = TangentElement->GetDirectArray().GetAt(DirectIndex);
    FVector4         Result(static_cast<float>(FbxTangent[0]), static_cast<float>(FbxTangent[1]),
                            static_cast<float>(FbxTangent[2]), static_cast<float>(FbxTangent[3]));

    if (std::abs(Result.W) < 0.0001f)
    {
        Result.W = 1.0f;
    }

    return Result;
}

int32 FBXUtil::QuantizeFloat(float Value)
{
    constexpr float Scale = 100000.0f;
    return static_cast<int32>(std::round(Value * Scale));
}

FString FBXUtil::GetNodeName(FbxNode *Node)
{
    return Node && Node->GetName() ? Node->GetName() : "";
}
