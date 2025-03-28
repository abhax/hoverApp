
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "graphics.h"


struct {
    int device;
    uint32_t connector_id;
    drmModeModeInfo mode_info;
    drmModeCrtc* crtc;
} drmData;

struct {
    struct gbm_device* gbm_device;
    struct gbm_surface* gbm_surface;
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    struct gbm_bo* previous_bo;
    uint32_t previous_fb;
} eglData;

drmModeConnector* findConnector(drmModeRes* resources)
{
    // iterate the connectors
    for (int i = 0; i < resources->count_connectors; i++) {
        drmModeConnector* connector = drmModeGetConnector(drmData.device, resources->connectors[i]);
        // pick the first connected connector
        if (connector->connection == DRM_MODE_CONNECTED) {
            return connector;
        }
        drmModeFreeConnector(connector);
    }
    // no connector found
    return NULL;
}

drmModeEncoder* findEncoder(drmModeRes* resources, drmModeConnector* connector)
{
    if (connector->encoder_id) {
        return drmModeGetEncoder(drmData.device, connector->encoder_id);
    }
    // no encoder found
    return NULL;
}

bool initDrm(const char* card)
{
    drmData.device = open(card, O_RDWR | O_CLOEXEC);
    drmModeRes* resources = drmModeGetResources(drmData.device);
    if (!resources) {
        fprintf(stderr, "device resources not found\n");
        return false;
    }

    // find a connector
    drmModeConnector* connector = findConnector(resources);
    if (!connector) {
        fprintf(stderr, "no connector found\n");
        return false;
    }
    // save the connector_id
    drmData.connector_id = connector->connector_id;
    // save the first mode
    drmData.mode_info = connector->modes[0];
    fprintf(stderr, "resolution: %ix%i\n", drmData.mode_info.hdisplay, drmData.mode_info.vdisplay);
    // find an encoder
    drmModeEncoder* encoder = findEncoder(resources, connector);
    if (!encoder) {
        fprintf(stderr, "no encoder found\n");
        return false;
    }
    // find a CRTC
    if (encoder->crtc_id) {
        drmData.crtc = drmModeGetCrtc(drmData.device, encoder->crtc_id);
    }
    drmModeFreeEncoder(encoder);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    return true;
}

void teardownDrm()
{
    // set the previous crtc
    drmModeSetCrtc(drmData.device, drmData.crtc->crtc_id, drmData.crtc->buffer_id, drmData.crtc->x, drmData.crtc->y, &drmData.connector_id, 1, &drmData.crtc->mode);
    drmModeFreeCrtc(drmData.crtc);
}

bool setupEGL()
{
    eglData.gbm_device = gbm_create_device(drmData.device);
    if (!eglData.gbm_device) {
        fprintf(stderr, "failed to create GBM device\n");
        return false;
    }

    eglData.display = eglGetDisplay((EGLNativeDisplayType)eglData.gbm_device);
    if (eglData.display == EGL_NO_DISPLAY) {
        return false;
    }
    if (!eglInitialize(eglData.display, NULL, NULL)) {
        EGLint error = eglGetError();
        if (error == EGL_BAD_DISPLAY) {
            fprintf(stderr, "failed to initialize EGL: bad display\n");
        } else {
            fprintf(stderr, "failed to initialize EGL\n");
        }
        return false;
    }
    return true;
}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

const char* getEGLErrorString()
{
    EGLint errors[] = {
        EGL_SUCCESS,
        EGL_NOT_INITIALIZED,
        EGL_BAD_ACCESS,
        EGL_BAD_ALLOC,
        EGL_BAD_ATTRIBUTE,
        EGL_BAD_CONTEXT,
        EGL_BAD_CONFIG,
        EGL_BAD_CURRENT_SURFACE,
        EGL_BAD_DISPLAY,
        EGL_BAD_SURFACE,
        EGL_BAD_MATCH,
        EGL_BAD_PARAMETER,
        EGL_BAD_NATIVE_PIXMAP,
        EGL_BAD_NATIVE_WINDOW,
        EGL_CONTEXT_LOST,
    };
    const char* errorNames[] = {
        "EGL_SUCCESS",
        "EGL_NOT_INITIALIZED",
        "EGL_BAD_ACCESS",
        "EGL_BAD_ALLOC",
        "EGL_BAD_ATTRIBUTE",
        "EGL_BAD_CONTEXT",
        "EGL_BAD_CONFIG",
        "EGL_BAD_CURRENT_SURFACE",
        "EGL_BAD_DISPLAY",
        "EGL_BAD_SURFACE",
        "EGL_BAD_MATCH",
        "EGL_BAD_PARAMETER",
        "EGL_BAD_NATIVE_PIXMAP",
        "EGL_BAD_NATIVE_WINDOW",
        "EGL_CONTEXT_LOST",
    };
    EGLint error = eglGetError();
    for (int i = 0; i < ARRAY_SIZE(errors); i++) {
        if (error == errors[i]) {
            return errorNames[i];
        }
    }
    return "UNKNOWN";
}

