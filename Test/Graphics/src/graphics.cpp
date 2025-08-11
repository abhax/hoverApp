#ifndef GRAPHIC_H
#define GRAPHICS_H

#endif //GRAPHICS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define DRM_DEVICE_PATH "/dev/dri/card0"

int main() {
    int drm_fd;
    drmModeRes *res;
    drmModeConnector *conn;
    drmModeEncoder *enc;
    drmModeModeInfo mode;

    // Open the DRM device
    drm_fd = open(DRM_DEVICE_PATH, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        perror("Failed to open DRM device");
        return 1;
    }

    // Get the resources (connectors, encoders, modes, etc.)
    res = drmModeGetResources(drm_fd);
    if (!res) {
        perror("Failed to get DRM resources");
        close(drm_fd);
        return 1;
    }

    // Find a connected connector
    int i;
    for (i = 0; i < res->count_connectors; ++i) {
        conn = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (conn->connection == DRM_MODE_CONNECTED)
            break;
        drmModeFreeConnector(conn);
    }

    if (i == res->count_connectors) {
        fprintf(stderr, "No connected connector found\n");
        drmModeFreeResources(res);
        close(drm_fd);
        return 1;
    }

    // Find an encoder associated with the connector
    enc = drmModeGetEncoder(drm_fd, conn->encoder_id);
    if (!enc) {
        fprintf(stderr, "Failed to get encoder\n");
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        close(drm_fd);
        return 1;
    }

    // Get the preferred mode of the connector
    mode = conn->modes[0];

    // Create framebuffer
    uint32_t fb_id;
    uint32_t handle, stride;
    uint32_t width = mode.hdisplay;
    uint32_t height = mode.vdisplay;
    uint32_t depth = 24;
    uint32_t bpp = 32;

    drmModeFBPtr fb;
    int ret = drmModeAddFB(drm_fd, width, height, depth, bpp, 0, &fb_id);
    if (ret) {
        perror("Failed to add framebuffer");
        drmModeFreeEncoder(enc);
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        close(drm_fd);
        return 1;
    }

    // Cleanup
    drmModeFreeFB(fb);
    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    close(drm_fd);

    return 0;
}
