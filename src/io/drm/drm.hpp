#pragma once

#include "../internal.hpp"

struct IoDrmResources
{
    std::vector<drmModeConnector*> connectors;
    std::vector<drmModeEncoder*>   encoders;
    std::vector<drmModeCrtc*>      crtcs;
    std::vector<drmModePlane*>     planes;

    drmModeConnector* find_connector(u32 id);
    drmModeEncoder*   find_encoder(  u32 id);
    drmModeCrtc*      find_crtc(     u32 id);
    drmModePlane*     find_plane(    u32 id);

    IoDrmResources(int drm_fd);
    ~IoDrmResources();
};

struct IoDrmPropertyMap
{
    struct PropDeleter
    {
        void operator()(drmModeObjectProperties* v)
        {
            drmModeFreeObjectProperties(v);
        }
    };
    using PropPtr = std::unique_ptr<drmModeObjectProperties, PropDeleter>;

    int drm;
    PropPtr props;
    ankerl::unordered_dense::map<std::string_view, drmModePropertyRes*> properties;

    IoDrmPropertyMap() = default;

    IoDrmPropertyMap(int drm, u32 object_id, u32 object_type);

    IoDrmPropertyMap(IoDrmPropertyMap&& other);
    IoDrmPropertyMap& operator=(IoDrmPropertyMap&& other);

    ~IoDrmPropertyMap();

    u32 get_prop_id(   std::string_view prop_name);
    u64 get_prop_value(std::string_view prop_name);
    int get_enum_value(std::string_view prop_name, std::string_view enum_name);
};

// -----------------------------------------------------------------------------

struct IoDrmOutput : IoOutputBase
{
    u32 primary_plane_id;
    u32 crtc_id;
    u32 connector_id;

    IoDrmPropertyMap plane_prop;
    IoDrmPropertyMap crtc_prop;

    Ref<GpuImage> current_image;
    Ref<GpuImage> pending_image;

    GpuFormatSet format_set;

    std::chrono::steady_clock::time_point last_commit_time = {};

    GpuFormatSet formats;

    virtual auto info() -> IoOutputInfo final override
    {
        return {
            .size = size,
            .formats = &formats,
        };
    }

    virtual void commit(GpuImage*, GpuSyncpoint done, Flags<IoOutputCommitFlag>) final override;
};

struct IoDrmBuffer
{
    Weak<GpuImage> image;
    u32 fb2_handle;
};

struct IoDrm
{
    int fd;

    RefVector<IoDrmOutput> outputs;

    std::vector<IoDrmBuffer> buffer_cache;
};

// -----------------------------------------------------------------------------

auto parse_plane_formats(IoContext* io, IoDrmResources* resources, drmModePlane* plane) -> GpuFormatSet;
