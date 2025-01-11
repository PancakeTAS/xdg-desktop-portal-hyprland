#include "ScreencopyShared.hpp"
#include "../helpers/MiscFunctions.hpp"
#include <wayland-client.h>
#include "../helpers/Log.hpp"
#include <libdrm/drm_fourcc.h>
#include <assert.h>
#include "../core/PortalManager.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <hyprutils/os/Process.hpp>
using namespace Hyprutils::OS;

std::string sanitizeNameForWindowList(const std::string& name) {
    std::string result = name;
    std::replace(result.begin(), result.end(), '\'', ' ');
    std::replace(result.begin(), result.end(), '\"', ' ');
    std::replace(result.begin(), result.end(), '$', ' ');
    std::replace(result.begin(), result.end(), '`', ' ');
    for (size_t i = 1; i < result.size(); ++i) {
        if (result[i - 1] == '>' && result[i] == ']')
            result[i] = ' ';
    }
    return result;
}

std::string buildWindowList() {
    std::string result = "";
    if (!g_pPortalManager->m_sPortals.screencopy->hasToplevelCapabilities())
        return result;

    for (auto& e : g_pPortalManager->m_sHelpers.toplevel->m_vToplevels) {
        result += std::format("{}[HC>]{}[HT>]{}[HE>]", (uint32_t)(((uint64_t)e->handle->resource()) & 0xFFFFFFFF), sanitizeNameForWindowList(e->windowClass),
                              sanitizeNameForWindowList(e->windowTitle));
    }

    return result;
}

SSelectionData promptForScreencopySelection() {
    SSelectionData      data;
    data.type = TYPE_OUTPUT;
    data.output = "DP-2";
    data.allowToken = true;

    return data;
}

wl_shm_format wlSHMFromDrmFourcc(uint32_t format) {
    switch (format) {
        case DRM_FORMAT_ARGB8888: return WL_SHM_FORMAT_ARGB8888;
        case DRM_FORMAT_XRGB8888: return WL_SHM_FORMAT_XRGB8888;
        case DRM_FORMAT_RGBA8888:
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_BGRA8888:
        case DRM_FORMAT_BGRX8888:
        case DRM_FORMAT_NV12:
        case DRM_FORMAT_XRGB2101010:
        case DRM_FORMAT_XBGR2101010:
        case DRM_FORMAT_RGBX1010102:
        case DRM_FORMAT_BGRX1010102:
        case DRM_FORMAT_ARGB2101010:
        case DRM_FORMAT_ABGR2101010:
        case DRM_FORMAT_RGBA1010102:
        case DRM_FORMAT_BGRA1010102: return (wl_shm_format)format;
        default: Debug::log(ERR, "[screencopy] Unknown format {}", format); abort();
    }
}

uint32_t drmFourccFromSHM(wl_shm_format format) {
    switch (format) {
        case WL_SHM_FORMAT_ARGB8888: return DRM_FORMAT_ARGB8888;
        case WL_SHM_FORMAT_XRGB8888: return DRM_FORMAT_XRGB8888;
        case WL_SHM_FORMAT_RGBA8888:
        case WL_SHM_FORMAT_RGBX8888:
        case WL_SHM_FORMAT_ABGR8888:
        case WL_SHM_FORMAT_XBGR8888:
        case WL_SHM_FORMAT_BGRA8888:
        case WL_SHM_FORMAT_BGRX8888:
        case WL_SHM_FORMAT_NV12:
        case WL_SHM_FORMAT_XRGB2101010:
        case WL_SHM_FORMAT_XBGR2101010:
        case WL_SHM_FORMAT_RGBX1010102:
        case WL_SHM_FORMAT_BGRX1010102:
        case WL_SHM_FORMAT_ARGB2101010:
        case WL_SHM_FORMAT_ABGR2101010:
        case WL_SHM_FORMAT_RGBA1010102:
        case WL_SHM_FORMAT_BGRA1010102:
        case WL_SHM_FORMAT_BGR888: return (uint32_t)format;
        default: Debug::log(ERR, "[screencopy] Unknown format {}", (int)format); abort();
    }
}

