/*
 * Copyright 2016 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <pebble.h>
#include "analog_layer.h"

/* Cycles:
 *  Regular numeric cycles:
 *   Minute of 60 seconds
 *   Hour of 60 minutes
 *   Day of 24 hours (but daylight savings...?)
 *  Regular named cycles
 *   Week of 7 named days
 *  Mostly-constant astronomical cycles:
 *   Moon phase cycle of about 29.53 days
 *   Solar year of about 365.25 days
 *  Irregular but well-defined cycles:
 *   Gregorian Month of 28, 29, 30, or 31 days
 *   Gregorian Year of 365 or 366 days
 */

/// Custom state per analog layer.
typedef struct {

    // The "absolute" moment to be displayed.
    time_t unix_time;

    // The timezone-local moment to be displayed.
    struct tm wall_time;

    // How thick the band of blue sky should be.
    int16_t sky_thickness;

} BSKY_AnalogData;

/// Make a smaller rect by trimming the edges of a larger one.
static GRect bsky_rect_trim (const GRect rect, const int8_t trim) {
    const GRect result = {
        .origin = {
            .x = rect.origin.x + trim,
            .y = rect.origin.y + trim,
        },
        .size = {
            .w = rect.size.w - trim*2,
            .h = rect.size.h - trim*2,
        },
    };
    return result;
}

static void bsky_analog_layer_update (Layer *layer, GContext *ctx) {
    const GColor color_sun_fill = GColorYellow;
    const GColor color_sun_stroke = GColorDarkCandyAppleRed;
    const GColor color_sky_fill [] = {
        GColorCyan,
        GColorElectricBlue,
        GColorCeleste,
    };
    const int8_t color_sky_fill_len =
        sizeof(color_sky_fill) / sizeof(color_sky_fill[0]);
    const GColor color_sky_stroke = GColorBlueMoon;

    const BSKY_AnalogData * const data = layer_get_data(layer);
    const GRect bounds = layer_get_bounds(layer);

    graphics_context_set_fill_color(ctx, GColorClear);
    graphics_fill_rect(ctx, bounds, 0, 0);

    // TODO: find value of inset_thickness based on available size, or
    //       set through public API of this module.

    // Update the Blue Sky
    const GRect sky_bounds = bounds;
    const int16_t sky_thickness =
        (sky_bounds.size.w > sky_bounds.size.h)
        ? sky_bounds.size.h / 6
        : sky_bounds.size.w / 6;
    for (int8_t sky_fill=0; sky_fill<color_sky_fill_len; ++sky_fill) {
        graphics_context_set_fill_color(ctx, color_sky_fill[sky_fill]);
        graphics_fill_radial(
                ctx,
                bsky_rect_trim(
                    sky_bounds,
                    sky_thickness * sky_fill / color_sky_fill_len),
                GOvalScaleModeFitCircle,
                sky_thickness / color_sky_fill_len,
                0,
                TRIG_MAX_ANGLE);
    }

    // Update the 24 hour markers
    const int32_t midnight_angle = TRIG_MAX_ANGLE / 2;
    const GRect sky_inset = bsky_rect_trim(sky_bounds, sky_thickness);
    graphics_context_set_stroke_color(ctx, color_sky_stroke);
    graphics_context_set_antialiased(ctx, true);
    for (int32_t hour = 0; hour < 24; ++hour) {
        const int32_t hour_angle =
            (midnight_angle + hour * TRIG_MAX_ANGLE / 24)
            % TRIG_MAX_ANGLE;
        const GPoint p0 = gpoint_from_polar(
                sky_inset,
                GOvalScaleModeFitCircle,
                hour_angle);
        const GPoint p1 = gpoint_from_polar(
                bounds,
                GOvalScaleModeFitCircle,
                hour_angle);
        graphics_context_set_stroke_width(ctx, hour % 3 ? 1 : 3);
        graphics_draw_line(ctx, p0, p1);
    }

    // Update the Sun
    const int32_t sun_angle = midnight_angle
        + TRIG_MAX_ANGLE * data->wall_time.tm_hour / 24
        + TRIG_MAX_ANGLE * data->wall_time.tm_min / (24 * 60);
    const int32_t sun_diameter = sky_thickness*3/4;
    const GRect sun_orbit = bsky_rect_trim(sky_bounds, sky_thickness/2);
    const GPoint sun_center = gpoint_from_polar(
            sun_orbit,
            GOvalScaleModeFitCircle,
            sun_angle);
    graphics_context_set_fill_color(ctx, color_sun_fill);
    graphics_fill_circle(ctx, sun_center, sun_diameter/2);
    graphics_context_set_stroke_color(ctx, color_sun_stroke);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_circle(ctx, sun_center, sun_diameter/2);
}

struct BSKY_AnalogLayer {

    // The real Pebble layer, of course.
    Layer *layer;

    // A conveniently typed pointer to custom state data, stored in the
    // layer itself.
    BSKY_AnalogData *data;
};

BSKY_AnalogLayer * bsky_analog_layer_create(GRect frame) {
    BSKY_AnalogLayer *analog_layer = malloc(sizeof(*analog_layer));
    if (analog_layer) {
        analog_layer->layer = layer_create_with_data(
                frame,
                sizeof(BSKY_AnalogData));
        if (!analog_layer->layer) {
            free(analog_layer);
            analog_layer = NULL;
        } else {
            analog_layer->data = layer_get_data(analog_layer->layer);
            layer_set_update_proc(
                    analog_layer->layer,
                    bsky_analog_layer_update);
        }
    }
    return analog_layer;
}

void bsky_analog_layer_destroy(BSKY_AnalogLayer *analog_layer) {
    layer_destroy(analog_layer->layer);
    analog_layer->layer = NULL;
    analog_layer->data = NULL;
    free(analog_layer);
}

Layer * bsky_analog_layer_get_layer(BSKY_AnalogLayer *analog_layer) {
    return analog_layer->layer;
}

void bsky_analog_layer_set_time(
        BSKY_AnalogLayer *analog_layer,
        time_t time) {
    analog_layer->data->unix_time = time;
    struct tm * wall_time = localtime(&time);
    analog_layer->data->wall_time = *wall_time;
    layer_mark_dirty(analog_layer->layer);
}