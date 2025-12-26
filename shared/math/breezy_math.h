/*
 * Shared math library for Breezy Desktop
 *
 * Pure C implementations of mathematical functions used by both GNOME extension
 * and X11 renderer for display positioning, FOV conversions, and transformations.
 */

#ifndef BREEZY_MATH_H
#define BREEZY_MATH_H

#include <stddef.h>
#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Basic Math Utilities
 * ============================================================================ */

/**
 * Convert degrees to radians
 */
static inline double breezy_degree_to_radian(double degree) {
    return degree * M_PI / 180.0;
}

/**
 * Normalize a 3D vector
 */
static inline void breezy_normalize_vector3(float *vector) {
    float length = sqrtf(vector[0] * vector[0] +
                         vector[1] * vector[1] +
                         vector[2] * vector[2]);
    if (length > 0.0f) {
        vector[0] /= length;
        vector[1] /= length;
        vector[2] /= length;
    }
}

/* ============================================================================
 * FOV Conversion Structures
 * ============================================================================ */

typedef struct {
    double diagonal;
    double horizontal;
    double vertical;
} BreezyFOVs;

/**
 * Convert diagonal FOV to horizontal and vertical FOVs
 *
 * FOV in radians is spherical, so doesn't follow Pythagoras' theorem.
 * First converts from spherical FOV to diagonal FOV on a flat plane,
 * then to horizontal/vertical FOVs, then back to spherical FOV.
 */
BreezyFOVs breezy_diagonal_to_cross_fovs(double diagonal_fov_radians, double aspect_ratio);

/* ============================================================================
 * FOV Conversion Functions (Flat Display)
 * ============================================================================ */

/**
 * Distance to an edge is the hypotenuse of the triangle where the opposite
 * side is half the width of the reference FOV screen
 */
double breezy_fov_flat_center_to_fov_edge_distance(double center_distance, double fov_length);

/**
 * Convert from FOV edge distance to screen center distance
 */
double breezy_fov_flat_fov_edge_to_screen_center_distance(double edge_distance, double screen_length);

/**
 * Convert length to radians for flat displays
 */
double breezy_fov_flat_length_to_radians(double fov_radians, double fov_length,
                                          double screen_edge_distance, double to_length);

/**
 * Convert angle to length for flat displays
 */
double breezy_fov_flat_angle_to_length(double fov_radians, double fov_length,
                                        double screen_distance,
                                        double to_angle_opposite, double to_angle_adjacent);

/**
 * Convert radians to segments for flat displays (always 1)
 */
static inline int breezy_fov_flat_radians_to_segments(double screen_radians) {
    (void)screen_radians;  // Unused for flat displays
    return 1;
}

/* ============================================================================
 * FOV Conversion Functions (Curved Display)
 * ============================================================================ */

/**
 * For curved displays, distance to edge is just the center distance
 */
static inline double breezy_fov_curved_center_to_fov_edge_distance(double center_distance, double fov_length) {
    (void)fov_length;  // Unused for curved displays
    return center_distance;
}

/**
 * For curved displays, edge to screen center distance is just the edge distance
 */
static inline double breezy_fov_curved_fov_edge_to_screen_center_distance(double edge_distance, double screen_length) {
    (void)screen_length;  // Unused for curved displays
    return edge_distance;
}

/**
 * Convert length to radians for curved displays (linear scaling)
 */
double breezy_fov_curved_length_to_radians(double fov_radians, double fov_length,
                                            double screen_edge_distance, double to_length);

/**
 * Convert angle to length for curved displays
 */
double breezy_fov_curved_angle_to_length(double fov_radians, double fov_length,
                                          double screen_distance,
                                          double to_angle_opposite, double to_angle_adjacent);

/**
 * Convert radians to segments for curved displays
 */
int breezy_fov_curved_radians_to_segments(double screen_radians);

/* ============================================================================
 * Quaternion and Vector Math
 * ============================================================================ */

