#include "TFile.h"
#include <cmath>
float TFile::patchValue(int patchId,PatchField field) const {
    const auto &p=patches()[patchId];
    switch(field) {
    case PatchField::CenterX: return p.centerX;
    case PatchField::AverageY: return p.averageY;
    case PatchField::CenterZ: return p.centerZ;
    case PatchField::FactorY: return p.sphereRadius;
    case PatchField::RangeY: return p.rangeY;
    case PatchField::RadiusM: return p.radiusM;
    case PatchField::ShaderIndex: return p.shaderIndex;
    case PatchField::TextureX: return p.uv.x;
    case PatchField::TextureY: return p.uv.y;
    case PatchField::TextureW: return p.uv.w;
    case PatchField::TextureB: return p.uv.b;
    case PatchField::TextureC: return p.uv.c;
    case PatchField::TextureH: return p.uv.h;
    }
    return 0;
}
void TFile::setPatchValue(int patchId,PatchField field,float value) {
    auto &p=patches()[patchId];
    switch(field) {
    case PatchField::CenterX: p.centerX=value; break;
    case PatchField::AverageY: p.averageY=value; break;
    case PatchField::CenterZ: p.centerZ=value; break;
    case PatchField::FactorY: p.sphereRadius=value; break;
    case PatchField::RangeY: p.rangeY=value; break;
    case PatchField::RadiusM: p.radiusM=value; break;
    case PatchField::ShaderIndex:
        if(std::isfinite(value)&&value>=0&&value==std::floor(value)
            &&double(value)<materialCount())p.shaderIndex=quint32(value);
        break;
    case PatchField::TextureX: p.uv.x=value; break;
    case PatchField::TextureY: p.uv.y=value; break;
    case PatchField::TextureW: p.uv.w=value; break;
    case PatchField::TextureB: p.uv.b=value; break;
    case PatchField::TextureC: p.uv.c=value; break;
    case PatchField::TextureH: p.uv.h=value; break;
    }
}
