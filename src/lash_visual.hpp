#pragma once
#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <d2d1.h>
#include <wrl/client.h>

namespace whip {
using LashBitmap=Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget>;
inline void paintLashVector(ID2D1RenderTarget* target,ID2D1Factory* factory,const std::array<Vec2,Whip::segments+1>& points,WhipStyle style,Vec2 origin,float scale,float widthScale) {
    using Microsoft::WRL::ComPtr;const auto look=tuning(style);
    std::array<Vec2,Whip::segments+1> left{},right{};
    for(std::size_t i=0;i<points.size();++i) {
        const auto direction=points[std::min(i+1,Whip::segments)]-points[i?i-1:0];const double distance=std::max(length(direction),1e-6);
        const double q=static_cast<double>(i)/Whip::segments;
        const double gripFraction=Whip::handleLength/Whip::totalLength;
        const double transition=std::clamp((q-0.78*gripFraction)/(0.33*gripFraction),0.0,1.0),smooth=transition*transition*(3-2*transition);
        // The outer handle width is scaled from the selected cable silhouette; the collar-free
        // width transition is part of the same closed Bezier contour.
        const double gripExtra=(look.bodyWidth+2*look.outline)*(Whip::handleWidthRatio-1)*(1-smooth);
        const double half=(look.bodyWidth+gripExtra)*widthScale*0.5;
        const Vec2 center=(points[i]-origin)*scale,normal=Vec2{-direction.y/distance,direction.x/distance}*half;
        left[i]=center+normal;right[i]=center-normal;
    }
    const auto point=[](Vec2 v){return D2D1::Point2F(static_cast<float>(v.x),static_cast<float>(v.y));};
    ComPtr<ID2D1PathGeometry> path;ComPtr<ID2D1GeometrySink> sink;
    if(FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return;
    const auto edge=[&](const auto& edgePoints){
        for(std::size_t i=0;i<Whip::segments;++i) {
            const auto a=edgePoints[i?i-1:0],b=edgePoints[i],c=edgePoints[i+1],d=edgePoints[std::min(i+2,Whip::segments)];
            sink->AddBezier(D2D1::BezierSegment(point(b+(c-a)/6),point(c-(d-b)/6),point(c)));
        }
    };
    sink->BeginFigure(point(left.front()),D2D1_FIGURE_BEGIN_FILLED);edge(left);sink->AddLine(point(right.back()));
    std::reverse(right.begin(),right.end());edge(right);sink->EndFigure(D2D1_FIGURE_END_CLOSED);if(FAILED(sink->Close())) return;
    ComPtr<ID2D1SolidColorBrush> body,outline;ComPtr<ID2D1StrokeStyle> stroke;
    target->CreateSolidColorBrush(D2D1::ColorF(0x101114),&body);target->CreateSolidColorBrush(D2D1::ColorF(0xf8f8f8),&outline);
    auto properties=D2D1::StrokeStyleProperties();properties.lineJoin=D2D1_LINE_JOIN_ROUND;factory->CreateStrokeStyle(properties,nullptr,0,&stroke);
    if(body && outline) {target->DrawGeometry(path.Get(),outline.Get(),static_cast<float>(2*look.outline)*widthScale,stroke.Get());target->FillGeometry(path.Get(),body.Get());}
}
// Physics stays continuous. Only the selected C design is rasterized on a
// three-pixel grid, then enlarged with nearest-neighbor sampling.
inline void drawLash(ID2D1RenderTarget* target,ID2D1Factory* factory,const std::array<Vec2,Whip::segments+1>& points,WhipStyle style,Vec2 origin={},float scale=1,bool scaleWidth=true,LashBitmap* cache=nullptr) {
    const float widthScale=scaleWidth?scale:1;
    if(style!=WhipStyle::Spring) {paintLashVector(target,factory,points,style,origin,scale,widthScale);return;}
    constexpr float pixel=3;
    double minX=1e10,minY=minX,maxX=-minX,maxY=-minX;
    for(auto p:points) {p=(p-origin)*scale;minX=std::min(minX,p.x);minY=std::min(minY,p.y);maxX=std::max(maxX,p.x);maxY=std::max(maxY,p.y);}
    const float x=static_cast<float>(std::floor((minX-14*widthScale)/pixel)*pixel),y=static_cast<float>(std::floor((minY-14*widthScale)/pixel)*pixel);
    const auto width=static_cast<UINT32>(std::ceil((maxX+14*widthScale-x)/pixel)),height=static_cast<UINT32>(std::ceil((maxY+14*widthScale-y)/pixel));
    LashBitmap local;auto& raster=cache?*cache:local;
    if(raster) {const auto size=raster->GetPixelSize();if(size.width<width || size.height<height) raster.Reset();}
    if(!raster && FAILED(target->CreateCompatibleRenderTarget(D2D1::SizeF(static_cast<float>(width),static_cast<float>(height)),D2D1::SizeU(width,height),D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,&raster))) return;
    raster->BeginDraw();raster->Clear(D2D1::ColorF(0,0,0,0));raster->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    raster->SetTransform(D2D1::Matrix3x2F::Translation(-x,-y)*D2D1::Matrix3x2F::Scale(1/pixel,1/pixel));
    paintLashVector(raster.Get(),factory,points,style,origin,scale,widthScale);
    if(FAILED(raster->EndDraw())) {raster.Reset();return;}
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;raster->GetBitmap(&bitmap);
    const auto size=bitmap->GetPixelSize();target->DrawBitmap(bitmap.Get(),{x,y,x+size.width*pixel,y+size.height*pixel},1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}
}
