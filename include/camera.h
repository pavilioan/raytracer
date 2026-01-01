#ifndef CAMERA_H
#define CAMERA_H

#include "hittable.h"
#include "material.h"
#include "rtweekend.h"
#include "vec3.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

class camera {
public:
  double aspect_ratio = 1.0;  // Ratio width/height
  int image_width = 100;      // Rendered image width in dots
  int samples_per_pixel = 10; // Count of random samples per dot
  int max_depth;              // Max num of ray bounces into the scene

  double vfov = 90;                  // Vertical field of view in degrees
  point3 lookfrom = point3(0, 0, 0); // Point camera is looking from
  point3 lookat = point3(0, 0, -1);  // Point camera is looking at
  vec3 vup = vec3(0, 1, 0);          // Camera-relative up direction

  double defocus_angle = 0; // Variation angle of rays through each pixel
  double focus_dist = 10;   // Distance from camera to plane of perfect focus

  void render(const hittable &world) {
    initialize();

    std::cout << "P3\n" << image_width << " " << image_height << "\n255\n";

    // Create a buffer to store pixel colors

    std::vector<color> image_buffer(image_width * image_height);
    std::atomic<int> scanlines_remaining{image_height};

    // Determine number of threads

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0)
      num_threads = 1; // Fallback if detection fails

    std::vector<std::thread> threads;
    std::mutex print_mutex; // Just in case we need to sync printing

    auto render_lines = [&](int start_line, int end_line) {
      for (int j = start_line; j < end_line; j++) {
        // Update progress
        int remaining = --scanlines_remaining;
        if (remaining % 10 == 0 || remaining == 0) {
          // Simple text output, might overlap but it's just progress
          // Using clogs for progress
          std::clog << "\rScanlines remaining: " << remaining << " "
                    << std::flush;
        }

        for (int i = 0; i < image_width; i++) {
          color pixel_color(0, 0, 0);
          for (int sample = 0; sample < samples_per_pixel; sample++) {
            ray r = get_ray(i, j);
            pixel_color += ray_color(r, max_depth, world);
          }

          // Store in buffer instead of printing
          // Note: 'j' is 0 at top, so index calculation is straightforward

          image_buffer[j * image_width + i] = pixel_samples_scale * pixel_color;
        }
      }
    };

    // Divide work

    auto start_time = std::chrono::high_resolution_clock::now();

    int lines_per_thread = image_height / num_threads;
    for (unsigned int t = 0; t < num_threads; ++t) {
      int start = t * lines_per_thread;
      int end =
          (t == num_threads - 1) ? image_height : (t + 1) * lines_per_thread;
      threads.emplace_back(render_lines, start, end);
    }

    // Wait for all threads

    for (auto &t : threads) {
      t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    std::clog << "\rDone. Render time: " << elapsed.count() << "s\n";

    // Output the image from buffer

    for (const auto &pixel : image_buffer) {
      write_color(std::cout, pixel);
    }
  }

private:
  int image_height;           // Rendered image height
  double pixel_samples_scale; // Color scale factor for sum of pixel samples
  point3 center;              // Cam center
  point3 pixel00_loc;         // Upper left pixel location
  vec3 pixel_delta_u;         // Change in horizontal pixel
  vec3 pixel_delta_v;         // Change in vertical pixel
  vec3 w, u, v;               // Basis vectors for camera
  vec3 defocus_disk_u;        // Defocus disk horizontal radius
  vec3 defocus_disk_v;        // Defocus disk vertical radius

  void initialize() {

    // Calculate image height and ensure is at least 1

    image_height = int(image_width / aspect_ratio);
    image_height = (image_height < 1) ? 1 : image_height;

    pixel_samples_scale = 1.0 / samples_per_pixel;

    center = lookfrom;

    // Determine viewport dimensions

    auto theta = degrees_to_radians(vfov);
    auto h = std::tan(theta / 2);
    auto viewport_height = 2.0 * h * focus_dist;
    auto viewport_width =
        viewport_height * (double(image_width) / image_height);

    // Calculate unit basis vectors

    w = unit_vector(lookfrom - lookat);
    u = unit_vector(cross(vup, w));
    v = cross(w, u);

    // Horizontal and vertical viewport vector calculus

    vec3 viewport_u = viewport_width * u;   // Horizontal vector
    vec3 viewport_v = viewport_height * -v; // Vertical vector (down)

    // Pixel delta vector calculations

    pixel_delta_u = viewport_u / image_width;
    pixel_delta_v = viewport_v / image_height;

    // Location of upper left pixel

    auto viewport_upper_left =
        center - (focus_dist * w) - viewport_u / 2 - viewport_v / 2;
    pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

    // Camera defocus disk basis vectors

    auto defocus_radius =
        focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
    defocus_disk_u = u * defocus_radius;
    defocus_disk_v = v * defocus_radius;
  }

  ray get_ray(int i, int j) const {
    // Construct ray originating from camera (origin) and directed at randomly
    // sampled point around target pixel location i,j

    auto offset = sample_square();
    auto pixel_sample = pixel00_loc + ((i + offset.x()) * pixel_delta_u) +
                        ((j + offset.y()) * pixel_delta_v);
    auto ray_origin = (defocus_angle <= 0) ? center : defocus_disk_sample();
    auto ray_direction = pixel_sample - ray_origin;

    return ray(ray_origin, ray_direction);
  }

  vec3 sample_square() const {
    // Returns vector to random point in unit square
    return vec3(random_double() - 0.5, random_double() - 0.5, 0);
  }

  point3 defocus_disk_sample() const {
    // Returns random point in camera defocus disk

    auto p = random_in_unit_disk();
    return center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
  }

  color ray_color(const ray &r, int depth, const hittable &world) const {
    // No more light is gathered once ray bounce limit is exceeded
    if (depth <= 0) {
      return color(0, 0, 0);
    }

    hit_record rec;

    if (world.hit(r, interval(0.001, infinity), rec)) {
      ray scattered;
      color attenuation;
      if (rec.mat->scatter(r, rec, attenuation, scattered))
        return attenuation * ray_color(scattered, depth - 1, world);
      return color(0, 0, 0);
    }

    vec3 unit_direction = unit_vector(r.direction());
    auto a = 0.5 * (unit_direction.y() + 1.0);
    return (1.0 - a) * color(1.0, 1.0, 1.0) + a * color(0.5, 0.7, 1.0);
  }
};

#endif