spa_video_format pwFromDrmFourcc(uint32_t format) {
    switch (format) {
        case DRM_FORMAT_ARGB8888: return SPA_VIDEO_FORMAT_BGRA;
        case DRM_FORMAT_XRGB8888: return SPA_VIDEO_FORMAT_BGRx;
        case DRM_FORMAT_RGBA8888: return SPA_VIDEO_FORMAT_ABGR;
        case DRM_FORMAT_RGBX8888: return SPA_VIDEO_FORMAT_xBGR;
        case DRM_FORMAT_ABGR8888: return SPA_VIDEO_FORMAT_RGBA;
        case DRM_FORMAT_XBGR8888: return SPA_VIDEO_FORMAT_RGBx;
        case DRM_FORMAT_BGRA8888: return SPA_VIDEO_FORMAT_ARGB;
        case DRM_FORMAT_BGRX8888: return SPA_VIDEO_FORMAT_xRGB;
        case DRM_FORMAT_NV12: return SPA_VIDEO_FORMAT_NV12;
        case DRM_FORMAT_XRGB2101010: return SPA_VIDEO_FORMAT_xRGB_210LE;
        case DRM_FORMAT_XBGR2101010: return SPA_VIDEO_FORMAT_xBGR_210LE;
        case DRM_FORMAT_RGBX1010102: return SPA_VIDEO_FORMAT_RGBx_102LE;
        case DRM_FORMAT_BGRX1010102: return SPA_VIDEO_FORMAT_BGRx_102LE;
        case DRM_FORMAT_ARGB2101010: return SPA_VIDEO_FORMAT_ARGB_210LE;
        case DRM_FORMAT_ABGR2101010: return SPA_VIDEO_FORMAT_ABGR_210LE;
        case DRM_FORMAT_RGBA1010102: return SPA_VIDEO_FORMAT_RGBA_102LE;
        case DRM_FORMAT_BGRA1010102: return SPA_VIDEO_FORMAT_BGRA_102LE;
        case DRM_FORMAT_BGR888: return SPA_VIDEO_FORMAT_BGR;
        default: Debug::log(ERR, "[screencopy] Unknown format {}", (int)format); abort();
    }
}

std::string getRandName(std::string prefix) {
    std::srand(time(NULL));
    return prefix +
        std::format("{}{}{}{}{}{}", (int)(std::rand() % 10), (int)(std::rand() % 10), (int)(std::rand() % 10), (int)(std::rand() % 10), (int)(std::rand() % 10),
                    (int)(std::rand() % 10));
}

spa_video_format pwStripAlpha(spa_video_format format) {
    switch (format) {
        case SPA_VIDEO_FORMAT_BGRA: return SPA_VIDEO_FORMAT_BGRx;
        case SPA_VIDEO_FORMAT_ABGR: return SPA_VIDEO_FORMAT_xBGR;
        case SPA_VIDEO_FORMAT_RGBA: return SPA_VIDEO_FORMAT_RGBx;
        case SPA_VIDEO_FORMAT_ARGB: return SPA_VIDEO_FORMAT_xRGB;
        case SPA_VIDEO_FORMAT_ARGB_210LE: return SPA_VIDEO_FORMAT_xRGB_210LE;
        case SPA_VIDEO_FORMAT_ABGR_210LE: return SPA_VIDEO_FORMAT_xBGR_210LE;
        case SPA_VIDEO_FORMAT_RGBA_102LE: return SPA_VIDEO_FORMAT_RGBx_102LE;
        case SPA_VIDEO_FORMAT_BGRA_102LE: return SPA_VIDEO_FORMAT_BGRx_102LE;
        default: return SPA_VIDEO_FORMAT_UNKNOWN;
    }
}

