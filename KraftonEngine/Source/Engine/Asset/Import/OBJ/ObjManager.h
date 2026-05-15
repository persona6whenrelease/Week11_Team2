/**
 * OBJ 기반 StaticMesh 로딩과 캐시 관리를 담당하는 매니저를 선언한다.
 *
 * 이 클래스는 텍스트 OBJ 파일을 직접 로드하는 경로와 이미 만들어진 바이너리 캐시를 사용하는 경로를
 * 모두 제공한다. 외부에서는 원본 경로만 넘기면 되고, 내부에서는 캐시 파일명 생성, StaticMesh 객체
 * 생성, GPU 리소스 해제, 에셋 목록 갱신을 일관된 규칙으로 처리한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"
#include "Object/ObjectIterator.h"
#include "Render/Types/RenderTypes.h"
#include <map>
#include <string>
#include <memory>

struct FStaticMesh;
struct FImportOptions;
class UStaticMesh;

/**
 * OBJ 기반 StaticMesh 로딩과 캐시 생성을 관리한다.
 *
 * 텍스트 OBJ를 직접 파싱하는 비용을 줄이기 위해 바이너리 캐시 경로를 만들고, 필요할 때만 원본을
 * 다시 읽는다. 또한 에디터가 보여줄 원본/에셋 목록과 모든 OBJ 메시의 GPU 리소스 해제를 관리한다.
 */
class FObjManager
{

    static TMap<std::string, UStaticMesh *> StaticMeshCache;
    static TArray<FMeshAssetListItem>       AvailableMeshFiles;
    static TArray<FMeshAssetListItem>       AvailableObjFiles;

  public:
    static std::string  GetBinaryFilePath(const std::string &OriginalPath);
    static UStaticMesh *LoadObjStaticMesh(const std::string &PathFileName, ID3D11Device *InDevice);
    static UStaticMesh *LoadObjStaticMesh(const FString        &PathFileName,
                                          const FImportOptions &Options, ID3D11Device *InDevice);
    static void         ScanMeshAssets();
    static const TArray<FMeshAssetListItem> &GetAvailableMeshFiles();
    static void                              ScanObjSourceFiles();
    static const TArray<FMeshAssetListItem> &GetAvailableObjFiles();

    static void ReleaseAllGPU();

  private:
    static bool LoadStaticMeshAsset(const std::string &PathFileName, ID3D11Device *InDevice,
                                    FStaticMesh *&OutMesh, TArray<FStaticMaterial> &OutMaterials);
};
