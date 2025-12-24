// Fragment shader based on Sombrero.frag
// Adapted for standard OpenGL (not ReShade)

#ifdef GL_ES
    precision mediump float;
#endif

#define float float
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define float2x2 mat2
#define float3x3 mat3
#define float4x4 mat4
#define SAMPLE_TEXTURE(name, coord) texture(name, coord)
#define DECLARE_UNIFORM(type, name) uniform type name;

// Uniforms from Sombrero.frag
DECLARE_UNIFORM(sampler2D, screenTexture)
DECLARE_UNIFORM(sampler2D, calibratingTexture)
DECLARE_UNIFORM(sampler2D, customBannerTexture)

DECLARE_UNIFORM(float2, banner_position)
DECLARE_UNIFORM(float, day_in_seconds)

// Virtual display uniforms
DECLARE_UNIFORM(bool, virtual_display_enabled)
DECLARE_UNIFORM(float4x4, pose_orientation)
DECLARE_UNIFORM(float3, pose_position)
DECLARE_UNIFORM(float4, look_ahead_cfg)
DECLARE_UNIFORM(float2, display_resolution)
DECLARE_UNIFORM(float2, source_to_display_ratio)
DECLARE_UNIFORM(float, display_size)
DECLARE_UNIFORM(float, display_north_offset)
DECLARE_UNIFORM(float3, lens_vector)
DECLARE_UNIFORM(float3, lens_vector_r)
DECLARE_UNIFORM(float2, texcoord_x_limits)
DECLARE_UNIFORM(float2, texcoord_x_limits_r)
DECLARE_UNIFORM(bool, show_banner)
DECLARE_UNIFORM(float, frametime)
DECLARE_UNIFORM(float, look_ahead_ms)
DECLARE_UNIFORM(bool, custom_banner_enabled)
DECLARE_UNIFORM(float2, trim_percent)
DECLARE_UNIFORM(bool, curved_display)
DECLARE_UNIFORM(bool, sbs_enabled)

// FOV uniforms
DECLARE_UNIFORM(float, half_fov_z_rads)
DECLARE_UNIFORM(float, half_fov_y_rads)
DECLARE_UNIFORM(float2, fov_half_widths)
DECLARE_UNIFORM(float2, fov_widths)

uniform float4 imu_reset_data = vec4(0.0, 0.0, 0.0, 1.0);
uniform float look_ahead_ms_cap = 45.0;

// Sideview uniforms
DECLARE_UNIFORM(bool, sideview_enabled)
DECLARE_UNIFORM(float, sideview_position)

in vec2 texCoord;
out vec4 fragColor;

// Include the core PS_Sombrero function from Sombrero.frag
// (We'll need to adapt the GLSL syntax)
// For now, include the key math functions and main logic

float mod_float(float x, float y) {
    return mod(x, y);
}

float4 quatMul(float4 q1, float4 q2) {
    float3 u = q1.xyz;
    float s = q1.w;
    float3 v = q2.xyz;
    float t = q2.w;
    return float4(s*v + t*u + cross(u, v), s*t - dot(u, v));
}

float4 quatConj(float4 q) {
    return float4(-q.x, -q.y, -q.z, q.w);
}

float3 applyQuaternionToVector(float4 q, float3 v) {
    float4 p = quatMul(quatMul(q, float4(v, 0.0)), quatConj(q));
    return p.xyz;
}

float3 applyLookAhead(float3 position, float3 velocity, float t) {
    return position + velocity * t;
}

float3 rateOfChange(float3 v1, float3 v2, float delta_time) {
    return (v1 - v2) / delta_time;
}

bool isKeepaliveRecent(float4 currentDate, float4 keepAliveDate) {
    return abs(mod(currentDate.w + day_in_seconds - keepAliveDate.w, day_in_seconds)) <= 5.0;
}

float getVectorScaleToCurve(float radius, float2 vectorStart, float2 lookVector) {
    float a = pow(lookVector.x, 2.0) + pow(lookVector.y, 2.0);
    float b = 2.0 * (lookVector.x * vectorStart.x + lookVector.y * vectorStart.y);
    float c = pow(vectorStart.x, 2.0) + pow(vectorStart.y, 2.0) - pow(radius, 2.0);

    float discriminant = pow(b, 2.0) - 4.0 * a * c;
    if (discriminant < 0.0) return -1.0;

    float sqrtDiscriminant = sqrt(discriminant);
    return max(
        (-b + sqrtDiscriminant) / (2.0 * a),
        (-b - sqrtDiscriminant) / (2.0 * a)
    );
}

float2 applySideviewTransform(float2 texcoord) {
    float2 texcoord_mins = vec2(0.0, 0.0);

    if (sideview_position == 2.0 || sideview_position == 3.0) {
        texcoord_mins.y = 1.0 - display_size;
    }

    if (sideview_position == 1.0 || sideview_position == 3.0) {
        texcoord_mins.x = 1.0 - display_size;
    }

    if (sideview_position == 4.0) {
        texcoord_mins.x = texcoord_mins.y = (1.0 - display_size) / 2.0;
    }

    return (texcoord - texcoord_mins) / display_size;
}

void main() {
    // For now, just display the texture directly
    // Full PS_Sombrero implementation will be added
    fragColor = texture(screenTexture, texCoord);
}

