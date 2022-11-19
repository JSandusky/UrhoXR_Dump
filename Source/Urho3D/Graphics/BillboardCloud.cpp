#include "../Precompiled.h"
#include "BillboardCloud.h"

#include "../Core/Context.h"

namespace Urho3D
{

BillboardCloud::BillboardCloud(Context* ctx) : BillboardSet(ctx)
{

}

BillboardCloud::~BillboardCloud()
{

}

void BillboardCloud::Register(Context* ctx)
{

}

void BillboardCloud::Update(const FrameInfo& frame)
{
    int seed = seed_;
    for (unsigned i = 0; i < billboards_.Size(); ++i)
    {
        float rot = billboards_[i].rotation_;
        float sway = RandomSeeded(0.0f, 1.0f, seed);
        sway = Lerp(swayRange_.x_, swayRange_.y_, sway) * frame.timeStep_ * Sign(rot);

        billboards_[i].rotation_ = Oscillate(-swayMax_, swayMax_, rot + sway);
    }
}

VariantVector BillboardCloud::GetSpheresAttribute() const
{
	VariantVector ret;
	return ret;
}

void BillboardCloud::SetSpheresAttribute(const VariantVector& vec)
{
}

void BillboardCloud::Populate()
{	
    int seed = seed_;
	int chosenCt = RandomSeeded(Urho3D::Min(countRange_.x_, countRange_.y_), Urho3D::Max(countRange_.x_, countRange_.y_), seed);

    SetNumBillboards(chosenCt);
    swayValues_.Resize(chosenCt);

    auto& billboards = GetBillboards();

    unsigned i = 0;
    for (int i = 0; i < chosenCt; ++i)
	{
        const auto& s = spheres_[RandomSeeded(0, spheres_.Size() - 1, seed)];

        float rx = RandomSeeded(-1.0f, 1.0f, seed);
        float ry = RandomSeeded(-1.0f, 1.0f, seed);
        float rz = RandomSeeded(-1.0f, 1.0f, seed);

        Vector3 bbPos = s.center_ + Vector3(rx * s.radius_, ry * s.radius_, rz * s.radius_);
        billboards[i].position_ = bbPos;
        billboards[i].direction_ = Vector3::UP;
        billboards[i].color_ = Color::WHITE;
        billboards[i].uv_ = uvSets_[RandomSeeded(0, uvSets_.Size() - 1, seed)];
        billboards[i].rotation_ = RandomSeeded(-swayMax_, swayMax_, seed);
        billboards[i].enabled_ = true;
        billboards[i].size_ = Vector2(RandomSeeded(widthRange_.x_, widthRange_.y_, seed), RandomSeeded(heightRange_.x_, heightRange_.y_, seed));
	}
}

}