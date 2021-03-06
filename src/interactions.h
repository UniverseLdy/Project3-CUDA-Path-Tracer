#pragma once

#include "intersections.h"
#define HALTON_SAMPLING

__host__ __device__
float halton(float index, float base) {
    float f = 1.f;
    float r = 0.f;

    while (index > 0)
    {
        f /= base;
        r += f * ((int)index % (int)base);
        index /= base;
    }
    return r;
}
// CHECKITOUT
/**
 * Computes a cosine-weighted random direction in a hemisphere.
 * Used for diffuse lighting.
 */
__host__ __device__
glm::vec3 calculateRandomDirectionInHemisphere(
        glm::vec3 normal, thrust::default_random_engine &rng) {
    thrust::uniform_real_distribution<float> u01(0, 1);

#ifdef HALTON_SAMPLING
    int x_base = 3;
    int y_base = 5;
    int x_res = 100;
    int y_res = 100;
    float x_raw = u01(rng);
    float y_raw = u01(rng);

    float x_hal = halton(y_raw * y_res * x_res + x_raw * x_res, x_base);
    float y_hal = halton(x_raw * x_res * y_res + y_raw * y_res, y_base);
    float up = sqrt(y_hal); // cos(theta)
    float over = sqrt(1 - up * up); // sin(theta)
    float around = x_hal * TWO_PI;
#else
    float up = sqrt(u01(rng)); // cos(theta)
    float over = sqrt(1 - up * up); // sin(theta)
    float around = u01(rng) * TWO_PI;
#endif

    // Find a direction that is not the normal based off of whether or not the
    // normal's components are all equal to sqrt(1/3) or whether or not at
    // least one component is less than sqrt(1/3). Learned this trick from
    // Peter Kutz.

    glm::vec3 directionNotNormal;
    if (abs(normal.x) < SQRT_OF_ONE_THIRD) {
        directionNotNormal = glm::vec3(1, 0, 0);
    } else if (abs(normal.y) < SQRT_OF_ONE_THIRD) {
        directionNotNormal = glm::vec3(0, 1, 0);
    } else {
        directionNotNormal = glm::vec3(0, 0, 1);
    }

    // Use not-normal direction to generate two perpendicular directions
    glm::vec3 perpendicularDirection1 =
        glm::normalize(glm::cross(normal, directionNotNormal));
    glm::vec3 perpendicularDirection2 =
        glm::normalize(glm::cross(normal, perpendicularDirection1));

    return up * normal
        + cos(around) * over * perpendicularDirection1
        + sin(around) * over * perpendicularDirection2;
}

/**
 * Scatter a ray with some probabilities according to the material properties.
 * For example, a diffuse surface scatters in a cosine-weighted hemisphere.
 * A perfect specular surface scatters in the reflected ray direction.
 * In order to apply multiple effects to one surface, probabilistically choose
 * between them.
 * 
 * The visual effect you want is to straight-up add the diffuse and specular
 * components. You can do this in a few ways. This logic also applies to
 * combining other types of materias (such as refractive).
 * 
 * - Always take an even (50/50) split between a each effect (a diffuse bounce
 *   and a specular bounce), but divide the resulting color of either branch
 *   by its probability (0.5), to counteract the chance (0.5) of the branch
 *   being taken.
 *   - This way is inefficient, but serves as a good starting point - it
 *     converges slowly, especially for pure-diffuse or pure-specular.
 * - Pick the split based on the intensity of each material color, and divide
 *   branch result by that branch's probability (whatever probability you use).
 *
 * This method applies its changes to the Ray parameter `ray` in place.
 * It also modifies the color `color` of the ray in place.
 *
 * You may need to change the parameter list for your purposes!
 */
__host__ __device__
void scatterRay(
		PathSegment & pathSegment,
        glm::vec3 intersect,
        glm::vec3 normal,
        const Material &m,
        thrust::default_random_engine &rng) {

    thrust::uniform_real_distribution<float> u01(0, 1);
    // TODO: implement this.
    // A basic implementation of pure-diffuse shading will just call the
     //calculateRandomDirectionInHemisphere defined above.
    if (m.hasRefractive)
    {
        // Refractive material

        float unit_projection = glm::dot(pathSegment.ray.direction, normal);
        float eta = (unit_projection > 0) ? (m.indexOfRefraction) : (1.f / m.indexOfRefraction);

        // Schlick's approximation
        float R0 = powf((1.0f - eta) / (1.0f + eta), 2.f);
        float R = R0 + (1 - R0) * powf(1 - glm::abs(unit_projection), 5.f);
        if (R < u01(rng)) {
            // Refracting Light
            pathSegment.ray.direction = glm::refract(pathSegment.ray.direction, normal, eta);
            normal = -normal;
            pathSegment.color *= m.color;
        }
        else {
            // Reflecting Light
            pathSegment.ray.direction = glm::reflect(pathSegment.ray.direction, normal);
            pathSegment.color *= m.specular.color;
        }
    }
    else if (m.hasReflective) 
    { 
        // Reflective material
        pathSegment.ray.direction = glm::reflect(pathSegment.ray.direction, normal);
        pathSegment.color *= m.specular.color;
    }
    else 
    {                
        // Diffuse material
        pathSegment.ray.direction = calculateRandomDirectionInHemisphere(normal, rng);
        pathSegment.color *= m.color;
    }

    glm::clamp(pathSegment.color, glm::vec3(0.0f), glm::vec3(1.0f));// Clamp the color
    glm::vec3 offset = (glm::dot(pathSegment.ray.direction, normal) > 0) ? 0.0005f * normal : -0.0005f * normal;
    pathSegment.ray.origin = intersect + offset;// Shoot a new ray from the intersection point, add an offset to avoid shadow acne.
    pathSegment.remainingBounces -= 1;// A bounce happened, remaining bounces -1.
}