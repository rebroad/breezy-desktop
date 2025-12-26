/*
 * Shared math library for Breezy Desktop - Implementation
 */

#include "breezy_math.h"
#include <string.h>

/* ============================================================================
 * FOV Conversion
 * ============================================================================ */

BreezyFOVs breezy_diagonal_to_cross_fovs(double diagonal_fov_radians, double aspect_ratio) {
    // First convert from a spherical FOV to a diagonal FOV on a flat plane at a generic distance of 1.0
    double flat_diagonal_fov = 2.0 * tan(diagonal_fov_radians / 2.0);

    // Then convert to flat plane horizontal and vertical FOVs
    double flat_vertical_fov = flat_diagonal_fov / sqrt(1.0 + aspect_ratio * aspect_ratio);
    double flat_horizontal_fov = flat_vertical_fov * aspect_ratio;

    // Then convert back to spherical FOV
    BreezyFOVs result;
    result.diagonal = diagonal_fov_radians;
    result.horizontal = 2.0 * atan(flat_horizontal_fov / 2.0);
    result.vertical = 2.0 * atan(flat_vertical_fov / 2.0);
    return result;
}

/* ============================================================================
 * FOV Conversion Functions (Flat Display)
 * ============================================================================ */

double breezy_fov_flat_center_to_fov_edge_distance(double center_distance, double fov_length) {
    // Distance to an edge is the hypotenuse of the triangle where the opposite
    // side is half the width of the reference FOV screen
    double half_fov_length = fov_length / 2.0;
    return sqrt(half_fov_length * half_fov_length + center_distance * center_distance);
}

double breezy_fov_flat_fov_edge_to_screen_center_distance(double edge_distance, double screen_length) {
    double half_screen_length = screen_length / 2.0;
    return sqrt(edge_distance * edge_distance - half_screen_length * half_screen_length);
}

double breezy_fov_flat_length_to_radians(double fov_radians, double fov_length,
                                          double screen_edge_distance, double to_length) {
    (void)fov_radians;  // Unused for flat displays
    (void)fov_length;   // Unused for flat displays
    return asin(to_length / 2.0 / screen_edge_distance) * 2.0;
}

double breezy_fov_flat_angle_to_length(double fov_radians, double fov_length,
                                        double screen_distance,
                                        double to_angle_opposite, double to_angle_adjacent) {
    (void)fov_radians;   // Unused for flat displays
    (void)fov_length;    // Unused for flat displays
    return to_angle_opposite / to_angle_adjacent * screen_distance;
}

/* ============================================================================
 * FOV Conversion Functions (Curved Display)
 * ============================================================================ */

double breezy_fov_curved_length_to_radians(double fov_radians, double fov_length,
                                            double screen_edge_distance, double to_length) {
    (void)screen_edge_distance;  // Unused for curved displays
    // For curved displays, scaling is linear
    return fov_radians / fov_length * to_length;
}

double breezy_fov_curved_angle_to_length(double fov_radians, double fov_length,
                                          double screen_distance,
                                          double to_angle_opposite, double to_angle_adjacent) {
    (void)screen_distance;  // Unused for curved displays
    return fov_length / fov_radians * atan2(to_angle_opposite, to_angle_adjacent);
}

int breezy_fov_curved_radians_to_segments(double screen_radians) {
    // Segments per radian: 20 segments per 90 degrees
    const double segments_per_radian = 20.0 / breezy_degree_to_radian(90.0);
    return (int)ceil(screen_radians * segments_per_radian);
}

/* ============================================================================
 * Quaternion and Vector Math
 * ============================================================================ */

void breezy_apply_quaternion_to_vector(float *result, const float *vector, const float *quaternion) {
    // quaternion is [x, y, z, w]
    float t[3] = {
        2.0f * (quaternion[1] * vector[2] - quaternion[2] * vector[1]),
        2.0f * (quaternion[2] * vector[0] - quaternion[0] * vector[2]),
        2.0f * (quaternion[0] * vector[1] - quaternion[1] * vector[0])
    };

    result[0] = vector[0] + quaternion[3] * t[0] + quaternion[1] * t[2] - quaternion[2] * t[1];
    result[1] = vector[1] + quaternion[3] * t[1] + quaternion[2] * t[0] - quaternion[0] * t[2];
    result[2] = vector[2] + quaternion[3] * t[2] + quaternion[0] * t[1] - quaternion[1] * t[0];
}

void breezy_multiply_quaternions(float *result, const float *q1, const float *q2) {
    // q1 = [x1, y1, z1, w1], q2 = [x2, y2, z2, w2]
    result[0] = q1[3] * q2[0] + q1[0] * q2[3] + q1[1] * q2[2] - q1[2] * q2[1];
    result[1] = q1[3] * q2[1] - q1[0] * q2[2] + q1[1] * q2[3] + q1[2] * q2[0];
    result[2] = q1[3] * q2[2] + q1[0] * q2[1] - q1[1] * q2[0] + q1[2] * q2[3];
    result[3] = q1[3] * q2[3] - q1[0] * q2[0] - q1[1] * q2[1] - q1[2] * q2[2];
}

