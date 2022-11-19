#include <Urho3D/VR/XR.h>

#include "../Graphics/AnimatedModel.h"
#include "../Core/CoreEvents.h"
#include "../Engine/Engine.h"
#include "../IO/File.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/GraphicsEvents.h"
#include "../Graphics/IndexBuffer.h"
#include "../Resource/Localization.h"
#include "../IO/Log.h"
#include "../Graphics/Material.h"
#include "../IO/MemoryBuffer.h"
#include "../Scene/Node.h"
#include "../Resource/ResourceCache.h"
#include "../Scene/Scene.h"
#include "../Graphics/StaticModel.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/VertexBuffer.h"
#include "../VR/VREvents.h"
#include "../Resource/XMLElement.h"
#include "../Resource/XMLFile.h"

// need this for loading the GLBs
#define TINYGLTF_IMPLEMENTATION
#include <ThirdParty/TinyGLTFLoader/tiny_gltf.h>

#include <iostream>

#ifdef URHO3D_D3D11
    #include "../Graphics/Direct3D11/D3D11GraphicsImpl.h"
    #include <d3d11.h>
    #define XR_USE_GRAPHICS_API_D3D11
    #define XR_USE_PLATFORM_WIN32
#endif

#include <ThirdParty/OpenXRSDK/include/openxr/openxr_platform_defines.h>
#include <ThirdParty/OpenXRSDK/include/openxr/openxr_platform.h>

namespace Urho3D
{

SharedPtr<Node> LoadGLTFModel(Context* ctx, tinygltf::Model& model);

const XrPosef xrPoseIdentity = { {0,0,0,1}, {0,0,0} };

#pragma optimize("", off)

#define XR_INIT_TYPE(D, T) for (auto& a : D) a.type = T

#define XR_FOREACH(X)\
  X(xrDestroyInstance)\
  X(xrPollEvent)\
  X(xrResultToString)\
  X(xrGetSystem)\
  X(xrGetSystemProperties)\
  X(xrCreateSession)\
  X(xrDestroySession)\
  X(xrCreateReferenceSpace)\
  X(xrGetReferenceSpaceBoundsRect)\
  X(xrCreateActionSpace)\
  X(xrLocateSpace)\
  X(xrDestroySpace)\
  X(xrEnumerateViewConfigurations)\
  X(xrEnumerateViewConfigurationViews)\
  X(xrCreateSwapchain)\
  X(xrDestroySwapchain)\
  X(xrEnumerateSwapchainImages)\
  X(xrAcquireSwapchainImage)\
  X(xrWaitSwapchainImage)\
  X(xrReleaseSwapchainImage)\
  X(xrBeginSession)\
  X(xrEndSession)\
  X(xrWaitFrame)\
  X(xrBeginFrame)\
  X(xrEndFrame)\
  X(xrLocateViews)\
  X(xrStringToPath)\
  X(xrCreateActionSet)\
  X(xrDestroyActionSet)\
  X(xrCreateAction)\
  X(xrDestroyAction)\
  X(xrSuggestInteractionProfileBindings)\
  X(xrAttachSessionActionSets)\
  X(xrGetActionStateBoolean)\
  X(xrGetActionStateFloat)\
  X(xrGetActionStateVector2f)\
  X(xrSyncActions)\
  X(xrApplyHapticFeedback)\
  X(xrCreateHandTrackerEXT)\
  X(xrDestroyHandTrackerEXT)\
  X(xrLocateHandJointsEXT) \
  X(xrGetVisibilityMaskKHR) \
  X(xrCreateDebugUtilsMessengerEXT)

#ifdef URHO3D_D3D11
    #define XR_PLATFORM(X) \
        X(xrGetD3D11GraphicsRequirementsKHR)
#endif

#define XR_EXTENSION_BASED(X) \
    X(xrLoadControllerModelMSFT) \
    X(xrGetControllerModelKeyMSFT) \
    X(xrGetControllerModelStateMSFT) \
    X(xrGetControllerModelPropertiesMSFT)

#define XR_DECLARE(fn) static PFN_##fn fn;
#define XR_LOAD(fn) xrGetInstanceProcAddr(instance_, #fn, (PFN_xrVoidFunction*) &fn);

    XR_FOREACH(XR_DECLARE)
    XR_PLATFORM(XR_DECLARE)
    XR_EXTENSION_BASED(XR_DECLARE)

#define XR_ERRNAME(ENUM) { ENUM, #ENUM },

        static Urho3D::HashMap<int, const char*> xrErrorNames = {
            XR_ERRNAME(XR_SUCCESS)
            XR_ERRNAME(XR_TIMEOUT_EXPIRED)
            XR_ERRNAME(XR_SESSION_LOSS_PENDING)
            XR_ERRNAME(XR_EVENT_UNAVAILABLE)
            XR_ERRNAME(XR_SPACE_BOUNDS_UNAVAILABLE)
            XR_ERRNAME(XR_SESSION_NOT_FOCUSED)
            XR_ERRNAME(XR_FRAME_DISCARDED)
            XR_ERRNAME(XR_ERROR_VALIDATION_FAILURE)
            XR_ERRNAME(XR_ERROR_RUNTIME_FAILURE)
            XR_ERRNAME(XR_ERROR_OUT_OF_MEMORY)
            XR_ERRNAME(XR_ERROR_API_VERSION_UNSUPPORTED)
            XR_ERRNAME(XR_ERROR_INITIALIZATION_FAILED)
            XR_ERRNAME(XR_ERROR_FUNCTION_UNSUPPORTED)
            XR_ERRNAME(XR_ERROR_FEATURE_UNSUPPORTED)
            XR_ERRNAME(XR_ERROR_EXTENSION_NOT_PRESENT)
            XR_ERRNAME(XR_ERROR_LIMIT_REACHED)
            XR_ERRNAME(XR_ERROR_SIZE_INSUFFICIENT)
            XR_ERRNAME(XR_ERROR_HANDLE_INVALID)
            XR_ERRNAME(XR_ERROR_INSTANCE_LOST)
            XR_ERRNAME(XR_ERROR_SESSION_RUNNING)
            XR_ERRNAME(XR_ERROR_SESSION_NOT_RUNNING)
            XR_ERRNAME(XR_ERROR_SESSION_LOST)
            XR_ERRNAME(XR_ERROR_SYSTEM_INVALID)
            XR_ERRNAME(XR_ERROR_PATH_INVALID)
            XR_ERRNAME(XR_ERROR_PATH_COUNT_EXCEEDED)
            XR_ERRNAME(XR_ERROR_PATH_FORMAT_INVALID)
            XR_ERRNAME(XR_ERROR_PATH_UNSUPPORTED)
            XR_ERRNAME(XR_ERROR_LAYER_INVALID)
            XR_ERRNAME(XR_ERROR_LAYER_LIMIT_EXCEEDED)
            XR_ERRNAME(XR_ERROR_SWAPCHAIN_RECT_INVALID)
            XR_ERRNAME(XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED)
            XR_ERRNAME(XR_ERROR_ACTION_TYPE_MISMATCH)
            XR_ERRNAME(XR_ERROR_SESSION_NOT_READY)
            XR_ERRNAME(XR_ERROR_SESSION_NOT_STOPPING)
            XR_ERRNAME(XR_ERROR_TIME_INVALID)
            XR_ERRNAME(XR_ERROR_REFERENCE_SPACE_UNSUPPORTED)
            XR_ERRNAME(XR_ERROR_FILE_ACCESS_ERROR)
            XR_ERRNAME(XR_ERROR_FILE_CONTENTS_INVALID)
            XR_ERRNAME(XR_ERROR_FORM_FACTOR_UNSUPPORTED)
            XR_ERRNAME(XR_ERROR_FORM_FACTOR_UNAVAILABLE)
            XR_ERRNAME(XR_ERROR_API_LAYER_NOT_PRESENT)
            XR_ERRNAME(XR_ERROR_CALL_ORDER_INVALID)
            XR_ERRNAME(XR_ERROR_GRAPHICS_DEVICE_INVALID)
            XR_ERRNAME(XR_ERROR_POSE_INVALID)
            XR_ERRNAME(XR_ERROR_INDEX_OUT_OF_RANGE)
            XR_ERRNAME(XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED)
            XR_ERRNAME(XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED)
            XR_ERRNAME(XR_ERROR_NAME_DUPLICATED)
            XR_ERRNAME(XR_ERROR_NAME_INVALID)
            XR_ERRNAME(XR_ERROR_ACTIONSET_NOT_ATTACHED)
            XR_ERRNAME(XR_ERROR_ACTIONSETS_ALREADY_ATTACHED)
            XR_ERRNAME(XR_ERROR_LOCALIZED_NAME_DUPLICATED)
            XR_ERRNAME(XR_ERROR_LOCALIZED_NAME_INVALID)
            XR_ERRNAME(XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING)
            XR_ERRNAME(XR_ERROR_RUNTIME_UNAVAILABLE)
            XR_ERRNAME(XR_ERROR_ANDROID_THREAD_SETTINGS_ID_INVALID_KHR)
            XR_ERRNAME(XR_ERROR_ANDROID_THREAD_SETTINGS_FAILURE_KHR)
            XR_ERRNAME(XR_ERROR_CREATE_SPATIAL_ANCHOR_FAILED_MSFT)
            XR_ERRNAME(XR_ERROR_SECONDARY_VIEW_CONFIGURATION_TYPE_NOT_ENABLED_MSFT)
            XR_ERRNAME(XR_ERROR_CONTROLLER_MODEL_KEY_INVALID_MSFT)
            XR_ERRNAME(XR_ERROR_REPROJECTION_MODE_UNSUPPORTED_MSFT)
            XR_ERRNAME(XR_ERROR_COMPUTE_NEW_SCENE_NOT_COMPLETED_MSFT)
            XR_ERRNAME(XR_ERROR_SCENE_COMPONENT_ID_INVALID_MSFT)
            XR_ERRNAME(XR_ERROR_SCENE_COMPONENT_TYPE_MISMATCH_MSFT)
            XR_ERRNAME(XR_ERROR_SCENE_MESH_BUFFER_ID_INVALID_MSFT)
            XR_ERRNAME(XR_ERROR_SCENE_COMPUTE_FEATURE_INCOMPATIBLE_MSFT)
            XR_ERRNAME(XR_ERROR_SCENE_COMPUTE_CONSISTENCY_MISMATCH_MSFT)
            XR_ERRNAME(XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB)
            XR_ERRNAME(XR_ERROR_COLOR_SPACE_UNSUPPORTED_FB)
            XR_ERRNAME(XR_ERROR_SPATIAL_ANCHOR_NAME_NOT_FOUND_MSFT)
            XR_ERRNAME(XR_ERROR_SPATIAL_ANCHOR_NAME_INVALID_MSFT)
    };

