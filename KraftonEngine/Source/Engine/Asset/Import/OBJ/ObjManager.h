/**
 * OBJ 원본과 바이너리 캐시를 관리하는 매니저를 선언한다.
 *
 * 원본 OBJ를 매번 텍스트 파싱하지 않기 위해 .bin 캐시를 만들고, StaticMesh UObject 생성과 GPU 리소스
 * 초기화까지 이어지는 로드 경로를 제공한다. 에디터의 메시 목록 스캔도 이 매니저가 담당한다.
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
 * OBJ 원본 파일과 바이너리 캐시, UStaticMesh 생성 과정을 관리한다.
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
    /**
     * 저장된 메시 에셋과 원본 메시 파일을 스캔해 목록을 갱신한다.
     */
    static void         ScanMeshAssets();
    static const TArray<FMeshAssetListItem> &GetAvailableMeshFiles();
    /**
     * 프로젝트 원본 폴더에서 OBJ 파일을 찾아 에디터 목록용 항목으로 수집한다.
     */
    static void                              ScanObjSourceFiles();
    static const TArray<FMeshAssetListItem> &GetAvailableObjFiles();

    /**
     * 캐시에 남아 있는 모든 텍스처의 GPU 리소스를 해제한다.
     */
    static void ReleaseAllGPU();

  private:
    static bool LoadStaticMeshAsset(const std::string &PathFileName, ID3D11Device *InDevice,
                                    FStaticMesh *&OutMesh, TArray<FStaticMaterial> &OutMaterials);
};
