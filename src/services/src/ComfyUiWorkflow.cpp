// Building the Qwen Image 2.1 text-to-image workflow in ComfyUI's API format.
#include "ComfyUiWorkflow.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace mikserstudio::services::comfyui {

namespace {

QJsonObject node(const QString& classType, const QJsonObject& inputs)
{
    return QJsonObject{{u"class_type"_s, classType}, {u"inputs"_s, inputs}};
}

QJsonArray link(const QString& nodeId, int output)
{
    return QJsonArray{nodeId, output};
}

}  // namespace

QList<std::pair<QString, QString>> requiredModels()
{
    return {
        {u"diffusion_models"_s, kDiffusionModel.toString()},
        {u"text_encoders"_s, kTextEncoder.toString()},
        {u"vae"_s, kVae.toString()},
    };
}

QStringList requiredNodes()
{
    return {u"QwenImage21Cache"_s, u"TextEncodeQwenImage21"_s};
}

QString stage(const QString& nodeId)
{
    if (nodeId == u"1")
        return u"Loading the image model"_s;
    if (nodeId == u"3")
        return u"Loading the text encoder"_s;
    if (nodeId == u"4")
        return u"Reading the prompt"_s;
    if (nodeId == u"7")
        return u"Loading the VAE"_s;
    if (nodeId == u"8")
        return u"Decoding the image"_s;
    if (nodeId == kSaveNode)
        return u"Saving the image"_s;
    return {};
}

int modelSize(int pixels)
{
    return pixels / 16 * 16;
}

QJsonObject imageWorkflow(const ImageRequest& request, const QString& filenamePrefix)
{
    return QJsonObject{
        {u"1"_s,
         node(u"UNETLoader"_s, {{u"unet_name"_s, kDiffusionModel.toString()}, {u"weight_dtype"_s, u"default"_s}})},
        {u"2"_s, node(u"QwenImage21Cache"_s,
                      {{u"model"_s, link(u"1"_s, 0)}, {u"device"_s, u"auto"_s}, {u"dtype"_s, u"default"_s}})},
        {u"3"_s, node(u"CLIPLoader"_s, {{u"clip_name"_s, kTextEncoder.toString()}, {u"type"_s, u"qwen_image"_s}})},
        {u"4"_s, node(u"TextEncodeQwenImage21"_s,
                      {
                          {u"clip"_s, link(u"3"_s, 0)},
                          {u"prompt"_s, request.prompt},
                          {u"negative_prompt"_s, request.negativePrompt},
                          {u"resolution"_s, 1024},
                      })},
        {u"5"_s, node(u"EmptyLatentImage"_s,
                      {
                          {u"width"_s, modelSize(request.width)},
                          {u"height"_s, modelSize(request.height)},
                          {u"batch_size"_s, 1},
                      })},
        {u"6"_s, node(u"KSampler"_s,
                      {
                          {u"model"_s, link(u"2"_s, 0)},
                          {u"seed"_s, request.seed},
                          {u"steps"_s, request.steps},
                          {u"cfg"_s, 1.0},
                          {u"sampler_name"_s, u"euler"_s},
                          {u"scheduler"_s, u"simple"_s},
                          {u"positive"_s, link(u"4"_s, 0)},
                          {u"negative"_s, link(u"4"_s, 1)},
                          {u"latent_image"_s, link(u"5"_s, 0)},
                          {u"denoise"_s, 1.0},
                      })},
        {u"7"_s, node(u"VAELoader"_s, {{u"vae_name"_s, kVae.toString()}})},
        {u"8"_s, node(u"VAEDecode"_s, {{u"samples"_s, link(u"6"_s, 0)}, {u"vae"_s, link(u"7"_s, 0)}})},
        {kSaveNode.toString(),
         node(u"SaveImage"_s, {{u"images"_s, link(u"8"_s, 0)}, {u"filename_prefix"_s, filenamePrefix}})},
    };
}

}  // namespace mikserstudio::services::comfyui
