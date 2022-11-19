# Urho3D XR Dump

Dump of code for XR in Urho3D (was base for porting to RBFX, dump will be eventually updated with results of porting back to RBFX again, and GL support). 

Put out as a dump as I'm too far diverged from 1.6 for it to ever become a PR to master. I've only tested on WMR (Odyssey+) and Oculus CV1.

**Warning:** this has only ever been Win64 + D3D11 tested.

##General gist:

- VR (folder, the more important stuff)
  - VRInterface.h/VRInterface.cpp - common VR/XR backend base, lots of support work is done here for junk like updating the VRRig
    - Designed to be used as a System, XR needs to span through scenes, so scene ownership of it isn't that sane (unless adding more abstractions/indirection to stuff)
  - VR.h/VR.cpp - SteamVR backend
    - Supports Single-Pass stereo via instancing doubling, or 1 pass each eye
    - MSAA works dandy
  - XR.h/XR.cpp - OpenXR backend
    - Assumes it's always a stereo-VR use case, if you want to do mobile AR it's gonna be best to
    - MR (hololens) stuff requires more texture format handling stuff and the like but should pretty much just work aside from that
    - Only supports Single-Pass stereo via instancing doubling
    - MSAA doesn't work r/n (sample-quality nonsense, need to add swapchains for depth - quick work with unknown bleed over consequences into graphics)
- Graphics folder
  - All sorts of crazy stuff happens ... see below for a summary there's lots of little spill over consequences

##General gist of what the user has to do:

- Attach a VR/XR system
- Create a node to hold the rig
- Call VRInterface::PrepareRig once to seup all the required nodes and stuff for the VR-Rig (you could do this yourself through the functions)
- Every update VRInterface::UpdateRig, VRInterface::UpdateHands to get all the correct information into the VR rig (again, you could do this directly yourself) for the head transform, eyes, and hands
  - These helper function just take care of probably desired behaviour of switching hands enabled/disabled for events like controller lost/acquired/etc and that the right transforms get passed along to everyone
  - You move by moving the Rig, the Rig is basically the centroid of the tracking volume
  - There may be a better way to do this? This seems to be what most are doing though? All ears on smarter or cleaner ways to handle this
- Remember to set yourself to 90fps max or w/e

##The general just of what happens in Graphics:

- View becomes aware of eyes
- Vertex buffer handling becomes capable of having strided instancing
  - I opted for transforming the instanced flag into a stride
  - This required changing lots of stuff all over the place, such as hashing and so to include that it's now a number of who-knows-what value instead of an on/off bool for instancing
- An added missing instanced draw-call (unindexed instanced) for stereo
- Batch collection is done once for each eye in stereo and then merged
  - Because ultra-wide FOV exists (see [RBFX branch](https://github.com/Jsandusky/rbfx/tree/XRMapToLatest) for combined frustum calc)
- Renderpath gets an extra flag to indicate it wants to duplex for VR
  - In which case drawing is performed as instanceCt * instanceMultiplier (only ever 1 or 2 so far)
    - Shaders then have to index as SV_InstanceID & 1 (shader happy version of SV_InstanceID % 2 without heavy modulo), so left-eye is 0 and right-eye is 1
  - All the above view related stuff happens
- Audio has to be reinitialized to respond to the change in the default device when XR/VR is started

If it's in here and I wrote it, then it's MIT license.

I would not assume anything is great, assume I'm brain-dead - especially with stuff going on the walker and rig handling.