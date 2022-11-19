#pragma once

#include <Urho3D/Graphics/BillboardSet.h>
#include <Urho3D/Math/Sphere.h>

namespace Urho3D
{
    
class URHO3D_API BillboardCloud : public BillboardSet
{
    URHO3D_OBJECT(BillboardCloud, BillboardSet);
public:
    BillboardCloud(Context*);
    virtual ~BillboardCloud();
    static void Register(Context*);
    
    void Update(const FrameInfo& frame) override;
    
    unsigned GetSeed() const { return seed_; }
    void SetSeed(unsigned seed) { seed_ = seed; Populate(); }
    
    IntVector2 GetCountRange() const { return countRange_; }
    
    PODVector<Sphere>& GetSpheres() { return spheres_; }
    const PODVector<Sphere>& GetSpheres() const { return spheres_; }
    
    VariantVector GetSpheresAttribute() const;
    void SetSpheresAttribute(const VariantVector&);

	struct BBSphere {
		Sphere sphere_;
		int count_;
		int seed_;
	};
    
private:
    void Populate();

    PODVector<Rect> uvSets_;
    PODVector<Sphere> spheres_;
    PODVector<float> swayValues_;
    unsigned seed_;
    IntVector2 countRange_;
    Vector2 swayRange_;
    Vector2 widthRange_;
    Vector2 heightRange_;
    float swayMax_;
};
    
}