    const char* xrGetErrorStr(XrResult r)
    {
        auto found = xrErrorNames.Find(r);
        if (found != xrErrorNames.End())
            return found->second_;
        return "Unknown XR Error";
    }

    Vector3 uxrGetVec(XrVector3f v)
    {
        return Vector3(v.x, v.y, -v.z);
    }

    Urho3D::Quaternion uxrGetQuat(XrQuaternionf q)
    {
        Quaternion out;
        out.x_ = -q.x;
        out.y_ = -q.y;
        out.z_ = q.z;
        out.w_ = q.w;
        return out;
    }

    Urho3D::Matrix3x4 uxrGetTransform(XrPosef pose)
    {
        return Matrix3x4(uxrGetVec(pose.position), uxrGetQuat(pose.orientation), 1.0f);
    }

    Urho3D::Matrix4 uxrGetProjection(float nearZ, float farZ, float angleLeft, float angleTop, float angleRight, float angleBottom)
    {
        const float tanLeft = tanf(angleLeft);
        const float tanRight = tanf(angleRight);
        const float tanDown = tanf(angleBottom);
        const float tanUp = tanf(angleTop);
        const float tanAngleWidth = tanRight - tanLeft;
        const float tanAngleHeight = tanUp - tanDown;
        const float q = farZ / (farZ - nearZ);
        const float r = -q * nearZ;

        Matrix4 projection = Matrix4::ZERO;
        projection.m00_ = 2 / tanAngleWidth;
        projection.m11_ = 2 / tanAngleHeight;

        projection.m02_ = -(tanRight + tanLeft) / tanAngleWidth;
        projection.m12_ = -(tanUp + tanDown) / tanAngleHeight;

        projection.m22_ = q;
        projection.m23_ = r;
        projection.m32_ = 1.0f;
        return projection;
    }

    

    struct OpenXR::Opaque
    {
#ifdef URHO3D_D3D11
        XrSwapchainImageD3D11KHR swapImages_[4] = { {}, {} };
#endif
#ifdef URHO3D_OPENGL
        XrSwapchainImageOpenGLKHR swapImages_[2][4] = { {}, {} };
#endif
    };

    OpenXR::OpenXR(Context* ctx) : 
        BaseClassName(ctx),
        instance_(0),
        session_(0),
        swapChain_{},
        sessionLive_(false)
    {
        useSingleTexture_ = true;

        opaque_.Reset(new Opaque());

        SubscribeToEvent(E_BEGINFRAME, URHO3D_HANDLER(OpenXR, HandlePreUpdate));
        //SubscribeToEvent(E_BEGINRENDERING, URHO3D_HANDLER(OpenXR, HandlePreRender));
        SubscribeToEvent(E_POSTPRESENT, URHO3D_HANDLER(OpenXR, HandlePostRender));
    }

    OpenXR::~OpenXR()
    {
        Shutdown();
    }

    void OpenXR::QueryExtensions()
    {
        extensions_.Clear();

        unsigned extCt = 0;
        xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCt, nullptr);
        PODVector<XrExtensionProperties> extensions;
        extensions.Resize(extCt);
        for (auto& x : extensions)
            x = { XR_TYPE_EXTENSION_PROPERTIES };
        xrEnumerateInstanceExtensionProperties(nullptr, extensions.Size(), &extCt, extensions.Buffer());