bool createWindow()
{
    int width = drmData.mode_info.hdisplay;
    int height = drmData.mode_info.vdisplay;

    eglBindAPI(EGL_OPENGL_API);
    EGLint attributes[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config;
    EGLint num_config;
    eglChooseConfig(eglData.display, attributes, &config, 1, &num_config);

    eglData.context = eglCreateContext(eglData.display, config, EGL_NO_CONTEXT, NULL);
    if (eglData.context == EGL_NO_CONTEXT) {
        fprintf(stderr, "failed to create EGL context: %s\n", getEGLErrorString());
        return false;
    }

    eglData.gbm_surface = gbm_surface_create(eglData.gbm_device, width, height, GBM_BO_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    eglData.surface = eglCreateWindowSurface(eglData.display, config, (EGLNativeWindowType)eglData.gbm_surface, NULL);
    if (eglData.surface == EGL_NO_SURFACE) {
        fprintf(stderr, "failed to create EGL window surface: %s\n", getEGLErrorString());
        return false;
    }

    eglMakeCurrent(eglData.display, eglData.surface, eglData.surface, eglData.context);

    return true;
}

bool initializeWindow()
{
    if (!setupEGL()) {
        fprintf(stderr, "failed to setup EGL\n");
        return false;
    }

    if (!createWindow()) {
        fprintf(stderr, "failed to create window\n");
        return false;
    }

    return true;
}

void tearDownWindow()
{
    if (eglData.previous_bo) {
        drmModeRmFB(drmData.device, eglData.previous_fb);
        gbm_surface_release_buffer(eglData.gbm_surface, eglData.previous_bo);
    }

    eglDestroySurface(eglData.display, eglData.surface);
    gbm_surface_destroy(eglData.gbm_surface);
    eglDestroyContext(eglData.display, eglData.context);
    eglTerminate(eglData.display);
    gbm_device_destroy(eglData.gbm_device);
}

void swapBuffers()
{
    eglSwapBuffers(eglData.display, eglData.surface);
    struct gbm_bo* bo = gbm_surface_lock_front_buffer(eglData.gbm_surface);
    union gbm_bo_handle handle_internal = gbm_bo_get_handle(bo);
    uint32_t handle = handle_internal.u32;
    uint32_t pitch = gbm_bo_get_stride(bo);
    uint32_t fb;
    drmModeAddFB(drmData.device, drmData.mode_info.hdisplay, drmData.mode_info.vdisplay, 24, 32, pitch, handle, &fb);
    drmModeSetCrtc(drmData.device, drmData.crtc->crtc_id, fb, 0, 0, &drmData.connector_id, 1, &drmData.mode_info);

    if (eglData.previous_bo) {
        drmModeRmFB(drmData.device, eglData.previous_fb);
        gbm_surface_release_buffer(eglData.gbm_surface, eglData.previous_bo);
    }
    eglData.previous_bo = bo;
    eglData.previous_fb = fb;
}

int doOpenGL(void)
{
    const char* card;
    card = "/dev/dri/card0";

    printf("initialize DRM\n");
    if (!initDrm(card)) {
        fprintf(stderr, "failed to initialize DRM\n");
        return EXIT_FAILURE;
    }

    printf("initialize window\n");
    if (!initializeWindow()) {
        fprintf(stderr, "failed to initialize window\n");
        return EXIT_FAILURE;
    }

    glClearColor(1, 0, 0, 1); // red
    sleep(10);

    tearDownWindow();
    teardownDrm();

    return EXIT_SUCCESS;
}