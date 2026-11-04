#include "caption-source.hpp"

#include <obs-module.h>
#include <util/bmem.h>

namespace {

struct CaptionFilter {
    obs_source_t *source = nullptr;
    char *text = nullptr;
};

const char *source_name(void *)
{
    return obs_module_text("LiveTranslateFilter");
}

void apply_text(CaptionFilter *data)
{
    if (!data || !data->source)
        return;

    obs_source_t *target = obs_filter_get_target(data->source);
    if (!target)
        return;

    obs_data_t *settings = obs_source_get_settings(target);
    obs_data_set_string(settings, "text", data->text ? data->text : "");
    obs_source_update(target, settings);
    obs_data_release(settings);
}

void *filter_create(obs_data_t *settings, obs_source_t *source)
{
    auto *data = new CaptionFilter;
    data->source = source;
    data->text = bstrdup(obs_data_get_string(settings, "caption_text"));
    apply_text(data);
    return data;
}

void filter_destroy(void *opaque)
{
    auto *data = static_cast<CaptionFilter *>(opaque);
    if (data) {
        bfree(data->text);
        delete data;
    }
}

void filter_update(void *opaque, obs_data_t *settings)
{
    auto *data = static_cast<CaptionFilter *>(opaque);
    bfree(data->text);
    data->text = bstrdup(obs_data_get_string(settings, "caption_text"));
    apply_text(data);
}

obs_properties_t *filter_properties(void *)
{
    obs_properties_t *properties = obs_properties_create();
    obs_properties_add_text(properties, "caption_text", obs_module_text("CaptionText"), OBS_TEXT_DEFAULT);
    return properties;
}

void filter_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "caption_text", "English subtitles will appear here");
}

obs_source_frame *filter_video(void *, obs_source_frame *frame)
{
    return frame;
}

} // namespace

extern "C" void register_caption_source(void)
{
    obs_source_info info = {};
    info.id = "obs_live_translate_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_VIDEO;
    info.get_name = source_name;
    info.create = filter_create;
    info.destroy = filter_destroy;
    info.update = filter_update;
    info.get_defaults = filter_defaults;
    info.get_properties = filter_properties;
    info.filter_video = filter_video;
    obs_register_source(&info);
}