        for (auto e : extensions)
            extensions_.Push(e.extensionName);
    }

    bool OpenXR::Initialize(const String& manifestPath)
    {
        auto graphics = GetSubsystem<Graphics>();

        manifest_ = new XMLFile(GetContext());
        if (!manifest_->LoadFile(manifestPath))
            manifest_.Reset();

        QueryExtensions();

        static auto SupportsExt = [](const StringVector& extensions, const char* name) {
            for (auto ext : extensions)
            {
                if (ext.Compare(name, false) == 0)
                    return true;
            }
            return false;
        };

        PODVector<const char*> activeExt;
#ifdef URHO3D_D3D11
        activeExt.Push(XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
#elif defined(URHO3D_OPENGL)
#ifdef GL_ES_VERSION_2_0
        activeExt.Push(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
#else   
        activeExt.Push(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
#endif  
#endif

        bool supportsDebug = false;
        if (SupportsExt(extensions_, XR_EXT_DEBUG_UTILS_EXTENSION_NAME))
        {
            activeExt.Push(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
            supportsDebug = true;
        }
        if (SupportsExt(extensions_, XR_KHR_VISIBILITY_MASK_EXTENSION_NAME))
        {
            activeExt.Push(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
            supportsMask_ = true;
        }
        if (SupportsExt(extensions_, XR_MSFT_CONTROLLER_MODEL_EXTENSION_NAME))
        {
            activeExt.Push(XR_MSFT_CONTROLLER_MODEL_EXTENSION_NAME);
            supportsControllerModel_ = true;
        }

        for (auto e : extraExtensions_)
            activeExt.Push(e.CString());

        XrInstanceCreateInfo info = { XR_TYPE_INSTANCE_CREATE_INFO };
        strcpy_s(info.applicationInfo.engineName, 128, "Urho3D");
        strcpy_s(info.applicationInfo.applicationName, 128, "Urho3D");
        info.applicationInfo.engineVersion = (1 << 24) + (0 << 16) + 0;
        info.applicationInfo.applicationVersion = 0;
        info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
        info.enabledExtensionCount = activeExt.Size();
        info.enabledExtensionNames = activeExt.Buffer();

        instance_ = 0x0;
        auto errCode = xrCreateInstance(&info, &instance_);
        if (errCode != XrResult::XR_SUCCESS)
        {
            URHO3D_LOGERRORF("Unable to create OpenXR instance: %s", xrGetErrorStr(errCode));
            return false;
        }

        XR_FOREACH(XR_LOAD);
        XR_PLATFORM(XR_LOAD);
        XR_EXTENSION_BASED(XR_LOAD);

        XrInstanceProperties instProps = { XR_TYPE_INSTANCE_PROPERTIES };
        xrGetInstanceProperties(instance_, &instProps);

        if (supportsDebug)
        {
            XrDebugUtilsMessengerCreateInfoEXT debugUtils = { XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            debugUtils.messageTypes =
                XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
            debugUtils.messageSeverities =
                XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

            debugUtils.userData = GetSubsystem<Log>();
            debugUtils.userCallback = [](XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT types, const XrDebugUtilsMessengerCallbackDataEXT *msg, void* user_data) {
                auto log = ((Log*)user_data);
                log->Write(3, Urho3D::ToString("XR Error: %s, %s", msg->functionName, msg->message));
                std::cout << msg->functionName << " : " << msg->message << std::endl;
                return (XrBool32)XR_FALSE;
            };

            XrDebugUtilsMessengerEXT msg;
            xrCreateDebugUtilsMessengerEXT(instance_, &debugUtils, &msg);
        }

        XrSystemGetInfo sysInfo = { XR_TYPE_SYSTEM_GET_INFO };
        sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

#define XR_COMMON_ER(TERM) if (errCode != XrResult::XR_SUCCESS) { \
    URHO3D_LOGERRORF("Unable to produce OpenXR " TERM " ID: %s", xrGetErrorStr(errCode)); \
    Shutdown(); \
    return false; \
}

        errCode = xrGetSystem(instance_, &sysInfo, &system_);
        XR_COMMON_ER("system ID");

        uint32_t blendCount = 0;
        errCode = xrEnumerateEnvironmentBlendModes(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 1, &blendCount, &blendMode_);
        XR_COMMON_ER("blending mode");

        XrSystemHandTrackingPropertiesEXT handTracking = { XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT };
        handTracking.supportsHandTracking = false;

        XrSystemProperties sysProps = { XR_TYPE_SYSTEM_PROPERTIES };
        errCode = xrGetSystemProperties(instance_, system_, &sysProps);
        XR_COMMON_ER("system properties");
        systemName_ = sysProps.systemName;

        unsigned viewConfigCt = 0;
        XrViewConfigurationType viewConfigurations[4];
        errCode = xrEnumerateViewConfigurations(instance_, system_, 4, &viewConfigCt, viewConfigurations);
        XR_COMMON_ER("view config");
        bool stereo = false;
        for (auto v : viewConfigurations)
        {
            if (v == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
                stereo = true;
        }

        if (!stereo)
        {
            URHO3D_LOGERROR("Stereo rendering not supported on this device");
            Shutdown();
            return false;
        }

        unsigned viewCt = 0;
        XrViewConfigurationView views[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW }, { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
        errCode = xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCt, views);
        XR_COMMON_ER("view config views")

        trueEyeTexWidth_ = eyeTexWidth_ = (int)Min(views[VR_EYE_LEFT].recommendedImageRectWidth, views[VR_EYE_RIGHT].recommendedImageRectWidth);
        trueEyeTexHeight_ = eyeTexHeight_ = (int)Min(views[VR_EYE_LEFT].recommendedImageRectHeight, views[VR_EYE_RIGHT].recommendedImageRectHeight);

        eyeTexWidth_ = eyeTexWidth_ * renderTargetScale_;
        eyeTexHeight_ = eyeTexHeight_ * renderTargetScale_;

        if (!OpenSession())
        {
            Shutdown();
            return false;
        }

        if (!CreateSwapchain())
        {
            Shutdown();
            return false;
        }

        GetHiddenAreaMask();

        return true;
    }

    void OpenXR::Shutdown()
    {
        // already shutdown?
        if (instance_ == 0)
            return;

        for (int i = 0; i < 2; ++i)
        {
            wandModels_[i] = ControllerModel();
            handGrips_[i].Reset();
            handAims_[i].Reset();
            handHaptics_[i].Reset();
            views_[i] = { XR_TYPE_VIEW };
        }
        manifest_.Reset();
        actionSets_.Clear();
        sessionLive_ = false;

        DestroySwapchain();

#define DTOR_XR(N, S) if (S) xrDestroy ## N(S)

        DTOR_XR(Space, headSpace_);
        DTOR_XR(Space, viewSpace_);

#undef DTOR_SPACE

        CloseSession();
        session_ = { };

        if (instance_)
            xrDestroyInstance(instance_);

        instance_ = { };
        system_ = { };
        blendMode_ = { };
        headSpace_ = { };
        viewSpace_ = { };
    }

    bool OpenXR::OpenSession()
    {
        auto graphics = GetSubsystem<Graphics>();

        XrActionSetCreateInfo actionCreateInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
        strcpy_s(actionCreateInfo.actionSetName, 64, "default");
        strcpy_s(actionCreateInfo.localizedActionSetName, 128, "default");
        actionCreateInfo.priority = 0;


        XrSessionCreateInfo sessionCreateInfo = { XR_TYPE_SESSION_CREATE_INFO };
        sessionCreateInfo.systemId = system_;

#if URHO3D_D3D11
        XrGraphicsBindingD3D11KHR binding = { XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
        binding.device = graphics->GetImpl()->GetDevice();        
        sessionCreateInfo.next = &binding;

        XrGraphicsRequirementsD3D11KHR requisite = { XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
        auto errCode = xrGetD3D11GraphicsRequirementsKHR(instance_, system_, &requisite);
        XR_COMMON_ER("graphics requirements");

        
#endif
#if URHO3D_OPENGL
    #ifdef WIN32
            XrGraphicsBindingOpenGLWin32KHR binding = { };
            sessionCreateInfo.next = &binding;
    #endif
#endif        

        errCode = xrCreateSession(instance_, &sessionCreateInfo, &session_);
        XR_COMMON_ER("session");

        // attempt stage-space first
        XrReferenceSpaceCreateInfo refSpaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        refSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        refSpaceInfo.poseInReferenceSpace.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
        refSpaceInfo.poseInReferenceSpace.position = { 0.0f, 0.0f, 0.0f };

        errCode = xrCreateReferenceSpace(session_, &refSpaceInfo, &headSpace_);
        // failed? then do local space
        if (errCode != XR_SUCCESS)
        {
            refSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            errCode = xrCreateReferenceSpace(session_, &refSpaceInfo, &headSpace_);
            XR_COMMON_ER("reference space");

            isRoomScale_ = false;
        }
        else
            isRoomScale_ = true;

        XrReferenceSpaceCreateInfo viewSpaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        viewSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        viewSpaceInfo.poseInReferenceSpace.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
        viewSpaceInfo.poseInReferenceSpace.position = { 0.0f, 0.0f, 0.0f };
        errCode = xrCreateReferenceSpace(session_, &viewSpaceInfo, &viewSpace_);
        XR_COMMON_ER("view reference space");        

        if (manifest_)
            BindActions(manifest_);

        // if there's a default action set, then use it.
        VRInterface::SetCurrentActionSet(String("default"));

        return true;
    }

    void OpenXR::CloseSession()
    {
        if (session_)
            xrDestroySession(session_);
        session_ = 0;
    }

    bool OpenXR::CreateSwapchain()
    {
        auto graphics = GetSubsystem<Graphics>();

        for (int j = 0; j < 4; ++j)
        {
#ifdef URHO3D_D3D11
            opaque_->swapImages_[j].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            opaque_->swapImages_[j].next = nullptr;
#endif
#ifdef URHO3D_OPENGL
            swapImages_[j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
            opaque_->swapImages_[j].next = nullptr;
#endif
        }

        
        XrSwapchainCreateInfo swapInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        swapInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        swapInfo.format = graphics->GetRGBAFormat();
        swapInfo.width = eyeTexWidth_ * 2;
        swapInfo.height = eyeTexHeight_;
        swapInfo.sampleCount = msaaLevel_;
        swapInfo.faceCount = 1;
        swapInfo.arraySize = 1;
        swapInfo.mipCount = 1;

        unsigned fmtCt = 0;
        xrEnumerateSwapchainFormats(session_, 0, &fmtCt, 0);
        PODVector<int64_t> fmts;
        fmts.Resize(fmtCt);
        xrEnumerateSwapchainFormats(session_, fmtCt, &fmtCt, fmts.Buffer());

        auto errCode = xrCreateSwapchain(session_, &swapInfo, &swapChain_);
        XR_COMMON_ER("swapchain")

        errCode = xrEnumerateSwapchainImages(swapChain_, 4, &imgCount_, (XrSwapchainImageBaseHeader*)opaque_->swapImages_);
        XR_COMMON_ER("swapchain images")

#ifdef URHO3D_D3D11
        // bump the d3d-ref count
        for (int i = 0; i < imgCount_; ++i)
            opaque_->swapImages_[i].texture->AddRef();
#endif

        CreateEyeTextures();

        return true;
    }

    void OpenXR::DestroySwapchain()
    {
        if (swapChain_)
            xrDestroySwapchain(swapChain_);
        swapChain_ = { };

        for (int j = 0; j < 4; ++j)
            if (opaque_->swapImages_[j].texture)
                opaque_->swapImages_[j].texture->Release();
    }

    void OpenXR::CreateEyeTextures()
    {
        // if we've got a swapchain it needs to be resized
        if (swapChain_)
        {
            DestroySwapchain();
            CreateSwapchain();
        }

        sharedTexture_ = new Texture2D(GetContext());
        leftTexture_.Reset();
        rightTexture_.Reset();

        sharedDS_.Reset();
        leftDS_.Reset();
        rightDS_.Reset();

        sharedDS_ = new Texture2D(GetContext());
        sharedDS_->SetNumLevels(1);
        sharedDS_->SetSize(eyeTexWidth_ * 2, eyeTexHeight_, Graphics::GetDepthStencilFormat(), TEXTURE_DEPTHSTENCIL, msaaLevel_);

        for (unsigned i = 0; i < imgCount_; ++i)
        {
            eyeColorTextures_[i] = new Texture2D(GetContext());
            eyeColorTextures_[i]->CreateFromExternal(opaque_->swapImages_[i].texture, msaaLevel_);
            eyeColorTextures_[i]->GetRenderSurface()->SetLinkedDepthStencil(sharedDS_->GetRenderSurface());
        }
    }

    void OpenXR::HandlePreUpdate(StringHash, VariantMap& data)
    {
        if (instance_ == 0 || session_ == 0)
            return;

        XrEventDataBuffer eventBuffer = { XR_TYPE_EVENT_DATA_BUFFER };
        while (xrPollEvent(instance_, &eventBuffer) == XR_SUCCESS)
        {
            switch (eventBuffer.type)
            {
            case XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR:
                GetHiddenAreaMask();
                break;
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                sessionLive_ = false;
                SendEvent(E_VREXIT); //?? does something need to be communicated beyond this?
                break;
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                UpdateBindingBound();
                SendEvent(E_VRINTERACTIONPROFILECHANGED);
                break;
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                XrEventDataSessionStateChanged* changed = (XrEventDataSessionStateChanged*)&eventBuffer;
                auto state = changed->state;
                switch (state)
                {
                case XR_SESSION_STATE_READY: {
                    XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
                    beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    auto res = xrBeginSession(session_, &beginInfo);
                    if (res != XR_SUCCESS)
                    {
                        URHO3D_LOGERRORF("Failed to begin XR session: %s", xrGetErrorStr(res));
                        sessionLive_ = false;
                    }
                    else
                        sessionLive_ = true; // uhhh what
                } break;
                case XR_SESSION_STATE_IDLE:
                    SendEvent(E_VRPAUSE);
                    sessionLive_ = false;
                    break;
                case XR_SESSION_STATE_FOCUSED: // we're hooked up
                    sessionLive_ = true;
                    SendEvent(E_VRRESUME);
                    break;
                case XR_SESSION_STATE_STOPPING:
                    xrEndSession(session_);
                    sessionLive_ = false;
                    break;
                case XR_SESSION_STATE_EXITING:
                case XR_SESSION_STATE_LOSS_PENDING:
                    sessionLive_ = false;
                    SendEvent(E_VREXIT);
                    break;
                }

            }

            eventBuffer = { XR_TYPE_EVENT_DATA_BUFFER };
        }

        if (!IsLive())
            return;

        XrFrameState frameState = { XR_TYPE_FRAME_STATE };
        xrWaitFrame(session_, nullptr, &frameState);
        predictedTime_ = frameState.predictedDisplayTime;

        XrFrameBeginInfo begInfo = { XR_TYPE_FRAME_BEGIN_INFO };
        xrBeginFrame(session_, &begInfo);
    // head stuff
        headLoc_.next = &headVel_;
        xrLocateSpace(viewSpace_, headSpace_, frameState.predictedDisplayTime, &headLoc_);

        HandlePreRender(StringHash(), VariantMap());

        for (int i = 0; i < 2; ++i)
        {
            if (handAims_[i])
            {
                // ensure velocity is linked
                handAims_[i]->location_.next = &handAims_[i]->velocity_;
                xrLocateSpace(handAims_[i]->actionSpace_, headSpace_, frameState.predictedDisplayTime, &handAims_[i]->location_);
            }

            if (handGrips_[i])
            {
                handGrips_[i]->location_.next = &handGrips_[i]->velocity_;
                xrLocateSpace(handGrips_[i]->actionSpace_, headSpace_, frameState.predictedDisplayTime, &handGrips_[i]->location_);
            }
        }

    // eyes
        XrViewLocateInfo viewInfo = { XR_TYPE_VIEW_LOCATE_INFO };
        viewInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        viewInfo.space = headSpace_;
        viewInfo.displayTime = frameState.predictedDisplayTime;

        XrViewState viewState = { XR_TYPE_VIEW_STATE };
        unsigned viewCt = 0;
        xrLocateViews(session_, &viewInfo, &viewState, 2, &viewCt, views_);

    // handle actions
        if (activeActionSet_)
        {
            auto set = activeActionSet_->Cast<XRActionSet>();

            XrActiveActionSet activeSet = { };
            activeSet.actionSet = set->actionSet_;

            XrActionsSyncInfo sync = { XR_TYPE_ACTIONS_SYNC_INFO };
            sync.activeActionSets = &activeSet;
            sync.countActiveActionSets = 1;
            xrSyncActions(session_, &sync);

            using namespace BeginFrame;
            UpdateBindings(data[P_TIMESTEP].GetFloat());
        }
    }

    void OpenXR::HandlePreRender(StringHash, VariantMap&)
    {
        if (IsLive())
        {
            XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            unsigned imgID;
            auto res = xrAcquireSwapchainImage(swapChain_, &acquireInfo, &imgID);
            if (res != XR_SUCCESS)
            {
                URHO3D_LOGERRORF("Failed to acquire swapchain: %s", xrGetErrorStr(res));
                return;
            }

            XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            waitInfo.timeout = XR_INFINITE_DURATION;
            res = xrWaitSwapchainImage(swapChain_, &waitInfo);
            if (res != XR_SUCCESS)
                URHO3D_LOGERRORF("Failed to wait on swapchain: %s", xrGetErrorStr(res));

            // update which shared-texture we're using so UpdateRig will do things correctly.
            sharedTexture_ = eyeColorTextures_[imgID];

            if (autoClearMasks_ && supportsMask_)
                DrawEyeMask();
            else
            {
                auto gfx = GetSubsystem<Graphics>();
                gfx->ResetRenderTargets();
                gfx->SetRenderTarget(0, sharedTexture_->GetRenderSurface());
                gfx->SetDepthStencil(sharedDS_);
                gfx->SetViewport({ 0, 0, eyeTexWidth_ * 2, eyeTexHeight_ });
                gfx->Clear(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_STENCIL);
            }
        }
    }

    void OpenXR::HandlePostRender(StringHash, VariantMap&)
    {
        if (IsLive())
        {
#define CHECKVIEW(EYE) (views_[EYE].fov.angleLeft == 0 || views_[EYE].fov.angleRight == 0 || views_[EYE].fov.angleUp == 0 || views_[EYE].fov.angleDown == 0)
            
            XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            xrReleaseSwapchainImage(swapChain_, &releaseInfo);

            // it's harmless but checking this will prevent early bad draws with null FOV
            // XR eats the error, but handle it anyways to keep a clean output log
            if (CHECKVIEW(VR_EYE_LEFT) || CHECKVIEW(VR_EYE_RIGHT))
                return;

            XrCompositionLayerProjectionView eyes[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
            eyes[VR_EYE_LEFT].subImage.imageArrayIndex = 0;
            eyes[VR_EYE_LEFT].subImage.swapchain = swapChain_;
            eyes[VR_EYE_LEFT].subImage.imageRect = { { 0, 0 }, { eyeTexWidth_, eyeTexHeight_} };
            eyes[VR_EYE_LEFT].fov = views_[VR_EYE_LEFT].fov;
            eyes[VR_EYE_LEFT].pose = views_[VR_EYE_LEFT].pose;

            eyes[VR_EYE_RIGHT].subImage.imageArrayIndex = 0;
            eyes[VR_EYE_RIGHT].subImage.swapchain = swapChain_;
            eyes[VR_EYE_RIGHT].subImage.imageRect = { { eyeTexWidth_, 0 }, { eyeTexWidth_, eyeTexHeight_} };
            eyes[VR_EYE_RIGHT].fov = views_[VR_EYE_RIGHT].fov;
            eyes[VR_EYE_RIGHT].pose = views_[VR_EYE_RIGHT].pose;

            XrCompositionLayerProjection proj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
            proj.viewCount = 2;
            proj.views = eyes;
            proj.space = headSpace_;

            XrCompositionLayerBaseHeader* header = (XrCompositionLayerBaseHeader*)&proj;

            XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
            endInfo.layerCount = 1;
            endInfo.layers = &header;
            endInfo.environmentBlendMode = blendMode_;
            endInfo.displayTime = predictedTime_;
                
            xrEndFrame(session_, &endInfo);
        }
    }

    void OpenXR::BindActions(SharedPtr<XMLFile> doc)
    {
        auto root = doc->GetRoot();

        auto sets = root.GetChild("actionsets");

        XrPath handPaths[2];
        xrStringToPath(instance_, "/user/hand/left", &handPaths[VR_HAND_LEFT]);
        xrStringToPath(instance_, "/user/hand/right", &handPaths[VR_HAND_RIGHT]);

        for (auto set = root.GetChild("actionset"); set.NotNull(); set = set.GetNext("actionset"))
        {
            XrActionSetCreateInfo setCreateInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
            String setName = set.GetAttribute("name");
            String setLocalName = GetSubsystem<Localization>()->Get(setName);
            strncpy(setCreateInfo.actionSetName, setName.CString(), XR_MAX_ACTION_SET_NAME_SIZE);
            strncpy(setCreateInfo.localizedActionSetName, setLocalName.CString(), XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE);
            
            XrActionSet createSet = { };
            auto errCode = xrCreateActionSet(instance_, &setCreateInfo, &createSet);
            if (errCode != XR_SUCCESS)
            {
                URHO3D_LOGERRORF("Failed to create ActionSet: %s, error: %s", setName.CString(), xrGetErrorStr(errCode));
                continue;
            }

            // create our wrapper
            SharedPtr<XRActionSet> actionSet(new XRActionSet(GetContext()));
            actionSet->actionSet_ = createSet;
            actionSets_.Insert({ setName, actionSet });

            auto bindings = set.GetChild("actions");
            for (auto child = bindings.GetChild("action"); child.NotNull(); child = child.GetNext("action"))
            {
                String name = child.GetAttribute("name");
                String type = child.GetAttribute("type");
                bool handed = child.GetBool("handed");

                SharedPtr<XRActionBinding> binding(new XRActionBinding(GetContext(), this));
                SharedPtr<XRActionBinding> otherHand = binding; // if identical it won't be pushed

                XrActionCreateInfo createInfo = { XR_TYPE_ACTION_CREATE_INFO };
                if (handed)
                {
                    otherHand = new XRActionBinding(GetContext(), this);
                    binding->hand_ = VR_HAND_LEFT;
                    binding->subPath_ = handPaths[VR_HAND_LEFT];
                    otherHand->hand_ = VR_HAND_RIGHT;
                    otherHand->subPath_ = handPaths[VR_HAND_RIGHT];

                    createInfo.countSubactionPaths = 2;
                    createInfo.subactionPaths = handPaths;
                    binding->hand_ = VR_HAND_LEFT;
                    otherHand->hand_ = VR_HAND_RIGHT;
                }
                else
                    binding->hand_ = VR_HAND_NONE;

                String localizedName = GetSubsystem<Localization>()->Get(name);
                strcpy_s(createInfo.actionName, 64, name.CString());
                strcpy_s(createInfo.localizedActionName, 128, localizedName.CString());

#define DUPLEX(F, V) binding->F = V; otherHand->F = V

                DUPLEX(path_, name);
                DUPLEX(localizedName_, localizedName);

                if (type == "boolean")
                {
                    DUPLEX(dataType_, VAR_BOOL);
                    createInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
                }
                else if (type == "vector1" || type == "single")
                {
                    DUPLEX(dataType_, VAR_FLOAT);
                    createInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
                }
                else if (type == "vector2")
                {
                    DUPLEX(dataType_, VAR_VECTOR2);
                    createInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
                }
                else if (type == "vector3")
                {
                    DUPLEX(dataType_, VAR_VECTOR3);
                    createInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
                }
                else if (type == "pose")
                {
                    DUPLEX(dataType_, VAR_MATRIX3X4);
                    createInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
                }
                else if (type == "haptic")
                {
                    DUPLEX(dataType_, VAR_NONE);
                    DUPLEX(haptic_, true);
                    createInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
                }
                else
                {
                    URHO3D_LOGERRORF("Unknown XR action type: %s", type.CString());
                    continue;
                }

                auto result = xrCreateAction(createSet, &createInfo, &binding->action_);
                if (result != XR_SUCCESS)
                {
                    URHO3D_LOGERRORF("Failed to create action %s because %s", name.CString(), xrGetErrorStr(result));
                    continue;
                }

                if (binding->dataType_ == VAR_MATRIX3X4 || binding->dataType_ == VAR_VECTOR3)
                {
                    XrActionSpaceCreateInfo spaceInfo = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
                    spaceInfo.action = binding->action_;
                    spaceInfo.poseInActionSpace = xrPoseIdentity;
                    if (handed)
                    {
                        spaceInfo.subactionPath = handPaths[0];
                        xrCreateActionSpace(session_, &spaceInfo, &binding->actionSpace_);
                        spaceInfo.subactionPath = handPaths[1];
                        xrCreateActionSpace(session_, &spaceInfo, &otherHand->actionSpace_);

                        if (child.GetBool("grip"))
                        {
                            binding->isPose_ = true;
                            otherHand->isPose_ = true;
                        }
                        else if (child.GetBool("aim"))
                        {
                            binding->isAimPose_ = true;
                            otherHand->isAimPose_ = true;
                        }
                    }
                    else
                        xrCreateActionSpace(session_, &spaceInfo, &binding->actionSpace_);
                }

                DUPLEX(set_, createSet);
                otherHand->action_ = binding->action_;

                actionSet->bindings_.Push(binding);
                if (otherHand != binding)
                {
                    otherHand->responsibleForDelete_ = false;
                    actionSet->bindings_.Push(otherHand);
                }
            }

#undef DUPLEX

            for (auto child = set.GetChild("profile"); child.NotNull(); child = child.GetNext("profile"))
            {
                String device = child.GetAttribute("device");

                XrPath devicePath;
                xrStringToPath(instance_, device.CString(), &devicePath);

                XrInteractionProfileSuggestedBinding suggest = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
                suggest.interactionProfile = devicePath;
                PODVector<XrActionSuggestedBinding> bindings;

                for (auto bind = child.GetChild("bind"); bind.NotNull(); bind = bind.GetNext("bind"))
                {
                    String action = bind.GetAttribute("action");
                    String bindStr = bind.GetAttribute("path");

                    XrPath bindPath;
                    xrStringToPath(instance_, bindStr.CString(), &bindPath);

                    XrActionSuggestedBinding b = { };

                    
                    for (auto found : actionSet->bindings_)
                    {
                        if (found->path_.Compare(action, false) == 0)
                        {
                            b.action = found->Cast<XRActionBinding>()->action_;
                            b.binding = bindPath;
                            bindings.Push(b);
                            break;
                        }
                    }
                }

                if (!bindings.Empty())
                {
                    suggest.countSuggestedBindings = bindings.Size();
                    suggest.suggestedBindings = bindings.Buffer();

                    auto res = xrSuggestInteractionProfileBindings(instance_, &suggest);
                    if (res != XR_SUCCESS)
                        URHO3D_LOGERRORF("Failed to suggest bindings: %s", xrGetErrorStr(res));
                }
            }
        }

        UpdateBindingBound();
    }

    void OpenXR::SetCurrentActionSet(SharedPtr<XRActionGroup> set)
    {
        if (session_ && set != nullptr)
        {
            auto xrSet = set->Cast<XRActionSet>();
            if (xrSet->actionSet_)
            {
                activeActionSet_ = set;

                XrSessionActionSetsAttachInfo attachInfo = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
                attachInfo.actionSets = &xrSet->actionSet_;
                attachInfo.countActionSets = 1;
                xrAttachSessionActionSets(session_, &attachInfo);

                UpdateBindingBound();
            }
        }
    }

    void OpenXR::UpdateBindings(float t)
    {
        if (instance_ == 0)
            return;

        if (!IsLive())
            return;

        auto& eventData = GetEventDataMap();
        using namespace VRBindingChange;

        eventData[VRBindingChange::P_ACTIVE] = true;

        for (auto b : activeActionSet_->bindings_)
        {
            auto bind = b->Cast<XRActionBinding>();
            if (bind->action_)
            {
                eventData[P_NAME] = bind->localizedName_;
                eventData[P_BINDING] = bind;

#define SEND_EVENT eventData[P_DATA] = bind->storedData_; eventData[P_DELTA] = bind->delta_; eventData[P_EXTRADELTA] = bind->extraDelta_[0]

                XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
                getInfo.action = bind->action_;
                getInfo.subactionPath = bind->subPath_;

                switch (bind->dataType_)
                {
                case VAR_BOOL: {
                    XrActionStateBoolean boolC = { XR_TYPE_ACTION_STATE_BOOLEAN };
                    if (xrGetActionStateBoolean(session_, &getInfo, &boolC) == XR_SUCCESS)
                    {
                        bind->active_ = boolC.isActive;
                        if (boolC.changedSinceLastSync)
                        {
                            bind->storedData_ = boolC.currentState;
                            bind->changed_ = true;
                            bind->PushWindow(boolC.currentState, t);
                            SEND_EVENT;
                        }
                        else
                            bind->changed_ = false;
                    }
                }
                    break;
                case VAR_FLOAT: {
                    XrActionStateFloat floatC = { XR_TYPE_ACTION_STATE_FLOAT };
                    if (xrGetActionStateFloat(session_, &getInfo, &floatC) == XR_SUCCESS)
                    {
                        bind->active_ = floatC.isActive;
                        if (floatC.changedSinceLastSync)
                        {
                            bind->storedData_ = floatC.currentState;
                            bind->changed_ = true;
                            bind->PushWindow(floatC.currentState, t);
                            SEND_EVENT;
                        }
                        else
                            bind->changed_ = false;
                    }
                }
                    break;
                case VAR_VECTOR2: {
                    XrActionStateVector2f vec = { XR_TYPE_ACTION_STATE_VECTOR2F };
                    if (xrGetActionStateVector2f(session_, &getInfo, &vec) == XR_SUCCESS)
                    {
                        bind->active_ = vec.isActive;
                        Vector2 v(vec.currentState.x, vec.currentState.y);
                        if (vec.changedSinceLastSync)
                        {
                            bind->storedData_ = v;
                            bind->changed_ = true;
                            bind->PushWindow(v, t);
                            SEND_EVENT;
                        }
                        else
                            bind->changed_ = false;
                    }
                }
                    break;
                case VAR_VECTOR3: {
                    XrActionStatePose pose = { XR_TYPE_ACTION_STATE_POSE };
                    if (xrGetActionStatePose(session_, &getInfo, &pose) == XR_SUCCESS)
                    {
                        bind->active_ = pose.isActive;
                        Vector3 v = uxrGetVec(bind->location_.pose.position);
                        bind->storedData_ = v;
                        bind->changed_ = true;
                        bind->PushWindow(v, t);
                        bind->extraData_[0] = uxrGetVec(bind->velocity_.linearVelocity);
                    }
                } break;
                case VAR_MATRIX3X4: {
                    XrActionStatePose pose = { XR_TYPE_ACTION_STATE_POSE };
                    if (xrGetActionStatePose(session_, &getInfo, &pose) == XR_SUCCESS)
                    {
                        bind->active_ = pose.isActive;
                        Matrix3x4 m = Matrix3x4(uxrGetVec(bind->location_.pose.position), uxrGetQuat(bind->location_.pose.orientation), 1);
                        bind->storedData_ = m;
                        bind->changed_ = true;
                        bind->PushWindow(m, t);

                        bind->extraData_[0] = uxrGetVec(bind->velocity_.linearVelocity);
                        bind->extraData_[1] = uxrGetVec(bind->velocity_.angularVelocity);
                    }
                } break;
                }
            }
        }
    }

    void OpenXR::GetHiddenAreaMask()
    {
        // extension wasn't supported
        if (!supportsMask_)
            return;

        for (int eye = 0; eye < 2; ++eye)
        {
            XrVisibilityMaskKHR mask = { XR_TYPE_VISIBILITY_MASK_KHR };
        // hidden
            {

                xrGetVisibilityMaskKHR(session_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye, XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, &mask);

                PODVector<XrVector2f> verts;
                verts.Resize(mask.vertexCountOutput);
                PODVector<unsigned> indices;
                indices.Resize(mask.indexCountOutput);

                mask.vertexCapacityInput = verts.Size();
                mask.indexCapacityInput = indices.Size();

                mask.vertices = verts.Buffer();
                mask.indices = indices.Buffer();

                xrGetVisibilityMaskKHR(session_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye, XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, &mask);

                PODVector<Vector3> vtxData; vtxData.Resize(verts.Size());
                for (unsigned i = 0; i < verts.Size(); ++i)
                    vtxData[i] = Vector3(verts[i].x, verts[i].y, 0.0f);

                VertexBuffer* vtx = new VertexBuffer(GetContext());
                vtx->SetSize(vtxData.Size(), ELEMENT_POSITION);
                vtx->SetData(vtxData.Buffer());

                IndexBuffer* idx = new IndexBuffer(GetContext());
                idx->SetSize(indices.Size(), true);
                idx->SetData(indices.Buffer());

                hiddenAreaMesh_[eye] = new Geometry(GetContext());
                hiddenAreaMesh_[eye]->SetVertexBuffer(0, vtx);
                hiddenAreaMesh_[eye]->SetIndexBuffer(idx);
                hiddenAreaMesh_[eye]->SetDrawRange(TRIANGLE_LIST, 0, indices.Size());
            }

        // visible
            {
                mask.indexCapacityInput = 0;
                mask.vertexCapacityInput = 0;
                mask.indices = nullptr;
                mask.vertices = nullptr;
                mask.indexCountOutput = 0;
                mask.vertexCountOutput = 0;
                
                xrGetVisibilityMaskKHR(session_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye, XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR, &mask);

                PODVector<XrVector2f> verts;
                verts.Resize(mask.vertexCountOutput);
                PODVector<unsigned> indices;
                indices.Resize(mask.indexCountOutput);

                mask.vertexCapacityInput = verts.Size();
                mask.indexCapacityInput = indices.Size();

                mask.vertices = verts.Buffer();
                mask.indices = indices.Buffer();

                xrGetVisibilityMaskKHR(session_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye, XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR, &mask);

                PODVector<Vector3> vtxData; vtxData.Resize(verts.Size());
                for (unsigned i = 0; i < verts.Size(); ++i)
                    vtxData[i] = Vector3(verts[i].x, verts[i].y, 0.0f);

                VertexBuffer* vtx = new VertexBuffer(GetContext());
                vtx->SetSize(vtxData.Size(), ELEMENT_POSITION);
                vtx->SetData(vtxData.Buffer());

                IndexBuffer* idx = new IndexBuffer(GetContext());
                idx->SetSize(indices.Size(), true);
                idx->SetData(indices.Buffer());

                visibleAreaMesh_[eye] = new Geometry(GetContext());
                visibleAreaMesh_[eye]->SetVertexBuffer(0, vtx);
                visibleAreaMesh_[eye]->SetIndexBuffer(idx);
                visibleAreaMesh_[eye]->SetDrawRange(TRIANGLE_LIST, 0, indices.Size());
            }

        // build radial from line loop
            {
                mask.indexCapacityInput = 0;
                mask.vertexCapacityInput = 0;
                mask.indices = nullptr;
                mask.vertices = nullptr;
                mask.indexCountOutput = 0;
                mask.vertexCountOutput = 0;

                xrGetVisibilityMaskKHR(session_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye, XR_VISIBILITY_MASK_TYPE_LINE_LOOP_KHR, &mask);

                PODVector<XrVector2f> verts;
                verts.Resize(mask.vertexCountOutput);
                PODVector<unsigned> indices;
                indices.Resize(mask.indexCountOutput);

                mask.vertexCapacityInput = verts.Size();
                mask.indexCapacityInput = indices.Size();

                mask.vertices = verts.Buffer();
                mask.indices = indices.Buffer();

                xrGetVisibilityMaskKHR(session_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye, XR_VISIBILITY_MASK_TYPE_LINE_LOOP_KHR, &mask);

                struct V {
                    Vector3 pos;
                    Color color;
                };

                PODVector<V> vtxData; vtxData.Resize(verts.Size());
                Vector3 centroid = Vector3::ZERO;
                Vector3 minVec = Vector3(10000, 10000, 10000);
                Vector3 maxVec = Vector3(-10000, -10000, -10000);
                for (unsigned i = 0; i < verts.Size(); ++i)
                {
                    vtxData[i] = { Vector3(verts[i].x, verts[i].y, 0.0f), Color::WHITE };
                    centroid += vtxData[i].pos;
                }
                centroid /= verts.Size();

                PODVector<unsigned short> newIndices;
                vtxData.Push({ centroid, Color(1.0f, 1.0f, 1.0f, 0.0f) });

                // turn the line loop into a fan
                for (unsigned i = 0; i < indices.Size(); ++i)
                {
                    unsigned me = indices[i];
                    unsigned next = indices[(i + 1) % indices.Size()];

                    newIndices.Push(vtxData.Size());
                    newIndices.Push(me);
                    newIndices.Push(next);
                }

                VertexBuffer* vtx = new VertexBuffer(GetContext());
                vtx->SetSize(vtxData.Size(), ELEMENT_POSITION | ELEMENT_COLOR);
                vtx->SetData(vtxData.Buffer());

                IndexBuffer* idx = new IndexBuffer(GetContext());
                idx->SetSize(newIndices.Size(), false);
                idx->SetData(newIndices.Buffer());

                radialAreaMesh_[eye] = new Geometry(GetContext());
                radialAreaMesh_[eye]->SetVertexBuffer(0, vtx);
                radialAreaMesh_[eye]->SetIndexBuffer(idx);
                radialAreaMesh_[eye]->SetDrawRange(TRIANGLE_LIST, 0, indices.Size());
            }
        }
    }

    void OpenXR::LoadControllerModels()
    {
        if (!supportsControllerModel_)
            return;

        XrPath handPaths[2];
        xrStringToPath(instance_, "/user/hand/left", &handPaths[0]);
        xrStringToPath(instance_, "/user/hand/right", &handPaths[1]);

        XrControllerModelKeyStateMSFT states[2] = { { XR_TYPE_CONTROLLER_MODEL_KEY_STATE_MSFT }, { XR_TYPE_CONTROLLER_MODEL_KEY_STATE_MSFT } };
        XrResult errCodes[2];
        errCodes[0] = xrGetControllerModelKeyMSFT(session_, handPaths[0], &states[0]);
        errCodes[1] = xrGetControllerModelKeyMSFT(session_, handPaths[1], &states[1]);

        for (int i = 0; i < 2; ++i)
        {
            // skip if we're the same, we could change
            if (states[i].modelKey == wandModels_[i].modelKey_)
                continue;

            wandModels_[i].modelKey_ = states[i].modelKey;

            if (errCodes[i] == XR_SUCCESS)
            {
                unsigned dataSize = 0;
                if (xrLoadControllerModelMSFT(session_, states[i].modelKey, 0, &dataSize, nullptr) == XR_SUCCESS)
                {
                    PODVector<unsigned char> data;
                    data.Resize(dataSize);
                    if (xrLoadControllerModelMSFT(session_, states[i].modelKey, data.Size(), &dataSize, data.Buffer()) == XR_SUCCESS)
                    {
                        tinygltf::Model model;
                        tinygltf::TinyGLTF ctx;
                        tinygltf::Scene scene;

                        std::string err, warn;
                        if (ctx.LoadBinaryFromMemory(&model, &err, &warn, data.Buffer(), data.Size()))
                        {
                            wandModels_[i].model_ = LoadGLTFModel(GetContext(), model);
                        }
                        else
                            wandModels_[i].model_.Reset();

                        XR_INIT_TYPE(wandModels_[i].properties_, XR_TYPE_CONTROLLER_MODEL_NODE_PROPERTIES_MSFT);

                        XrControllerModelPropertiesMSFT props = { XR_TYPE_CONTROLLER_MODEL_PROPERTIES_MSFT };
                        props.nodeCapacityInput = 256;
                        props.nodeCountOutput = 0;
                        props.nodeProperties = wandModels_[i].properties_;
                        if (xrGetControllerModelPropertiesMSFT(session_, states[i].modelKey, &props) == XR_SUCCESS)
                        {
                            wandModels_[i].numProperties_ = props.nodeCountOutput;
                        }
                        else
                            wandModels_[i].numProperties_ = 0;

                        auto& data = GetEventDataMap();
                        data[VRControllerChange::P_HAND] = i;
                        SendEvent(E_VRCONTROLLERCHANGE, data);
                    }
                }
            }
        }
    }

    void OpenXR::UpdateControllerModel(VRHand hand, SharedPtr<Node> model)
    {
        if (!supportsControllerModel_)
            return;

        if (model == nullptr)
            return;

        if (wandModels_[hand].modelKey_ == 0)
            return;

        // nothing to animate
        if (wandModels_[hand].numProperties_ == 0)
            return;

        XrControllerModelNodeStateMSFT nodeStates[256];
        XR_INIT_TYPE(nodeStates, XR_TYPE_CONTROLLER_MODEL_NODE_STATE_MSFT);

        XrControllerModelStateMSFT state = { XR_TYPE_CONTROLLER_MODEL_STATE_MSFT };
        state.nodeCapacityInput = 256;
        state.nodeStates = nodeStates;

        auto errCode = xrGetControllerModelStateMSFT(session_, wandModels_[hand].modelKey_, &state);
        if (errCode == XR_SUCCESS)
        {
            auto node = model;
            for (unsigned i = 0; i < state.nodeCountOutput; ++i)
            {
                SharedPtr<Node> bone;
                if (strlen(wandModels_[hand].properties_[i].parentNodeName))
                {
                    if (auto parent = node->GetChild(wandModels_[hand].properties_[i].parentNodeName, true))
                        bone = parent->GetChild(wandModels_[hand].properties_[i].nodeName);
                }
                else
                    bone = node->GetChild(wandModels_[hand].properties_[i].nodeName, true);
                
                if (bone != nullptr)
                {
                    // we have a 1,1,-1 scale at the root to flip gltf coordinate system to ours,
                    // because of that this transform needs to be direct and not converted, or it'll get unconverted
                    // TODO: figure out how to properly fully flip the gltf nodes and vertices
                    Vector3 t = Vector3(nodeStates[i].nodePose.position.x, nodeStates[i].nodePose.position.y, nodeStates[i].nodePose.position.z);
                    auto& q = nodeStates[i].nodePose.orientation;
                    Quaternion outQ = Quaternion(q.w, q.x, q.y, q.z);

                    bone->SetTransform(Matrix3x4(t, outQ, Vector3(1,1,1)));
                }
            }
        }
    }

    void OpenXR::TriggerHaptic(VRHand hand, float durationSeconds, float cyclesPerSec, float amplitude)
    {
        if (activeActionSet_)
        {
            for (auto b : activeActionSet_->bindings_)
            {
                if (b->IsHaptic() && b->Hand() == hand)
                    b->Vibrate(durationSeconds, cyclesPerSec, amplitude);
            }
        }
    }

    Matrix3x4 OpenXR::GetHandTransform(VRHand hand) const
    {
        if (hand == VR_HAND_NONE)
            return Matrix3x4();

        if (!handGrips_[hand])
            return Matrix3x4();

        auto q = uxrGetQuat(handGrips_[hand]->location_.pose.orientation);
        auto v = uxrGetVec(handGrips_[hand]->location_.pose.position);

        // bring it into head space instead of stage space
        auto headInv = GetHeadTransform().Inverse();
        return headInv * Matrix3x4(v, q, 1.0f);
    }

    Matrix3x4 OpenXR::GetHandAimTransform(VRHand hand) const
    {
        if (hand == VR_HAND_NONE)
            return Matrix3x4();

        if (!handAims_[hand])
            return Matrix3x4();

        // leave this in stage space, that's what we want
        auto q = uxrGetQuat(handAims_[hand]->location_.pose.orientation);
        auto v = uxrGetVec(handAims_[hand]->location_.pose.position);
        return Matrix3x4(v, q, 1.0f);
    }

    Ray OpenXR::GetHandAimRay(VRHand hand) const
    {
        if (hand == VR_HAND_NONE)
            return Ray();

        if (!handAims_[hand])
            return Ray();

        // leave this one is stage space, that's what we want
        auto q = uxrGetQuat(handAims_[hand]->location_.pose.orientation);
        auto v = uxrGetVec(handAims_[hand]->location_.pose.position);
        return Ray(v, (q * Vector3(0, 0, 1)).Normalized());
    }
    
    void OpenXR::GetHandVelocity(VRHand hand, Vector3* linear, Vector3* angular) const
    {
        if (hand == VR_HAND_NONE)
            return;

        if (!handGrips_[hand])
            return;
        
        if (linear && handGrips_[hand]->velocity_.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
            *linear = uxrGetVec(handGrips_[hand]->velocity_.linearVelocity);
        if (angular && handGrips_[hand]->velocity_.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)
            *angular = uxrGetVec(handGrips_[hand]->velocity_.angularVelocity);
    }

    void OpenXR::UpdateHands(Scene* scene, Node* rigRoot, Node* leftHand, Node* rightHand)
    {
        if (!IsLive())
            return;

        LoadControllerModels();

        if (leftHand == nullptr)
            leftHand = rigRoot->CreateChild("Left_Hand");
        if (rightHand == nullptr)
            rightHand = rigRoot->CreateChild("Right_Hand");

        if (handGrips_[0] && handGrips_[1])
        {
            auto lq = uxrGetQuat(handGrips_[VR_HAND_LEFT]->location_.pose.orientation);
            auto lp = uxrGetVec(handGrips_[VR_HAND_LEFT]->location_.pose.position);

            static const StringHash lastTrans = StringHash("LastTransform");
            static const StringHash lastTransWS = StringHash("LastTransformWS");

            leftHand->SetVar(lastTrans, leftHand->GetTransform());
            leftHand->SetVar(lastTransWS, leftHand->GetWorldTransform());
            leftHand->SetEnabled(handGrips_[VR_HAND_LEFT]->location_.locationFlags & (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT));
            leftHand->SetPosition(lp);
            if (handGrips_[VR_HAND_LEFT]->location_.locationFlags & (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT))
                leftHand->SetRotation(lq);

            auto rq = uxrGetQuat(handGrips_[VR_HAND_RIGHT]->location_.pose.orientation);
            auto rp = uxrGetVec(handGrips_[VR_HAND_RIGHT]->location_.pose.position);

            rightHand->SetVar(lastTrans, leftHand->GetTransform());
            rightHand->SetVar(lastTransWS, leftHand->GetWorldTransform());
            rightHand->SetEnabled(handGrips_[VR_HAND_RIGHT]->location_.locationFlags & (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT));
            rightHand->SetPosition(rp);
            if (handGrips_[VR_HAND_RIGHT]->location_.locationFlags & (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT))
                rightHand->SetRotation(rq);
        }
    }

    Matrix3x4 OpenXR::GetEyeLocalTransform(VREye eye) const
    {
        // TODO: fixme, why is view space not correct xrLocateViews( view-space )
        return GetHeadTransform().Inverse() * uxrGetTransform(views_[eye].pose);
    }

    Matrix4 OpenXR::GetProjection(VREye eye, float nearDist, float farDist) const
    {
        return uxrGetProjection(nearDist, farDist, views_[eye].fov.angleLeft, views_[eye].fov.angleUp, views_[eye].fov.angleRight, views_[eye].fov.angleDown);
    }

    Matrix3x4 OpenXR::GetHeadTransform() const
    {
        return uxrGetTransform(headLoc_.pose);
    }

    OpenXR::XRActionBinding::~XRActionBinding()
    {
        if (responsibleForDelete_ && action_)
            xrDestroyAction(action_);
        action_ = 0;
    }

    OpenXR::XRActionSet::~XRActionSet()
    {
        bindings_.Clear();
        if (actionSet_)
            xrDestroyActionSet(actionSet_);
        actionSet_ = 0;
    }

    void OpenXR::XRActionBinding::Vibrate(float duration, float freq, float amplitude)
    {
        if (!xr_->IsLive())
            return;

        XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
        info.action = action_;
        info.subactionPath = subPath_;

        XrHapticVibration vib = { XR_TYPE_HAPTIC_VIBRATION };
        vib.amplitude = amplitude;
        vib.frequency = freq;
        vib.duration = duration * 1000.0f;
        xrApplyHapticFeedback(xr_->session_, &info, (XrHapticBaseHeader*)&vib);
    }

    void OpenXR::UpdateBindingBound()
    {
        if (session_ == 0)
            return;

        if (activeActionSet_)
        {
            for (auto b : activeActionSet_->bindings_)
            {
                auto bind = b->Cast<XRActionBinding>();
                XrBoundSourcesForActionEnumerateInfo info = { XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO };
                info.action = bind->action_;
                unsigned binds = 0;
                xrEnumerateBoundSourcesForAction(session_, &info, 0, &binds, nullptr);
                b->isBound_ = binds > 0;

                if (b->isAimPose_)
                    handAims_[b->Hand()] = b->Cast<XRActionBinding>();
                if (b->isPose_)
                    handGrips_[b->Hand()] = b->Cast<XRActionBinding>();
            }
        }
    }

    void GLTFRecurseModel(Context* ctx, tinygltf::Model& gltf, Node* parent, int nodeIndex, int parentIndex, Material* mat, Matrix3x4 matStack)
    {
        auto& n = gltf.nodes[nodeIndex];

        auto node = parent->CreateChild(n.name.c_str());

        // root node will deal with the 1,1,-1 - so just accept the transforms we get
        // same with vertex data later
        if (n.translation.size())
        {
            Vector3 translation = Vector3(n.translation[0], n.translation[1], n.translation[2]);
            Quaternion rotation = Quaternion(n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]);
            Vector3 scale = Vector3(n.scale[0], n.scale[1], n.scale[2]);
            node->SetPosition(translation);
            node->SetRotation(rotation);
            node->SetScale(scale);
        }
        else if (n.matrix.size())
        {
            Matrix3x4 mat = Matrix3x4(
                n.matrix[0], n.matrix[4], n.matrix[8], n.matrix[12],
                n.matrix[1], n.matrix[5], n.matrix[9], n.matrix[13],
                n.matrix[2], n.matrix[6], n.matrix[10], n.matrix[14]
            );
            node->SetTransform(mat);
        }
        else
            node->SetTransform(Matrix3x4::IDENTITY);

        if (n.mesh != -1)
        {
            auto& mesh = gltf.meshes[n.mesh];
            BoundingBox bounds;
            bounds.Clear();
            for (auto& prim : mesh.primitives)
            {
                SharedPtr<Geometry> geom(new Geometry(ctx));

                if (prim.mode == TINYGLTF_MODE_TRIANGLES)
                {
                    SharedPtr<IndexBuffer> idxBuffer(new IndexBuffer(ctx));
                    Vector< SharedPtr<VertexBuffer> > vertexBuffers;

                    struct Vertex {
                        Vector3 pos;
                        Vector3 norm;
                        Vector2 tex;
                    };

                    PODVector<Vertex> verts;
                    verts.Resize(gltf.accessors[prim.attributes.begin()->second].count);

                    for (auto c : prim.attributes)
                    {
                        if (gltf.accessors[c.second].componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                        {
                            auto& access = gltf.accessors[c.second];
                            auto& view = gltf.bufferViews[access.bufferView];
                            auto& buffer = gltf.buffers[view.buffer];

                            LegacyVertexElement element;

                            String str(access.name.c_str());
                            if (str.Contains("position", false))
                                element = ELEMENT_POSITION;
                            else if (str.Contains("texcoord", false))
                                element = ELEMENT_TEXCOORD1;
                            else if (str.Contains("normal", false))
                                element = ELEMENT_NORMAL;

                            SharedPtr<VertexBuffer> vtx(new VertexBuffer(ctx));

                            size_t sizeElem = access.type == TINYGLTF_TYPE_VEC2 ? sizeof(Vector2) : sizeof(Vector3);
                            if (access.type == TINYGLTF_TYPE_VEC3)
                            {
                                const float* d = (const float*)&buffer.data[view.byteOffset + access.byteOffset];
                                if (element == ELEMENT_NORMAL)
                                {
                                    for (unsigned i = 0; i < access.count; ++i)
                                        verts[i].norm = Vector3(d[i * 3 + 0], d[i * 3 + 1], d[i * 3 + 2]);
                                }
                                else if (element == ELEMENT_POSITION)
                                {
                                    for (unsigned i = 0; i < access.count; ++i)
                                        bounds.Merge(verts[i].pos = Vector3(d[i * 3 + 0], d[i * 3 + 1], d[i * 3 + 2]));
                                }
                            }
                            else
                            {
                                const float* d = (const float*)&buffer.data[view.byteOffset + access.byteOffset];
                                for (unsigned i = 0; i < access.count; ++i)
                                    verts[i].tex = Vector2(d[i * 2 + 0], d[i * 2 + 1]);
                            }
                        }
                        else
                            URHO3D_LOGERRORF("Found unsupported GLTF component type for vertex data: %u", gltf.accessors[prim.indices].componentType);
                    }

                    VertexBuffer* buff = new VertexBuffer(ctx);
                    buff->SetSize(verts.Size(), { VertexElement(TYPE_VECTOR3, SEM_POSITION, 0, 0), VertexElement(TYPE_VECTOR3, SEM_NORMAL, 0, 0), VertexElement(TYPE_VECTOR2, SEM_TEXCOORD, 0, 0) });
                    buff->SetData(verts.Buffer());
                    vertexBuffers.Push(SharedPtr<VertexBuffer>(buff));

                    if (prim.indices != -1)
                    {
                        auto& access = gltf.accessors[prim.indices];
                        auto& view = gltf.bufferViews[access.bufferView];
                        auto& buffer = gltf.buffers[view.buffer];

                        if (gltf.accessors[prim.indices].componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                        {
                            PODVector<unsigned> indexData;
                            indexData.Resize(access.count);

                            const unsigned* indices = (const unsigned*)&buffer.data[view.byteOffset + access.byteOffset];
                            for (int i = 0; i < access.count; ++i)
                                indexData[i] = indices[i];

                            idxBuffer->SetSize(access.count, true, false);
                            idxBuffer->SetData(indexData.Buffer());
                        }
                        else if (gltf.accessors[prim.indices].componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                        {
                            PODVector<unsigned short> indexData;
                            indexData.Resize(access.count);

                            const unsigned short* indices = (const unsigned short*)&buffer.data[view.byteOffset + access.byteOffset];
                            for (int i = 0; i < access.count; ++i)
                                indexData[i] = indices[i];
                            for (int i = 0; i < indexData.Size(); i += 3)
                            {
                                Swap(indexData[i], indexData[i + 2]);
                            }

                            idxBuffer->SetSize(access.count, false, false);
                            idxBuffer->SetData(indexData.Buffer());
                        }
                        else
                        {
                            URHO3D_LOGERRORF("Found unsupported GLTF component type for index data: %u", gltf.accessors[prim.indices].componentType);
                            continue;
                        }
                    }

                    SharedPtr<Geometry> geom(new Geometry(ctx));
                    geom->SetIndexBuffer(idxBuffer);
                    geom->SetNumVertexBuffers(vertexBuffers.Size());
                    for (unsigned i = 0; i < vertexBuffers.Size(); ++i)
                        geom->SetVertexBuffer(i, vertexBuffers[0]);
                    geom->SetDrawRange(TRIANGLE_LIST, 0, idxBuffer->GetIndexCount());

                    SharedPtr<Model> m(new Model(ctx));
                    m->SetNumGeometries(1);
                    m->SetGeometry(0, 0, geom);
                    m->SetName(mesh.name.c_str());
                    m->SetBoundingBox(bounds);

                    auto sm = node->CreateComponent<StaticModel>();
                    sm->SetModel(m);
                    sm->SetMaterial(mat);
                }
            }
        }

        for (auto child : n.children)
            GLTFRecurseModel(ctx, gltf, node, child, nodeIndex, mat, node->GetWorldTransform());
    }

    SharedPtr<Texture2D> LoadGLTFTexture(Context* ctx, tinygltf::Model& gltf, int index)
    {
        auto gfx = ctx->GetSubsystem<Graphics>();

        auto img = gltf.images[index];
        SharedPtr<Texture2D> tex(new Texture2D(ctx));
        tex->SetSize(img.width, img.height, gfx->GetRGBAFormat());

        auto view = gltf.bufferViews[img.bufferView];

        MemoryBuffer buff(gltf.buffers[view.buffer].data.data() + view.byteOffset, view.byteLength);

        Image image(ctx);
        if (image.Load(buff))
        {
            tex->SetData(&image, true);
            return tex;
        }
        
        return nullptr;
    }

    SharedPtr<Node> LoadGLTFModel(Context* ctx, tinygltf::Model& gltf)
    {
        if (gltf.scenes.empty())
            return SharedPtr<Node>();

        // cloning because controllers could change or possibly even not be the same on each hand
        SharedPtr<Material> material = ctx->GetSubsystem<ResourceCache>()->GetResource<Material>("Materials/XRController.xml")->Clone();
        if (!gltf.materials.empty() && !gltf.textures.empty())
        {
            material->SetTexture(TU_DIFFUSE, LoadGLTFTexture(ctx, gltf, 0));
            if (gltf.materials[0].normalTexture.index)
                material->SetTexture(TU_NORMAL, LoadGLTFTexture(ctx, gltf, gltf.materials[0].normalTexture.index));
        }

        auto scene = gltf.scenes[gltf.defaultScene];
        SharedPtr<Node> root(new Node(ctx));
        root->SetScale(Vector3(1, 1, -1));
        //root->Rotate(Quaternion(45, Vector3::UP));
        for (auto n : scene.nodes)
            GLTFRecurseModel(ctx, gltf, root, n, -1, material, Matrix3x4::IDENTITY);

        return root;
    }

#pragma optimize("", on)
}