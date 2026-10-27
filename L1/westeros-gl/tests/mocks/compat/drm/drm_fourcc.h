/* Ubuntu installs this libdrm header under libdrm/, not drm/.
 * The pkg-config include path supplies it without modifying production includes.
 */
#ifndef WESTEROS_L1_DRM_FOURCC_FORWARD_H
#define WESTEROS_L1_DRM_FOURCC_FORWARD_H
#include <drm_fourcc.h>
#endif