/**
 * Apply quaternion rotation to a 3D vector
 *
 * @param result Output vector (3 floats)
 * @param vector Input vector (3 floats)
 * @param quaternion Quaternion [x, y, z, w]
 */
void breezy_apply_quaternion_to_vector(float *result, const float *vector, const float *quaternion);

/**
 * Quaternion multiplication: result = q1 * q2
 */
void breezy_multiply_quaternions(float *result, const float *q1, const float *q2);

/**
 * Quaternion conjugation: result = conjugate(q)
 */
void breezy_conjugate_quaternion(float *result, const float *q);

/**
 * Spherical linear interpolation (SLERP) between two quaternions
 *
 * @param result Output quaternion [x, y, z, w]
 * @param q1 Start quaternion [x, y, z, w]
 * @param q2 End quaternion [x, y, z, w]
 * @param t Interpolation factor [0.0, 1.0]
 */
void breezy_slerp_quaternion(float *result, const float *q1, const float *q2, float t);

/* ============================================================================
 * Display Distance and Scaling
 * ============================================================================ */

/**
 * Scale a 3D position vector by display distance ratio
 *
 * This makes the display appear larger (closer) or smaller (farther) when
 * it's focused. The scaling is applied uniformly to all coordinates.
 *
 * @param position Input/output position vector (3 floats) - modified in place
 * @param current_distance Current display distance (e.g., focused distance)
 * @param default_distance Default display distance (unfocused distance)
 */
static inline void breezy_scale_position_by_distance(float *position,
                                                       float current_distance,
                                                       float default_distance) {
    float scale = current_distance / default_distance;
    position[0] *= scale;
    position[1] *= scale;
    position[2] *= scale;
}

/**
 * Calculate display distance with monitor size adjustment
 *
 * Adjusts the display distance based on relative monitor size compared to
 * the FOV monitor, so that larger monitors appear at appropriate scale.
 *
 * @param base_distance Base display distance
 * @param focused_width Width of focused monitor
 * @param focused_height Height of focused monitor
 * @param fov_width Width of FOV monitor
 * @param fov_height Height of FOV monitor
 * @return Adjusted display distance
 */
float breezy_adjust_display_distance_for_monitor_size(float base_distance,
                                                       float focused_width, float focused_height,
                                                       float fov_width, float fov_height);

/* ============================================================================
 * Smooth Follow Progress Calculation
 * ============================================================================ */

/**
 * Calculate smooth follow SLERP progress based on elapsed time
 *
 * This mirrors how the XRLinuxDriver's slerp function progresses so effects
 * match the driver's behavior.
 *
 * @param elapsed_ms Elapsed time in milliseconds
 * @return Progress value [0.0, 1.0]
 */
float breezy_smooth_follow_slerp_progress(uint64_t elapsed_ms);

/**
 * Calculate look-ahead milliseconds
 *
 * @param imu_timestamp_ms IMU data timestamp in milliseconds
 * @param current_time_ms Current time in milliseconds
 * @param look_ahead_constant Constant look-ahead value
 * @param look_ahead_override Override value (-1 to use constant)
 * @return Look-ahead value in milliseconds
 */
float breezy_calculate_look_ahead_ms(uint64_t imu_timestamp_ms, uint64_t current_time_ms,
                                      float look_ahead_constant, float look_ahead_override);

/* ============================================================================
 * Perspective Matrix
 * ============================================================================ */

/**
 * Create a perspective projection matrix
 *
 * @param result Output 4x4 matrix (16 floats, column-major)
 * @param fov_horizontal_radians Horizontal FOV in radians
 * @param aspect Aspect ratio (width/height)
 * @param near Near plane distance
 * @param far Far plane distance
 */
void breezy_perspective_matrix(float *result,
                                float fov_horizontal_radians,
                                float aspect,
                                float near,
                                float far);

#endif /* BREEZY_MATH_H */