spa_pod* build_buffer(spa_pod_builder* b, uint32_t blocks, uint32_t size, uint32_t stride, uint32_t datatype) {
    assert(blocks > 0);
    assert(datatype > 0);
    spa_pod_frame f[1];

    spa_pod_builder_push_object(b, &f[0], SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers);
    spa_pod_builder_add(b, SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(XDPH_PWR_BUFFERS, XDPH_PWR_BUFFERS_MIN, 32), 0);
    spa_pod_builder_add(b, SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(blocks), 0);
    if (size > 0) {
        spa_pod_builder_add(b, SPA_PARAM_BUFFERS_size, SPA_POD_Int(size), 0);
    }
    if (stride > 0) {
        spa_pod_builder_add(b, SPA_PARAM_BUFFERS_stride, SPA_POD_Int(stride), 0);
    }
    spa_pod_builder_add(b, SPA_PARAM_BUFFERS_align, SPA_POD_Int(XDPH_PWR_ALIGN), 0);
    spa_pod_builder_add(b, SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(datatype), 0);
    return (spa_pod*)spa_pod_builder_pop(b, &f[0]);
}

spa_pod* fixate_format(spa_pod_builder* b, spa_video_format format, uint32_t width, uint32_t height, uint32_t framerate, uint64_t* modifier) {
    spa_pod_frame    f[1];

    spa_video_format format_without_alpha = pwStripAlpha(format);

    spa_pod_builder_push_object(b, &f[0], SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
    spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
    /* format */
    if (modifier || format_without_alpha == SPA_VIDEO_FORMAT_UNKNOWN) {
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format), 0);
    } else {
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(3, format, format, format_without_alpha), 0);
    }
    /* modifiers */
    if (modifier) {
        // implicit modifier
        spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY);
        spa_pod_builder_long(b, *modifier);
    }
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&SPA_RECTANGLE(width, height)), 0);
    // variable framerate
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&SPA_FRACTION(0, 1)), 0);
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction(&SPA_FRACTION(framerate, 1), &SPA_FRACTION(1, 1), &SPA_FRACTION(framerate, 1)), 0);
    return (spa_pod*)spa_pod_builder_pop(b, &f[0]);
}

spa_pod* build_format(spa_pod_builder* b, spa_video_format format, uint32_t width, uint32_t height, uint32_t framerate, uint64_t* modifiers, int modifier_count) {
    spa_pod_frame    f[2];
    int              i, c;

    spa_video_format format_without_alpha = pwStripAlpha(format);

    spa_pod_builder_push_object(b, &f[0], SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
    spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
    /* format */
    if (modifier_count > 0 || format_without_alpha == SPA_VIDEO_FORMAT_UNKNOWN) {
        // modifiers are defined only in combinations with their format
        // we should not announce the format without alpha
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format), 0);
    } else {
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(3, format, format, format_without_alpha), 0);
    }
    /* modifiers */
    if (modifier_count > 0) {
        // build an enumeration of modifiers
        spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        spa_pod_builder_push_choice(b, &f[1], SPA_CHOICE_Enum, 0);
        // modifiers from the array
        for (i = 0, c = 0; i < modifier_count; i++) {
            spa_pod_builder_long(b, modifiers[i]);
            if (c++ == 0)
                spa_pod_builder_long(b, modifiers[i]);
        }
        spa_pod_builder_pop(b, &f[1]);
    }
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&SPA_RECTANGLE(width, height)), 0);
    // variable framerate
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&SPA_FRACTION(0, 1)), 0);
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction(&SPA_FRACTION(framerate, 1), &SPA_FRACTION(1, 1), &SPA_FRACTION(framerate, 1)), 0);
    return (spa_pod*)spa_pod_builder_pop(b, &f[0]);
}

void randname(char* buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        assert(buf[i] == 'X');
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

int anonymous_shm_open() {
    char name[]  = "/xdph-shm-XXXXXX";
    int  retries = 100;

    do {
        randname(name + strlen(name) - 6);

        --retries;
        // shm_open guarantees that O_CLOEXEC is set
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);

    return -1;
}

SP<CCWlBuffer> import_wl_shm_buffer(int fd, wl_shm_format fmt, int width, int height, int stride) {
    int size = stride * height;

    if (fd < 0)
        return nullptr;

    auto pool = makeShared<CCWlShmPool>(g_pPortalManager->m_sWaylandConnection.shm->sendCreatePool(fd, size));
    auto buf  = makeShared<CCWlBuffer>(pool->sendCreateBuffer(0, width, height, stride, fmt));

    return buf;
}