void breezy_conjugate_quaternion(float *result, const float *q) {
    result[0] = -q[0];
    result[1] = -q[1];
    result[2] = -q[2];
    result[3] = q[3];
}

void breezy_slerp_quaternion(float *result, const float *q1, const float *q2, float t) {
    // Clamp t to [0, 1]
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    // Calculate dot product
    float dot = q1[0] * q2[0] + q1[1] * q2[1] + q1[2] * q2[2] + q1[3] * q2[3];

    // If dot product is negative, negate one quaternion to take shorter path
    float actual_q2[4];
    if (dot < 0.0f) {
        actual_q2[0] = -q2[0];
        actual_q2[1] = -q2[1];
        actual_q2[2] = -q2[2];
        actual_q2[3] = -q2[3];
        dot = -dot;
    } else {
        actual_q2[0] = q2[0];
        actual_q2[1] = q2[1];
        actual_q2[2] = q2[2];
        actual_q2[3] = q2[3];
    }

    // Clamp dot product to [-1, 1] to avoid numerical errors
    if (dot > 1.0f) dot = 1.0f;
    if (dot < -1.0f) dot = -1.0f;

    // Calculate angle
    float theta = acosf(dot);
    float sin_theta = sinf(theta);

    if (sin_theta < 1e-6f) {
        // Quaternions are very close, use linear interpolation
        float one_minus_t = 1.0f - t;
        result[0] = one_minus_t * q1[0] + t * actual_q2[0];
        result[1] = one_minus_t * q1[1] + t * actual_q2[1];
        result[2] = one_minus_t * q1[2] + t * actual_q2[2];
        result[3] = one_minus_t * q1[3] + t * actual_q2[3];
    } else {
        // Spherical linear interpolation
        float one_minus_t = 1.0f - t;
        float w1 = sinf(one_minus_t * theta) / sin_theta;
        float w2 = sinf(t * theta) / sin_theta;

        result[0] = w1 * q1[0] + w2 * actual_q2[0];
        result[1] = w1 * q1[1] + w2 * actual_q2[1];
        result[2] = w1 * q1[2] + w2 * actual_q2[2];
        result[3] = w1 * q1[3] + w2 * actual_q2[3];
    }

    // Normalize result
    float len = sqrtf(result[0] * result[0] + result[1] * result[1] +
                      result[2] * result[2] + result[3] * result[3]);
    if (len > 0.0f) {
        result[0] /= len;
        result[1] /= len;
        result[2] /= len;
        result[3] /= len;
    }
}

/* ============================================================================
 * Display Distance and Scaling
 * ============================================================================ */

float breezy_adjust_display_distance_for_monitor_size(float base_distance,
                                                       float focused_width, float focused_height,
                                                       float fov_width, float fov_height) {
    float ratio_w = focused_width / fov_width;
    float ratio_h = focused_height / fov_height;
    float focused_monitor_size_adjustment = (ratio_w > ratio_h) ? ratio_w : ratio_h;
    return base_distance / focused_monitor_size_adjustment;
}

/* ============================================================================
 * Smooth Follow Progress Calculation
 * ============================================================================ */

float breezy_smooth_follow_slerp_progress(uint64_t elapsed_ms) {
    // These need to mirror the values in XRLinuxDriver
    // https://github.com/rebroad/XRLinuxDriver/blob/main/src/plugins/smooth_follow.c#L31
    const uint64_t SMOOTH_FOLLOW_SLERP_TIMELINE_MS = 1000;
    const double SMOOTH_FOLLOW_SLERP_FACTOR = pow(1.0 - 0.999, 1.0 / (double)SMOOTH_FOLLOW_SLERP_TIMELINE_MS);

    // This mirrors how the driver's slerp function progresses so our effect will match it
    return 1.0f - (float)pow(SMOOTH_FOLLOW_SLERP_FACTOR, (double)elapsed_ms);
}

float breezy_calculate_look_ahead_ms(uint64_t imu_timestamp_ms, uint64_t current_time_ms,
                                      float look_ahead_constant, float look_ahead_override) {
    // How stale the IMU data is
    uint64_t data_age = (current_time_ms > imu_timestamp_ms) ?
                        (current_time_ms - imu_timestamp_ms) : 0;

    // Use override if provided, otherwise use constant
    float look_ahead = (look_ahead_override >= 0.0f) ? look_ahead_override : look_ahead_constant;

    return look_ahead + (float)data_age;
}

/* ============================================================================
 * Perspective Matrix
 * ============================================================================ */

void breezy_perspective_matrix(float *result,
                                float fov_horizontal_radians,
                                float aspect,
                                float near,
                                float far) {
    float f = 1.0f / tanf(fov_horizontal_radians / 2.0f);
    float range = far - near;

    // Column-major order (OpenGL style)
    result[0] = f / aspect;  result[4] = 0.0f;  result[8]  = 0.0f;                      result[12] = 0.0f;
    result[1] = 0.0f;        result[5] = f;     result[9]  = 0.0f;                      result[13] = 0.0f;
    result[2] = 0.0f;        result[6] = 0.0f;  result[10] = -(far + near) / range;     result[14] = -1.0f;
    result[3] = 0.0f;        result[7] = 0.0f;  result[11] = -(2.0f * near * far) / range; result[15] = 0.0f;
}

